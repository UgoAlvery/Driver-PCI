#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/device.h>

#include "edu_char.h"
#include "edu_mmio.h"

#define DEVICE_NAME "edu-fact"

static dev_t dev_num;
static struct cdev edu_cdev;
static struct class *edu_class;
static uint32_t last_result;

/* write */
static ssize_t edu_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
    char kbuf[32];
    long val;

    if (count >= sizeof(kbuf))
        return -EINVAL;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';

    if (kstrtol(kbuf, 10, &val))
        return -EINVAL;

    edu_mmio_write(val);
    last_result = edu_mmio_read_result();

    return count;
}

/* read */
static ssize_t edu_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    char kbuf[32];
    int len;

    if (*ppos > 0)
        return 0;

    len = snprintf(kbuf, sizeof(kbuf), "%u\n", last_result);

    if (copy_to_user(buf, kbuf, len))
        return -EFAULT;

    *ppos = len;
    return len;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .read = edu_read,
    .write = edu_write,
};

int edu_char_init(void)
{
    int ret;

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&edu_cdev, &fops);

    ret = cdev_add(&edu_cdev, dev_num, 1);
    if (ret)
        goto err_unregister;

    edu_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(edu_class)) {
        ret = PTR_ERR(edu_class);
        goto err_cdev;
    }

    device_create(edu_class, NULL, dev_num, NULL, "edu-fact0");

    return 0;

err_cdev:
    cdev_del(&edu_cdev);
err_unregister:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

void edu_char_cleanup(void)
{
    device_destroy(edu_class, dev_num);
    class_destroy(edu_class);
    cdev_del(&edu_cdev);
    unregister_chrdev_region(dev_num, 1);
}