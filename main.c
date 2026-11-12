#include <linux/module.h>

#include "edu_pci.h"
#include "edu_char.h"

/*
 * Fonction appelée au chargement du module.
 * On initialise d'abord la partie caractère, puis la partie PCI.
 */
static int __init edu_init(void)
{
	int init_ret;

	init_ret = edu_char_global_init();
	if (init_ret)
		return init_ret;

	init_ret = edu_pci_init();
	if (init_ret) {
		edu_char_global_exit();
		return init_ret;
	}

	return 0;
}

/*
 * Fonction appelée au déchargement du module.
 * On fait le nettoyage dans l'ordre inverse.
 */
static void __exit edu_exit(void)
{
	edu_pci_exit();
	edu_char_global_exit();
}

module_init(edu_init);
module_exit(edu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Educational PCI factorial driver");
MODULE_AUTHOR("UGO et ARTHUR");