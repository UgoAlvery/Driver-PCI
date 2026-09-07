# DESIGN.md

Ce document explique les choix techniques du driver et les alternatives
écartées. Objectif : pouvoir justifier n'importe quelle ligne de code en
entretien, pas juste dire "ça marche".

## 1. Bugs trouvés et corrigés

Le code de départ (TP *Driver Linux*, factorielle + IRQ) contenait trois
bugs réels, découverts en comparant le comportement attendu au [code
source de l'émulateur QEMU](https://github.com/qemu/qemu/blob/master/hw/misc/edu.c)
(`hw/misc/edu.c`), pas seulement à la spec :

1. **IRQ jamais activée côté matériel.** Le device EDU n'émet une IRQ à
   la fin d'un calcul que si le bit `STATUS_IRQFACT` (0x80) est posé
   dans `REG_STATUS` (0x20). Le code original n'écrivait jamais dans ce
   registre — il calculait donc la factorielle silencieusement, sans
   jamais prévenir le driver. Un `write()` bloquant sur
   `wait_event_interruptible()` restait donc bloqué indéfiniment.
   Corrigé par `edu_mmio_enable_irq()`, appelée une fois au probe.

2. **IRQ jamais acquittée.** Sans écriture dans `REG_INTR_ACK` (0x64)
   après lecture de `REG_INTR_STATUS` (0x24), la ligne INTx reste
   asserted — storm d'IRQ garanti une fois le bug n°1 corrigé sans
   celui-ci. Corrigé par `edu_mmio_ack_irq()`, appelée en tête de l'ISR.

3. **Race condition matérielle.** Le device ignore silencieusement une
   écriture dans `REG_FACTORIAL` si un calcul est déjà en cours (bit
   `STATUS_COMPUTING`). Deux `write()` concurrents sans sérialisation
   pouvaient donc laisser l'un des deux threads attendre une IRQ qui ne
   viendrait jamais. Corrigé par le mutex `io_lock` (voir §3).

4. **Fuite d'un header kernel-only dans l'API userspace.** `edu_ioctl.h`
   (censé être inclus par du code espace utilisateur, voir
   `tests/dma_roundtrip_test.c`) incluait `edu_mmio.h`, qui inclut
   `<linux/bitops.h>` — indisponible hors noyau. Détecté à la
   compilation du programme de test, pas à la relecture. Corrigé en
   extrayant les constantes réellement partagées (`EDU_DMA_BUF_SIZE`,
   `EDU_DMA_BUF_OFFSET`) dans `edu_common.h`, qui n'inclut aucun header
   kernel-only et peut être inclus des deux côtés de la frontière
   noyau/userspace.

## 2. Modèle de concurrence

Deux verrous, avec des rôles distincts :

- **`struct mutex io_lock`** sérialise toute la séquence
  déclenchement→attente→lecture, pour la factorielle *et* pour le DMA.
  Un seul verrou pour les deux, pas un par mécanisme : ils partagent le
  même bloc de registres MMIO, donc les paralléliser n'apporterait rien
  et risquerait des bugs d'entrelacement (ex. un DMA qui démarre pendant
  qu'un calcul de factorielle est en cours sur le même device). Un
  mutex convient ici car la section critique peut dormir
  (`wait_event_interruptible`).

- **`spinlock_t reg_lock`** protège uniquement `irq_done` et
  `dma_done` contre la course entre le contexte process (qui les remet
  à 0 puis les lit) et le contexte IRQ (qui les positionne à 1).
  `io_lock` ne suffit pas ici : l'ISR ne peut jamais attendre un mutex
  (elle tourne en contexte interruption), donc un spinlock — rapide,
  non-bloquant — est la seule option pour cette section précise.

**Pourquoi pas de bottom half (tasklet/workqueue) ?** Le traitement dans
l'ISR est trivial et borné : deux accès MMIO (ack) et l'écriture d'un
entier sous spinlock, puis un `wake_up_interruptible()`. Aucune
opération lente, aucune allocation, rien qui justifierait de différer le
travail hors du contexte interruption. Découper en top/bottom half
ajouterait de la complexité (et un mécanisme de synchronisation de plus
à documenter) sans bénéfice réel ici. Ce serait un choix différent si
l'ISR devait, par exemple, parser un buffer DMA volumineux ou faire de
l'I/O.

## 3. DMA

- **Registres 32 bits, pas 64.** `REG_DMA_SRC`/`DST` acceptent des accès
  4 ou 8 octets selon la spec EDU. Le driver force
  `dma_set_mask_and_coherent(28 bits)` (limite documentée du device :
  256 Mio adressables), donc toute adresse DMA obtenue tient forcément
  sur 32 bits. Écrire avec `iowrite32` plutôt que `writeq` évite une
  dépendance à un support MMIO 64 bits pas garanti sur toutes les
  architectures, pour un gain nul ici.

- **Un seul buffer cohérent, alloué une fois au probe.** Pas de
  (dés)allocation à chaque appel ioctl : `dma_alloc_coherent()` est
  coûteux et le buffer (4 Kio, taille imposée par le scratch buffer
  interne du device) est de toute façon la limite haute de ce qu'un
  round-trip peut transporter en un seul appel.

- **Exposition via ioctl, pas via read()/write().** Réutiliser
  read/write pour le DMA aurait mélangé deux sémantiques différentes
  (calcul vs transfert brut) sur la même interface. Un ioctl dédié
  (`EDU_IOC_DMA_ROUNDTRIP`) garde chaque mécanisme lisible
  séparément.

- **Le driver ne juge pas le round-trip.** Il exécute le transfert
  aller (RAM→EDU) puis retour (EDU→RAM) et renvoie les données à
  l'appelant ; c'est à l'appelant de comparer ce qu'il a envoyé et ce
  qu'il a reçu (voir `tests/dma_roundtrip_test.c`). Séparation propre
  entre "exécuter un mécanisme" et "décider si le résultat est correct".

## 4. Introspection : sysfs vs debugfs

Les deux compteurs (`irq_count`, `dma_count`) sont exposés aux deux
endroits, avec un rôle différent :

- **sysfs** (`/sys/class/edu-fact/edu-factX/`) : interface qu'on
  s'engage à maintenir. On n'y met que ce qui a vocation à être une
  petite ABI stable.
- **debugfs** : outil de debug, sans garantie de stabilité. On y ajoute
  librement de l'état interne utile au diagnostic (ici, la taille du
  dernier transfert DMA) sans engager de compatibilité.

