#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/debugfs.h>

#include "edu_char.h"
#include "edu_mmio.h"
#include "edu_ioctl.h"

#define DEVICE_NAME "edu-fact"

static dev_t dev_base;
static struct class *edu_class;
static DEFINE_IDA(edu_minor_ida);
static struct dentry *edu_debugfs_root;

/*
 * Ouverture du device (/dev/edu-factX)
 * On associe le fichier ouvert à notre structure edu_dev.
 */
static int edu_open(struct inode *inode, struct file *file)
{
	unsigned int major_number = imajor(inode);
	unsigned int minor_number = iminor(inode);
	struct edu_dev *edu;

	edu = container_of(inode->i_cdev, struct edu_dev, cdev);
	if (inode->i_cdev != &edu->cdev) {
		pr_warn("edu_open: internal error (major=%d minor=%d)\n",
			major_number, minor_number);
		return -ENODEV;
	}

	file->private_data = edu;
	pr_info("edu-fact%d opened\n", minor_number);
	return 0;
}

/*
 * Écriture : on reçoit un nombre depuis l'utilisateur,
 * on le donne au device pour calculer la factorielle,
 * et on attend que l'interruption nous dise que c'est terminé.
 */
static ssize_t edu_write(struct file *file, const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	struct edu_dev *edu = file->private_data;
	char kernel_buf[32];
	long factorial_input;
	unsigned long reg_lock_flags;
	ssize_t write_result;

	if (count >= sizeof(kernel_buf))
		return -EINVAL;

	if (copy_from_user(kernel_buf, user_buf, count))
		return -EFAULT;

	kernel_buf[count] = '\0';
	if (kstrtol(kernel_buf, 10, &factorial_input))
		return -EINVAL;

	/*
	 * Le device EDU ignore silencieusement une écriture dans
	 * REG_FACTORIAL si un calcul est déjà en cours (EDU_STATUS_COMPUTING).
	 * io_lock garantit qu'un seul calcul est en vol à la fois pour ce
	 * device : sans lui, deux write() concurrents pourraient laisser
	 * l'un des deux threads attendre une IRQ qui ne viendra jamais.
	 */
	if (mutex_lock_interruptible(&edu->io_lock))
		return -ERESTARTSYS;

	spin_lock_irqsave(&edu->reg_lock, reg_lock_flags);
	edu->irq_done = 0;
	spin_unlock_irqrestore(&edu->reg_lock, reg_lock_flags);

	edu_mmio_write(edu->mmio_base, (uint32_t)factorial_input);

	if (wait_event_interruptible(edu->wq, edu->irq_done)) {
		write_result = -ERESTARTSYS;
		goto out_unlock;
	}

	edu->last_result = edu_mmio_read_result(edu->mmio_base);
	write_result = count;

out_unlock:
	mutex_unlock(&edu->io_lock);
	return write_result;
}

/*
 * Lecture : on renvoie le dernier résultat calculé à l'utilisateur.
 */
static ssize_t edu_read(struct file *file, char __user *user_buf, size_t count,
			loff_t *ppos)
{
	struct edu_dev *edu = file->private_data;
	char kernel_buf[32];
	int reply_len;

	if (*ppos > 0)
		return 0;

	/*
	 * Même verrou que edu_write : on ne veut pas lire last_result
	 * pendant qu'un calcul concurrent est en train de le mettre à jour.
	 * Si un write() est en cours, ce read() attend simplement qu'il se
	 * termine avant de renvoyer le résultat.
	 */
	if (mutex_lock_interruptible(&edu->io_lock))
		return -ERESTARTSYS;

	reply_len = snprintf(kernel_buf, sizeof(kernel_buf), "%u\n",
			      edu->last_result);

	mutex_unlock(&edu->io_lock);

	if (copy_to_user(user_buf, kernel_buf, reply_len))
		return -EFAULT;

	*ppos = reply_len;
	return reply_len;
}

/*
 * Lance un transfert DMA et attend sa fin (via IRQ). to_ram indique le sens :
 * true = EDU->RAM, false = RAM->EDU. Doit être appelée avec io_lock tenu.
 */
