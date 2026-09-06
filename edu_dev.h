#ifndef EDU_DEV_H
#define EDU_DEV_H

#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/atomic.h>

struct edu_dev {
	struct pci_dev *pdev;
	void __iomem *mmio_base;
	struct cdev cdev;
	dev_t dev_num;
	uint32_t last_result;
	int irq_done;
	wait_queue_head_t wq;

	/*
	 * io_lock serialise l'intégralité de la séquence write->wait->read
	 * pour ce device : le matériel ignore silencieusement une nouvelle
	 * commande de factorielle si un calcul est déjà en cours, donc deux
	 * write() concurrents non sérialisés peuvent laisser l'un des deux
	 * threads attendre une IRQ qui ne viendra jamais.
	 *
	 * Ce même verrou sérialise aussi les transferts DMA (cf. plus bas) :
	 * factorielle et DMA partagent le même bloc de registres MMIO, donc
	 * il n'y a aucun bénéfice réel à les paralléliser, et un seul verrou
	 * évite des bugs d'entrelacement entre les deux mécanismes.
	 */
	struct mutex io_lock;

	/*
	 * reg_lock protège irq_done et dma_done contre la course entre le
	 * contexte process (qui les remet à 0 puis les lit) et le contexte
	 * IRQ (le handler, qui les positionne à 1). io_lock ne suffit pas
	 * ici car l'ISR n'est jamais soumise au mutex.
	 */
	spinlock_t reg_lock;

	/* DMA : buffer cohérent servant de "côté RAM" à chaque transfert.
	 * Alloué une fois au probe, réutilisé pour tous les round-trips.
	 */
	void *dma_buf;
	dma_addr_t dma_handle;
	int dma_done;
	wait_queue_head_t dma_wq;
	uint32_t last_dma_len; /* debug uniquement : taille du dernier round-trip */

	/*
	 * Compteurs d'introspection, incrémentés depuis l'ISR (atomic_t, pas
	 * besoin de reg_lock). kzalloc() les initialise déjà à 0, donc pas
	 * d'atomic_set() explicite au probe.
	 */
	atomic_t irq_count; /* factorielles terminées (FACT_IRQ reçue) */
	atomic_t dma_count; /* transferts DMA élémentaires terminés (DMA_IRQ) */

	struct dentry *debugfs_dir;
};

#endif