`debugfs_create_dir(..., NULL)` n'est jamais vérifié pour une erreur :
c'est volontaire et documenté dans le code — l'API kernel garantit qu'un
`ERR_PTR()` passé aux appels `debugfs_create_*()` suivants devient un
no-op silencieux. L'absence de `CONFIG_DEBUGFS` ne doit jamais faire
échouer le chargement du module, puisque ce n'est qu'un outil de debug.

## 5. Tests et CI : pourquoi séparés

`kernel_training.qcow2` (l'image contenant le device EDU) est
propriétaire du cours : impossible de la committer sur GitHub (licence,
taille) ni de la reconstruire à l'identique dans un runner CI. D'où deux
niveaux distincts plutôt qu'une CI qui prétendrait tester le matériel
sans pouvoir le faire :

- **Local** (`tests/run_tests.sh`) : contre le vrai device, dans
  l'environnement de développement.
- **CI** (`.github/workflows/ci.yml`) : ce qui reste vérifiable sans
  device réel — compilation propre (0 warning) contre plusieurs
  versions de kernel réelles (deux runners GitHub différents, chacun
  avec son propre kernel hôte), et conformité au formatage.

## 6. Limitations connues et pistes d'évolution

- Pas de test de charge / stress concurrent (plusieurs threads sur le
  même device en boucle) — le modèle de locking est raisonné mais pas
  fuzzé.
- Pas testé avec plusieurs devices EDU simultanés (`-device edu`
  répété) — le code le permet (`next_minor`, un `edu_dev` par device),
  mais la suite de tests actuelle ne l'exerce pas.
- Périmètre volontairement limité à PCI/EDU : pas d'USB, I2C, SPI,
  device tree, alors que le cours les couvre. Choix de profondeur sur
  un sujet plutôt que de largeur sur plusieurs.
