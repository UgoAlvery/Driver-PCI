#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include "edu_dev.h"
#include "edu_pci.h"
#include "edu_mmio.h"
#include "edu_char.h"

#define DEVICE_NAME "edu-fact"
#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x11e8

/**
 * edu_irq_handler() - interrupt handler for factorial completion
 * @irq:  IRQ number
 * @data: pointer to our per-device struct edu_dev
 *
 * The EDU device sets bit STATUS_IRQ_REQ (0x80) in REG_STATUS when
 * the factorial computation is done. We acknowledge by clearing that bit.
 */
static irqreturn_t edu_irq_handler(int irq, void *data)
{
	struct edu_dev *edu = data;

	/* Si on n'attend pas de résultat, ce n'est pas notre IRQ */
	if (atomic_read(&edu->irq_done))
		return IRQ_NONE;

	atomic_set(&edu->irq_done, 1);
	wake_up_interruptible(&edu->wq);

	return IRQ_HANDLED;
}

static int edu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct edu_dev *edu;
	int ret;

	/* Allocate per-device context */
	edu = devm_kzalloc(&pdev->dev, sizeof(*edu), GFP_KERNEL);
	if (!edu)
		return -ENOMEM;

	edu->pdev = pdev;
	init_waitqueue_head(&edu->wq);
	pci_set_drvdata(pdev, edu);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device() failed\n");
		return ret;
	}

	ret = pci_request_region(pdev, 0, DEVICE_NAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_region() failed\n");
		goto err_disable;
	}

	/* Enable bus mastering so the device can raise interrupts */
	pci_set_master(pdev);

	edu->mmio_base = pci_iomap(pdev, 0, 0);
	if (!edu->mmio_base) {
		dev_err(&pdev->dev, "pci_iomap() failed\n");
		ret = -ENOMEM;
		goto err_release;
	}

	/* Register IRQ handler */
	ret = request_irq(pdev->irq, edu_irq_handler, IRQF_SHARED, DEVICE_NAME,
			  edu);
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
	return ret;
}

static void edu_remove(struct pci_dev *pdev)
{
	/* Retrieve the per-device context saved in probe */
	struct edu_dev *edu = pci_get_drvdata(pdev);

	edu_char_cleanup(edu);
	free_irq(pdev->irq, edu);
	pci_iounmap(pdev, edu->mmio_base);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);

	dev_info(&pdev->dev, "edu driver removed\n");
}

static const struct pci_device_id edu_ids[] = { { PCI_DEVICE(EDU_VENDOR_ID,
							     EDU_DEVICE_ID) },
						{ 0 } };
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