static int edu_dma_transfer(struct edu_dev *edu, dma_addr_t src_addr,
			    dma_addr_t dst_addr, uint32_t byte_count,
			    bool to_ram)
{
	unsigned long reg_lock_flags;

	spin_lock_irqsave(&edu->reg_lock, reg_lock_flags);
	edu->dma_done = 0;
	spin_unlock_irqrestore(&edu->reg_lock, reg_lock_flags);

	edu_mmio_dma_start(edu->mmio_base, src_addr, dst_addr, byte_count,
			   to_ram);

	if (wait_event_interruptible(edu->dma_wq, edu->dma_done))
		return -ERESTARTSYS;

	return 0;
}

/*
 * ioctl EDU_IOC_DMA_ROUNDTRIP : copie "len" octets fournis par l'appelant
 * dans le buffer DMA cohérent, les envoie au device (RAM->EDU), puis les
 * relit immédiatement (EDU->RAM) et les renvoie à l'appelant. Le driver ne
 * juge pas si le round-trip est correct : c'est à l'appelant de comparer
 * ce qu'il a envoyé et ce qu'il a reçu.
 */
static long edu_dma_roundtrip(struct edu_dev *edu,
			      struct edu_dma_req __user *req)
{
	uint32_t transfer_len;
	int dma_ret;

	if (get_user(transfer_len, &req->len))
		return -EFAULT;

	if (transfer_len == 0 || transfer_len > EDU_DMA_BUF_SIZE)
		return -EINVAL;

	if (mutex_lock_interruptible(&edu->io_lock))
		return -ERESTARTSYS;

	/* dma_buf is shared: copy under lock to prevent concurrent ioctl races */
	if (copy_from_user(edu->dma_buf, req->data, transfer_len)) {
		mutex_unlock(&edu->io_lock);
		return -EFAULT;
	}

	dma_ret = edu_dma_transfer(edu, edu->dma_handle, EDU_DMA_BUF_OFFSET,
				   transfer_len, false);
	if (dma_ret)
		goto out_unlock;

	dma_ret = edu_dma_transfer(edu, EDU_DMA_BUF_OFFSET, edu->dma_handle,
				   transfer_len, true);
	if (!dma_ret) {
		edu->last_dma_len = transfer_len;
		if (copy_to_user(req->data, edu->dma_buf, transfer_len))
			dma_ret = -EFAULT;
	}

out_unlock:
	mutex_unlock(&edu->io_lock);
	return dma_ret;
}

