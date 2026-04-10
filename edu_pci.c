#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "edu_dev.h"
#include "edu_pci.h"
#include "edu_mmio.h"
#include "edu_char.h"

#define DEVICE_NAME    "edu-fact"
#define EDU_VENDOR_ID  0x1234
#define EDU_DEVICE_ID  0x11e8

/* 
 * Gestionnaire d'interruption : appelé quand le device a fini son calcul.
 */
static irqreturn_t edu_irq_handler(int irq, void *data)
{
	struct edu_dev *edu = data;

	if (edu->irq_done)
		return IRQ_NONE;

	edu->irq_done = 1;
	wake_up_interruptible(&edu->wq);

	return IRQ_HANDLED;
}

/*
 * Fonction appelée quand le device PCI est détecté.
 * On initialise tout ce qui concerne le matériel.
 */
static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct edu_dev *edu;
	int ret;

	edu = kzalloc(sizeof(*edu), GFP_KERNEL);
	if (!edu)
		return -ENOMEM;

	edu->pdev = pdev;
	init_waitqueue_head(&edu->wq);
	pci_set_drvdata(pdev, edu);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device() failed\n");
		goto err_kfree;
	}

	ret = pci_request_region(pdev, 0, DEVICE_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_region() failed\n");
		goto err_disable;
	}

	pci_set_master(pdev);

	edu->mmio_base = pci_iomap(pdev, 0, 0);
	if (!edu->mmio_base) {
		dev_err(&pdev->dev, "pci_iomap() failed\n");
		ret = -ENOMEM;
		goto err_release;
	}

	ret = request_irq(pdev->irq, edu_irq_handler, IRQF_SHARED,
			  DEVICE_NAME, edu);
	if (ret) {
		dev_err(&pdev->dev, "request_irq() failed\n");
		goto err_iounmap;
	}

	ret = edu_char_init(edu);
	if (ret) {
		dev_err(&pdev->dev, "edu_char_init() failed\n");
		goto err_irq;
	}

	dev_info(&pdev->dev, "edu driver loaded\n");
	return 0;

err_irq:
	free_irq(pdev->irq, edu);
err_iounmap:
	pci_iounmap(pdev, edu->mmio_base);
err_release:
	pci_release_region(pdev, 0);
err_disable:
	pci_disable_device(pdev);
err_kfree:
	kfree(edu);
	return ret;
}

/*
 * Fonction appelée quand on retire le device (ou qu'on décharge le module).
 * On nettoie tout proprement.
 */
static void edu_remove(struct pci_dev *pdev)
{
	/* Retrieve the per-device context saved in probe */
	struct edu_dev *edu = pci_get_drvdata(pdev);

	edu_char_cleanup(edu);
	free_irq(pdev->irq, edu);
	pci_iounmap(pdev, edu->mmio_base);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(edu);

	dev_info(&pdev->dev, "edu driver removed\n");
}

/* Table des devices PCI supportés par ce driver */
static const struct pci_device_id edu_ids[] = {
	{ PCI_DEVICE(EDU_VENDOR_ID, EDU_DEVICE_ID) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, edu_ids);

static struct pci_driver edu_driver = {
	.name     = DEVICE_NAME,
	.id_table = edu_ids,
	.probe    = edu_probe,
	.remove   = edu_remove,
};

/* Initialisation du driver PCI */
int edu_pci_init(void)
{
	return pci_register_driver(&edu_driver);
}

/* Nettoyage du driver PCI */
void edu_pci_exit(void)
{
	pci_unregister_driver(&edu_driver);
}