#include <linux/module.h>
#include <linux/pci.h>

#include "edu_pci.h"
#include "edu_mmio.h"
#include "edu_char.h"

#define DEVICE_NAME "edu-fact"
#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x11e8

static struct pci_dev *edu_pdev;

static int edu_probe(struct pci_dev *pdev,
                     const struct pci_device_id *id)
{
    int ret;
    void __iomem *base;

    edu_pdev = pdev;

    ret = pci_enable_device(pdev);
    if (ret)
        return ret;

    ret = pci_request_region(pdev, 0, DEVICE_NAME);
    if (ret)
        goto err_disable;

    base = pci_iomap(pdev, 0, 0);
    if (!base) {
        ret = -ENOMEM;
        goto err_release;
    }

    edu_mmio_set_base(base);

    ret = edu_char_init();
    if (ret)
        goto err_iounmap;

    pr_info("edu driver loaded\n");
    return 0;

err_iounmap:
    pci_iounmap(pdev, base);
err_release:
    pci_release_region(pdev, 0);
err_disable:
    pci_disable_device(pdev);
    return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
    edu_char_cleanup();

    pci_iounmap(pdev, pci_iomap(pdev, 0, 0));
    pci_release_region(pdev, 0);
    pci_disable_device(pdev);

    pr_info("edu driver removed\n");
}

static const struct pci_device_id edu_ids[] = {
    { PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
    { 0 }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
    .name = DEVICE_NAME,
    .id_table = edu_ids,
    .probe = edu_probe,
    .remove = edu_remove,
};

int edu_pci_init(void)
{
    return pci_register_driver(&edu_driver);
}

void edu_pci_exit(void)
{
    pci_unregister_driver(&edu_driver);
}