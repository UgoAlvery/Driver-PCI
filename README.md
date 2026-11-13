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
- **MSI** : `pci_alloc_irq_vectors` préfère MSI ; repli automatique sur
  INTx si non disponible.
- **DMA bidirectionnel** : round-trip RAM→EDU→EDU→RAM via un ioctl
  (`EDU_IOC_DMA_ROUNDTRIP`), buffer cohérent alloué une fois au probe,
  complétion également signalée par IRQ.
- **poll/epoll** : `EPOLLIN` quand un résultat est prêt, `EPOLLOUT`
  toujours ; sans perte de réveil.
- **mmap DMA** : le buffer DMA cohérent est mappable en espace utilisateur
  (`dma_mmap_coherent`) ; `rmmod` retourne `EBUSY` tant qu'un mapping est actif.
- **compat\_ioctl** : struct 100 % compatible 32/64 bits (pas de pointeur,
  pas de `size_t`) ; `.compat_ioctl` enregistré.
- **Plusieurs devices** : chaque device EDU détecté obtient son propre
  `/dev/edu-factN`, son propre buffer DMA, ses propres compteurs.
- **Introspection** : compteurs d'IRQ/DMA exposés en sysfs (ABI stable)
  et en debugfs (détail de debug, non contractuel).
- **Robustesse** : gestion d'erreur en cascade sur tout le chemin de
  `probe()`, locking cohérent entre contexte process et contexte IRQ.
- **KUnit** : 12 cas de test sur la logique pure (validation entrées,
  constantes ioctl), compilés avec `obj-$(CONFIG_KUNIT)`.

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

## Résultats des corrections (2026-11-12)

| Défaut | Fichier:ligne | Statut | Tests |
|--------|--------------|--------|-------|
| Lock avant copy\_from\_user/copy\_to\_user | `edu_char.c:170,192` | Corrigé | Test concurrent DMA ajouté à `run_tests.sh` |
| `next_minor` race + fuite | `edu_char.c:261` | Corrigé via IDA | Test reload module ajouté |
| Timeouts non bornés (factorial + DMA) | `edu_char.c:82,152` | Corrigé (1 s, `-ETIMEDOUT`) | Tests existants = régression |
| Overflow N ≥ 13 (`uint32_t`) | `edu_char.c:63` | Corrigé (borne ≤ 12) | Tests N=13 et N=-1 ajoutés |

**Compilation** : 0 warning, 0 erreur — kernel 6.8.0-147-generic, `-Wall -Wextra`
(voir `results/2026-11-12/build.log`). Recompilé 0 warning contre kernel 6.18.16
debug KASAN le 2026-11-13.

**run_tests.sh** (2026-11-12) : non exécuté — VM non disponible dans cet environnement.
**Tests d'intégration** (2026-11-13) : 17/17 PASS sur kernel KASAN+lockdep+kmemleak,
2 devices. Voir `results/2026-11-13-kasan-lockdep/`.

## Vérification dynamique (2026-11-13)

Kernel de debug : Linux **6.18.16** compilé depuis les sources
(gcc 13.3, 20 cœurs, 23 min), booté via QEMU KVM sur Alpine
(`kernel_training.qcow2`, root `/dev/sda3`).

Options activées : `CONFIG_KASAN=y` (KASAN Generic Inline),
`CONFIG_PROVE_LOCKING=y` (lockdep), `CONFIG_DEBUG_KMEMLEAK=y`,
`CONFIG_DEBUG_ATOMIC_SLEEP=y`, `CONFIG_DEBUG_SPINLOCK=y`,
`CONFIG_DEBUG_MUTEXES=y`. `CONFIG_PANIC_ON_OOPS` non activé.

**Preuve que les détecteurs fonctionnent (Étape 2 obligatoire) :**
- Module jetable `kasan_test.ko` : écrit 1 octet hors de `kmalloc(16)` →
  `BUG: KASAN: slab-out-of-bounds in kasan_test_init` détecté
  (voir `results/2026-11-13-kasan-lockdep/dmesg-kasan-proof.txt`).
- Module jetable `lockdep_test.ko` : ordre AB-BA sur deux mutex →
  `WARNING: possible circular locking dependency detected / *** DEADLOCK ***`
  (voir `results/2026-11-13-kasan-lockdep/dmesg-lockdep-proof.txt`).

**Scénarios exécutés sur 2 devices (`/dev/edu-fact0` et `/dev/edu-fact1`) :**

| Scénario | Résultat |
|----------|----------|
| Factorielle 8!, 0! | PASS |
| Overflow N ≥ 13 | PASS |
| Input négatif | PASS |
| sysfs `irq_count` | PASS |
| DMA round-trip (256 octets) | PASS |
| Module reload, minor réutilisé (IDA) | PASS |
| DMA concurrent sur même device | PASS |
| Factorielle concurrente sur 2 devices (20 iter.) | PASS |
| DMA concurrent sur 2 devices (10 iter.) | PASS |
| 10 cycles load/unload (IDA minor reuse) | PASS |
| IRQ sous charge (50 factorielles × 2 devices) | PASS |
| Déchargement final | PASS |
| kmemleak après tous les tests | PASS (vide) |

