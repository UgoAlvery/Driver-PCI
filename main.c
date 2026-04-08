#include <linux/module.h>
#include "edu_pci.h"

static int __init edu_init(void)
{
    return edu_pci_init();
}

static void __exit edu_exit(void)
{
    edu_pci_exit();
}

module_init(edu_init);
module_exit(edu_exit);

MODULE_LICENSE("GPL");