static long edu_ioctl(struct file *file, unsigned int ioctl_cmd,
		      unsigned long ioctl_arg)
{
	struct edu_dev *edu = file->private_data;

	switch (ioctl_cmd) {
	case EDU_IOC_DMA_ROUNDTRIP:
		return edu_dma_roundtrip(edu,
					 (struct edu_dma_req __user *)ioctl_arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations edu_fops = {
	.owner = THIS_MODULE,
	.open = edu_open,
	.read = edu_read,
	.write = edu_write,
	.unlocked_ioctl = edu_ioctl,
};

/*
 * Attributs sysfs en lecture seule, exposés sous
 * /sys/class/edu-fact/edu-factX/{irq_count,dma_count}.
 * Interface stable et destinée à durer : contrairement à debugfs (plus
 * bas), on n'y met que ce qu'on est prêt à maintenir comme une petite ABI.
 */
static ssize_t irq_count_show(struct device *dev, struct device_attribute *attr,
			      char *sysfs_buf)
{
	struct edu_dev *edu = dev_get_drvdata(dev);

	return sysfs_emit(sysfs_buf, "%d\n", atomic_read(&edu->irq_count));
}
static DEVICE_ATTR_RO(irq_count);

static ssize_t dma_count_show(struct device *dev, struct device_attribute *attr,
			      char *sysfs_buf)
{
	struct edu_dev *edu = dev_get_drvdata(dev);

	return sysfs_emit(sysfs_buf, "%d\n", atomic_read(&edu->dma_count));
}
static DEVICE_ATTR_RO(dma_count);

static struct attribute *edu_attrs[] = {
	&dev_attr_irq_count.attr,
	&dev_attr_dma_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edu);

/*
 * Initialisation du device caractère pour un device PCI détecté.
 * On crée le nœud /dev/edu-factX
 */
int edu_char_init(struct edu_dev *edu)
{
	int init_ret;
	int minor_number;
	struct device *created_dev;
	char debugfs_name[16];

	minor_number = ida_alloc_max(&edu_minor_ida, 255, GFP_KERNEL);
	if (minor_number < 0)
		return minor_number;
	edu->dev_num = MKDEV(MAJOR(dev_base), minor_number);

	cdev_init(&edu->cdev, &edu_fops);
	edu->cdev.owner = THIS_MODULE;

	init_ret = cdev_add(&edu->cdev, edu->dev_num, 1);
	if (init_ret) {
		pr_warn("edu_char_init: cdev_add() failed\n");
		goto err_ida;
	}

	created_dev = device_create_with_groups(edu_class, &edu->pdev->dev,
						edu->dev_num, edu, edu_groups,
						DEVICE_NAME "%d", minor_number);
	if (IS_ERR(created_dev)) {
		init_ret = PTR_ERR(created_dev);
		pr_warn("edu_char_init: device_create_with_groups() failed\n");
		goto err_cdev;
	}

	/*
	 * debugfs est un outil de debug, pas une ABI : contrairement à
	 * sysfs ci-dessus, on peut y mettre de l'état interne sans engager
	 * de compatibilité, et son absence (CONFIG_DEBUGFS=n) n'est jamais
	 * une erreur fatale pour le driver.
	 */
	snprintf(debugfs_name, sizeof(debugfs_name), "%d", minor_number);
	edu->debugfs_dir = debugfs_create_dir(debugfs_name, edu_debugfs_root);
	debugfs_create_atomic_t("irq_count", 0444, edu->debugfs_dir,
				&edu->irq_count);
	debugfs_create_atomic_t("dma_count", 0444, edu->debugfs_dir,
				&edu->dma_count);
	debugfs_create_u32("last_dma_len", 0444, edu->debugfs_dir,
			   &edu->last_dma_len);

	return 0;

err_cdev:
	cdev_del(&edu->cdev);
err_ida:
	ida_free(&edu_minor_ida, minor_number);
	return init_ret;
}

void edu_char_cleanup(struct edu_dev *edu)
{
	debugfs_remove_recursive(edu->debugfs_dir);
	device_destroy(edu_class, edu->dev_num);
	cdev_del(&edu->cdev);
	ida_free(&edu_minor_ida, MINOR(edu->dev_num));
}

/*
 * Initialisation globale (appelée une seule fois au chargement du module)
 * On réserve un major number et on crée la classe /sys/class/edu-fact
 */
int edu_char_global_init(void)
{
	int init_ret;

	init_ret = alloc_chrdev_region(&dev_base, 0, 256, DEVICE_NAME);
	if (init_ret) {
		pr_warn("edu_char_global_init: alloc_chrdev_region() failed\n");
		return init_ret;
	}

	edu_class = class_create(DEVICE_NAME);
	if (IS_ERR(edu_class)) {
		pr_warn("edu_char_global_init: class_create() failed\n");
		unregister_chrdev_region(dev_base, 256);
		return PTR_ERR(edu_class);
	}

	/*
	 * Pas de vérification d'erreur ici : debugfs_create_dir() renvoie un
	 * ERR_PTR() si debugfs est absent (CONFIG_DEBUGFS=n) ou en cas
	 * d'échec, et il est explicitement sûr de passer ce pointeur à tous
	 * les appels debugfs_create_*() suivants (ils deviennent des no-op).
	 * C'est un outil de debug : son indisponibilité ne doit jamais faire
	 * échouer le chargement du module.
	 */
	edu_debugfs_root = debugfs_create_dir(DEVICE_NAME, NULL);

	return 0;
}

void edu_char_global_exit(void)
{
	debugfs_remove_recursive(edu_debugfs_root);
	class_destroy(edu_class);
	unregister_chrdev_region(dev_base, 256);
	ida_destroy(&edu_minor_ida);
}