**Total : 17/17 PASS.**

Rapports KASAN : 0 sur les chemins exercés (hors modules de preuve jetables).
Rapports lockdep : 0 sur les chemins exercés (hors modules de preuve jetables).
Rapports kmemleak : 0.
KCSAN : non effectué (build séparé requis, hors périmètre de cette session).

Preuves archivées dans `results/2026-11-13-kasan-lockdep/` :
`.config` du kernel de debug, commande QEMU exacte, `dmesg` complet,
extraits KASAN et lockdep des modules de preuve, résultats des tests,
sortie kmemleak.

**Absence de rapport sur les chemins exercés ; ne prouve pas l'absence
de défaut sur les chemins non exercés.**

## Driver v2 — évolutions (2026-11-13)

Cinq évolutions ajoutées après la validation KASAN/lockdep initiale,
chacune dans un commit séparé, toutes revalidées sous KASAN+lockdep.

| Étape | Évolution | Fichiers modifiés | Test |
|-------|-----------|-------------------|------|
| 1 | MSI (`pci_alloc_irq_vectors`, INTx fallback) | `edu_pci.c` | `PCI-MSI` dans `/proc/interrupts` |
| 2 | poll/epoll (`poll_wait`, lost-wakeup safe) | `edu_char.c` | `tests/poll_test.c` |
| 3 | mmap DMA buffer (`dma_mmap_coherent`, module-ref) | `edu_char.c` | `tests/mmap_test.c` |
| 4 | compat\_ioctl + uapi header guards | `edu_char.c`, `edu_ioctl.h` | recompilation userspace 0 warning |
| 5 | KUnit (12 cas, logique pure) | `edu_kunit.c`, `Makefile` | `obj-$(CONFIG_KUNIT)` gated |

**run\_tests.sh après v2 : 14 tests** (11 originaux + MSI check + poll + mmap).

### Vérification KASAN+lockdep v2 (2026-11-13)

Kernel et configuration identiques à la section ci-dessus (Linux 6.18.16
KASAN Generic Inline + lockdep + kmemleak). Module compilé, chargé, exercé
sur les nouveaux chemins (MSI, poll, mmap, compat\_ioctl), puis déchargé.

| Scénario v2 | Résultat |
|-------------|----------|
| MSI Enable+ (`lspci -v`) | PASS |
| poll POLLOUT + POLLIN (5! = 120) | PASS |
| mmap cohérence + ioctl concourant | PASS |
| rmmod bloqué avec mmap actif | PASS |
| DMA round-trip compat\_ioctl path | PASS |
| Chargement/déchargement final | PASS |

Rapports KASAN v2 : **0**. Rapports lockdep v2 : **0**.
Preuves archivées dans `results/2026-11-13-v2/`.

**Absence de rapport sur les chemins exercés ; ne prouve pas l'absence
de défaut sur les chemins non exercés.**

### Benchmark v2 (kernel sans KASAN, 2026-11-13)

Kernel `6.18.16-0-virt` Alpine (CONFIG\_KASAN=n, CONFIG\_PROVE\_LOCKING=n).
60 répétitions, `clock_gettime(CLOCK_MONOTONIC)`. Chiffres réels mesurés.

| Métrique | Mode | min | médiane | p95 | max |
|----------|------|-----|---------|-----|-----|
| Latence IRQ (write+read 8!) | MSI | 44 µs | **53 µs** | 244 µs | 313 µs |
| Latence IRQ (write+read 8!) | INTx | 51 µs | **58 µs** | 172 µs | 193 µs |
| Latence DMA (round-trip 4096 B) | MSI | 199 ms | **202 ms** | 218 ms | 218 ms |
| Débit DMA (round-trip 4096 B) | MSI | 20 KB/s | **20 KB/s** | 18 KB/s | 18 KB/s |

**Delta MSI vs INTx (médiane) : −9 % en faveur de MSI.**

**Ces chiffres sont des valeurs relatives propres à l'émulation QEMU/KVM.**
Le DMA du device EDU est entièrement émulé par logiciel (pas de vrai DMA
PCIe) ; les ~200 ms par round-trip sont le coût de l'émulation QEMU, pas
une caractéristique du driver. L'écart MSI/INTx (5 µs médiane) inclut
le bruit du scheduler KVM ; il n'est pas représentatif de matériel réel.
Utile uniquement pour comparer deux versions du driver sur le même hôte.

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
Extensions DMA / robustesse / tests / CI : Ugo Alvery-Marlier.
