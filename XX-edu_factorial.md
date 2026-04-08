# Projet: EDU factorial

## Objectifs

L'objectif est la création d'un driver PCI en mode caractère fonctionnel pour le calcul de factorielle du périphérique EDU fournit par QEMU: https://www.qemu.org/docs/master/specs/edu.html

Seul la fonctionnalité de calcul de factorielle est requis pour ce projet. Les autres fonctions du driver (DMA et operateur d'inversion) ne sont pas attendues.

Le fonctionnement minimal attendu du driver est le suivant:

    Compilation OK
    Chargement du module OK
    Création d'un device node (/dev/edu-fact0)
    Ecriture d'un nombre dans le device créé (echo 8 > /dev/edu-fact0)
    Lecture du résultat (cat /dev/edu-fact0 -> 40320)
    Déchargement du module

Un fonctionnement complet nécessite en plus:

    La gestion de plusieurs périphériques
    L'utilisation de l'IRQ pour être averti de la fin du calcul

La notation prendra en compte:

    Le fonctionnement du driver (minimal ou complet)
    La qualité du code (lisibilité, gestion d'erreurs, commentaires)
    Les erreurs/warning de compilation
    Le respect du formatage de code

## Instructions

Le périphérique doit être ajouté à QEMU au moment du démarrage de ce dernier. Pour cela il faut ajouter l'option "-device edu" au script de lancement que je vous ai fournit. Vous pouvez vérifier sa présence avec la commande "lspci", disponible dans le paquet pciutils (sudo apk add pciutils).

Quelques conseils:

    Revoir les cours/TD sur les périphériques PCI, l'accès au matériel et les drivers en mode caractère
    Respecter le formatage du code source "officiel": https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/.clang-format?h=v6.18.16
    Commenter votre code, pas à outrance mais les grandes étapes du driver
    Gérer correctement les cas d'erreurs (désallocation des ressources, ...)
    Je sais aussi me servir de google et de chatgpt, donc faîtes le développement et ne recopier pas bêtement ce que vous allez trouver (je le verrais sûrement)
    Le travail est personnel, vous pouvez vous aider, mais ne recopier pas le travail d'un autre étudiant/groupe

Quelques liens utiles:

    https://www.qemu.org/docs/master/specs/edu.html: spécification du fonctionnement du composant
    https://docs.kernel.org: la doc officielle du kernel
    https://elixir.bootlin.com/linux/v6.12.6/source: pour parcourir le code source du kernel mainline et avoir des exemples de bonne utilisation
    https://lwn.net/Kernel/LDD3: un bon livre (un peu ancien) sur le développement de drivers kernel
