#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include "edu_dev.h"
#include "edu_pci.h"
#include "edu_mmio.h"
#include "edu_char.h"

#define DEVICE_NAME "edu-fact"
#define EDU_VENDOR_ID 0x1234
#define EDU_DEVICE_ID 0x11e8

/*
 * Gestionnaire d'interruption : appelé quand le device a fini un calcul de
 * factorielle OU un transfert DMA (les deux mécanismes partagent la même
 * ligne d'IRQ physique, distingués par le bit levé dans REG_INTR_STATUS).
 *
 * IRQF_SHARED oblige à vérifier que l'interruption vient bien de NOTRE
 * device avant de la traiter : REG_INTR_STATUS peut être à 0 si la ligne
 * partagée a été levée par un autre périphérique.
 *
 * On doit aussi acquitter (edu_mmio_ack_irq) avant de sortir : sans ça,
 * la ligne INTx reste asserted et le kernel rappelle le handler en boucle
 * (storm d'IRQ).
 */
static irqreturn_t edu_irq_handler(int irq, void *data)
{
	struct edu_dev *edu = data;
	uint32_t status;
	unsigned long flags;

	status = edu_mmio_ack_irq(edu->mmio_base);
	if (!(status & (FACT_IRQ | DMA_IRQ)))
		return IRQ_NONE;

	spin_lock_irqsave(&edu->reg_lock, flags);
	if (status & FACT_IRQ)
		edu->irq_done = 1;
	if (status & DMA_IRQ)
		edu->dma_done = 1;
	spin_unlock_irqrestore(&edu->reg_lock, flags);

	if (status & FACT_IRQ) {
		atomic_inc(&edu->irq_count);
		wake_up_interruptible(&edu->wq);
	}
	if (status & DMA_IRQ) {
		atomic_inc(&edu->dma_count);
		wake_up_interruptible(&edu->dma_wq);
	}

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
	init_waitqueue_head(&edu->dma_wq);
	mutex_init(&edu->io_lock);
	spin_lock_init(&edu->reg_lock);
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

	/*
	 * Le device EDU ne supporte que 28 bits d'adresse DMA par défaut
	 * (256 Mio, cf. docs/specs/edu.rst). dma_alloc_coherent() ci-dessous
	 * doit rester dans ce domaine adressable, d'où ce masque explicite.
	 */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(&pdev->dev, "dma_set_mask_and_coherent() failed\n");
		goto err_release;
	}

	edu->mmio_base = pci_iomap(pdev, 0, 0);
	if (!edu->mmio_base) {
		dev_err(&pdev->dev, "pci_iomap() failed\n");
		ret = -ENOMEM;
		goto err_release;
	}

	/*
	 * Buffer cohérent servant de "côté RAM" à chaque round-trip DMA.
	 * Alloué une seule fois ici et réutilisé pour tous les transferts :
	 * pas besoin de (dés)allouer à chaque ioctl.
	 */
	edu->dma_buf = dma_alloc_coherent(&pdev->dev, EDU_DMA_BUF_SIZE,
					  &edu->dma_handle, GFP_KERNEL);
	if (!edu->dma_buf) {
		dev_err(&pdev->dev, "dma_alloc_coherent() failed\n");
		ret = -ENOMEM;
		goto err_iounmap;
	}

	ret = request_irq(pdev->irq, edu_irq_handler, IRQF_SHARED, DEVICE_NAME,
			  edu);
	if (ret) {
		dev_err(&pdev->dev, "request_irq() failed\n");
		goto err_dma_free;
	}

	/*
	 * A faire seulement une fois le handler enregistré : dès que ce bit
	 * est posé, le device peut lever une IRQ à tout moment après le
	 * prochain calcul, donc le handler doit déjà être prêt à la recevoir.
	 */
	edu_mmio_enable_irq(edu->mmio_base);

	ret = edu_char_init(edu);
	if (ret) {
		dev_err(&pdev->dev, "edu_char_init() failed\n");
		goto err_irq;
	}

	dev_info(&pdev->dev, "edu driver loaded\n");
	return 0;

err_irq:
	free_irq(pdev->irq, edu);
err_dma_free:
	dma_free_coherent(&pdev->dev, EDU_DMA_BUF_SIZE, edu->dma_buf,
			  edu->dma_handle);
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
	struct edu_dev *edu = pci_get_drvdata(pdev);

	edu_char_cleanup(edu);
	free_irq(pdev->irq, edu);
	mutex_destroy(&edu->io_lock);
	dma_free_coherent(&pdev->dev, EDU_DMA_BUF_SIZE, edu->dma_buf,
			  edu->dma_handle);
	pci_iounmap(pdev, edu->mmio_base);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(edu);

	dev_info(&pdev->dev, "edu driver removed\n");
}

/* Table des devices PCI supportés par ce driver */
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