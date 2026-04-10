#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/slab.h>

#include "edu_char.h"
#include "edu_mmio.h"

#define DEVICE_NAME "edu-fact"

static dev_t          dev_base;
static struct class  *edu_class;
static int            next_minor;

/*
 * Ouverture du device (/dev/edu-factX)
 * On associe le fichier ouvert à notre structure edu_dev.
 */
static int edu_open(struct inode *inode, struct file *file)
{
    unsigned int mj = imajor(inode);
    unsigned int mn = iminor(inode);
    struct edu_dev *edu;

    edu = container_of(inode->i_cdev, struct edu_dev, cdev);
    if (inode->i_cdev != &edu->cdev) {
        pr_warn("edu_open: internal error (major=%d minor=%d)\n", mj, mn);
        return -ENODEV;
    }

    file->private_data = edu;
    pr_info("edu-fact%d opened\n", mn);
    return 0;
}

/*
 * Écriture : on reçoit un nombre depuis l'utilisateur,
 * on le donne au device pour calculer la factorielle,
 * et on attend que l'interruption nous dise que c'est terminé.
 */
static ssize_t edu_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct edu_dev *edu = file->private_data;
	char kbuf[32];
	long val;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';
	if (kstrtol(kbuf, 10, &val))
		return -EINVAL;

	edu->irq_done = 0;
	edu_mmio_write(edu->mmio_base, (uint32_t)val);

	if (wait_event_interruptible(edu->wq, edu->irq_done))
		return -ERESTARTSYS;

	edu->last_result = edu_mmio_read_result(edu->mmio_base);

	return count;
}

/*
 * Lecture : on renvoie le dernier résultat calculé à l'utilisateur.
 */
static ssize_t edu_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct edu_dev *edu = file->private_data;
	char kbuf[32];
	int len;

	if (*ppos > 0)
		return 0;

	len = snprintf(kbuf, sizeof(kbuf), "%u\n", edu->last_result);

	if (copy_to_user(buf, kbuf, len))
		return -EFAULT;

	*ppos = len;
	return len;
}

static const struct file_operations edu_fops = {
	.owner   = THIS_MODULE,
	.open    = edu_open,
	.read    = edu_read,
	.write   = edu_write,
};

/*
 * Initialisation du device caractère pour un device PCI détecté.
 * On crée le nœud /dev/edu-factX
 */
int edu_char_init(struct edu_dev *edu)
{
	int ret;
	int minor;
	struct device *dev;

	/* Assign next available minor number */
	minor = next_minor++;
	edu->dev_num = MKDEV(MAJOR(dev_base), minor);

	cdev_init(&edu->cdev, &edu_fops);
	edu->cdev.owner = THIS_MODULE;

	ret = cdev_add(&edu->cdev, edu->dev_num, 1);
	if (ret) {
		pr_warn("edu_char_init: cdev_add() failed\n");
		return ret;
	}

	dev = device_create(edu_class, &edu->pdev->dev,
			    edu->dev_num, edu, DEVICE_NAME "%d", minor);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		pr_warn("edu_char_init: device_create() failed\n");
		goto err_cdev;
	}

	return 0;

err_cdev:
	cdev_del(&edu->cdev);
	return ret;
}

void edu_char_cleanup(struct edu_dev *edu)
{
	device_destroy(edu_class, edu->dev_num);
	cdev_del(&edu->cdev);
}

/*
 * Initialisation globale (appelée une seule fois au chargement du module)
 * On réserve un major number et on crée la classe /sys/class/edu-fact
 */
int edu_char_global_init(void)
{
	int ret;

	next_minor = 0;

	ret = alloc_chrdev_region(&dev_base, 0, 256, DEVICE_NAME);
	if (ret) {
		pr_warn("edu_char_global_init: alloc_chrdev_region() failed\n");
		return ret;
	}

	edu_class = class_create(DEVICE_NAME);
	if (IS_ERR(edu_class)) {
		pr_warn("edu_char_global_init: class_create() failed\n");
		unregister_chrdev_region(dev_base, 256);
		return PTR_ERR(edu_class);
	}

	return 0;
}

void edu_char_global_exit(void)
{
	class_destroy(edu_class);
	unregister_chrdev_region(dev_base, 256);
}