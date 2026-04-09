#include <linux/module.h>

#include "edu_pci.h"
#include "edu_char.h"

static int __init edu_init(void)
{
	int ret;

	/* Allocate major number and device class once for all instances */
	ret = edu_char_global_init();
	if (ret)
		return ret;

	ret = edu_pci_init();
	if (ret) {
		edu_char_global_exit();
		return ret;
	}

	return 0;
}

static void __exit edu_exit(void)
{
	edu_pci_exit();
	edu_char_global_exit();
}

module_init(edu_init);
module_exit(edu_exit);

MODULE_LICENSE("GPL");
