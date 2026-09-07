# edu-driver — Linux PCI character driver for the QEMU EDU device

Driver caractère PCI pour le [device pédagogique EDU de
QEMU](https://www.qemu.org/docs/master/specs/edu.html) : calcul de
factorielle piloté par IRQ, transfert DMA bidirectionnel, introspection
via sysfs/debugfs.

Point de départ : un TP du cours *Driver Linux* d'EPITA (calcul de
factorielle, mode caractère, IRQ). Ce repo en reprend la base et
l'étend pour couvrir DMA, robustesse et une hygiène de projet
(tests, CI, documentation) volontairement au-delà du périmètre noté du TP.

## Fonctionnalités

- **Factorielle** : `echo N > /dev/edu-factX` puis `cat /dev/edu-factX`,
  calcul effectué par le device, complétion signalée par IRQ (pas de
  polling).
- **DMA bidirectionnel** : round-trip RAM→EDU→EDU→RAM via un ioctl
  (`EDU_IOC_DMA_ROUNDTRIP`), buffer cohérent alloué une fois au probe,
  complétion également signalée par IRQ.
- **Plusieurs devices** : chaque device EDU détecté obtient son propre
  `/dev/edu-factN`, son propre buffer DMA, ses propres compteurs.
- **Introspection** : compteurs d'IRQ/DMA exposés en sysfs (ABI stable)
  et en debugfs (détail de debug, non contractuel).
- **Robustesse** : gestion d'erreur en cascade sur tout le chemin de
  `probe()`, locking cohérent entre contexte process et contexte IRQ.

## Architecture

```
main.c        -- module_init/exit, orchestre char + pci
edu_pci.c     -- probe/remove PCI, IRQ handler, allocation DMA
edu_char.c    -- cdev, read/write/ioctl, attributs sysfs, debugfs
edu_mmio.c    -- accès bas niveau aux registres MMIO du device
edu_common.h  -- constantes partagées kernel <-> userspace (taille buffer DMA)
edu_ioctl.h   -- interface ioctl exposée à l'espace utilisateur
edu_dev.h     -- struct edu_dev (état par device)
```

`edu_common.h` et `edu_ioctl.h` n'incluent volontairement aucun header
kernel-only : ce sont les seuls fichiers qu'un programme userspace (voir
`tests/dma_roundtrip_test.c`) a besoin d'inclure pour piloter le DMA.

Pour le détail des décisions de design (pourquoi ce locking, pourquoi ce
choix d'IRQ plutôt que de polling, bugs trouvés et corrigés en cours de
route), voir [`DESIGN.md`](DESIGN.md).

## Build

```bash
make          # compile edu.ko contre le kernel courant
make clean
```

Nécessite les headers du kernel cible (`/lib/modules/$(uname -r)/build`).

## Lancer dans QEMU (environnement de développement)

Ce projet a été développé et testé sous WSL2 Ubuntu, avec une image
noyau fournie par le cours (`kernel_training.qcow2`, non incluse dans ce
repo — propriétaire).

```bash
./scripts/run_qemu.sh
```

Voir [`tests/README.md`](tests/README.md) pour le détail des tests
d'intégration et la procédure complète (build, copie dans la VM,
exécution).

Une fois dans le guest :

```bash
insmod edu.ko
echo 8 > /dev/edu-fact0
cat /dev/edu-fact0        # -> 40320
cat /sys/class/edu-fact/edu-fact0/irq_count   # -> 1
```

## Tests et CI

- `tests/run_tests.sh` : suite d'intégration locale contre le vrai
  device EDU (factorielle, sysfs, round-trip DMA).
- `.github/workflows/ci.yml` : compilation sans warning sur plusieurs
  versions de kernel + conformité `clang-format`, à chaque push. Ne
  teste pas le comportement matériel (voir `tests/README.md` pour
  pourquoi).

## Limites connues

- Testé uniquement en environnement QEMU/EDU émulé, jamais sur matériel
  PCI réel — attendu, le device EDU n'existe que dans QEMU.
- Le périmètre est volontairement restreint à PCI/EDU (pas d'USB, I2C,
  device tree), pour privilégier la profondeur sur un sujet plutôt que
  la largeur.
- `-enable-kvm` dans `scripts/run_qemu.sh` suppose la virtualisation
  imbriquée disponible sous WSL2 ; sinon, QEMU bascule automatiquement
  sur l'émulation logicielle (TCG) si l'option est retirée.

## Auteurs

Voir [`Authors.txt`](Authors.txt) pour la base du TP original.
Extensions DMA / robustesse / tests / CI : Ugo Alvery-Marlier.C'est un choix assumé plutôt qu'une CI qui prétendrait valider le
matériel alors qu'elle ne le peut pas.
