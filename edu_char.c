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
static DEFINE_IDA(edu_minor_ida);

static int edu_open(struct inode *inode, struct file *file)
{
	struct edu_dev *edu = container_of(inode->i_cdev,
					   struct edu_dev, cdev);
	file->private_data = edu;
	return 0;
}

/* write: receive a number, trigger the factorial computation */
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

	/* Submit value and wait for IRQ completion */
	edu->irq_done = false;
	edu_mmio_write(edu->mmio_base, (uint32_t)val);

	if (wait_event_interruptible(edu->wq, edu->irq_done))
		return -ERESTARTSYS;

	edu->last_result = edu_mmio_read_result(edu->mmio_base);

	return count;
}

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

int edu_char_init(struct edu_dev *edu)
{
	int ret;
	int minor;
	struct device *dev;

	/* Allocate a unique minor for this device instance */
	minor = ida_alloc(&edu_minor_ida, GFP_KERNEL);
	if (minor < 0)
		return minor;

	edu->dev_num = MKDEV(MAJOR(dev_base), minor);

	cdev_init(&edu->cdev, &edu_fops);
	edu->cdev.owner = THIS_MODULE;

	ret = cdev_add(&edu->cdev, edu->dev_num, 1);
	if (ret)
		goto err_ida;

	dev = device_create(edu_class, &edu->pdev->dev,
			    edu->dev_num, edu, DEVICE_NAME "%d", minor);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto err_cdev;
	}

	return 0;

err_cdev:
	cdev_del(&edu->cdev);
err_ida:
	ida_free(&edu_minor_ida, minor);
	return ret;
}

void edu_char_cleanup(struct edu_dev *edu)
{
	int minor = MINOR(edu->dev_num);

	device_destroy(edu_class, edu->dev_num);
	cdev_del(&edu->cdev);
	ida_free(&edu_minor_ida, minor);
}

int edu_char_global_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_base, 0, 256, DEVICE_NAME);
	if (ret)
		return ret;

	edu_class = class_create(DEVICE_NAME);
	if (IS_ERR(edu_class)) {
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