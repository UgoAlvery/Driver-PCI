# Tests

Deux niveaux de test, pour deux usages différents :

## 1. Tests locaux (ce dossier) — nécessitent le vrai device EDU

`run_tests.sh` pilote une VM QEMU déjà démarrée (via `scripts/run_qemu.sh`)
par SSH, et vérifie le comportement réel du driver : factorielle,
compteurs sysfs, round-trip DMA.

Étapes :

```bash
# 1. Démarrer la VM (dans un autre terminal, reste ouvert)
./scripts/run_qemu.sh

# 2. Compiler le module et le programme de test DMA
make
make -C tests

# 3. Copier les deux binaires dans le dossier partagé (9p, mount_tag=workdir)
cp edu.ko tests/dma_roundtrip_test workdir/

# 4. (dans le guest, une seule fois) monter le partage :
#    mount -t 9p -o trans=virtio workdir /mnt/workdir

# 5. Lancer la suite de tests depuis l'hôte
./tests/run_tests.sh
```

Le script attend que SSH réponde, charge le module, exécute les
vérifications fonctionnelles, et termine avec un code de sortie non nul
si un test échoue (utilisable dans un script d'intégration plus large).

## 2. CI GitHub Actions (`.github/workflows/ci.yml`) — pas de device réel

`kernel_training.qcow2` est une image propriétaire du cours EPITA :
impossible à committer sur GitHub (licence, taille) ni à reconstruire à
l'identique dans un runner. La CI ne peut donc pas exécuter les tests
fonctionnels ci-dessus.

Elle vérifie à la place ce qui est vérifiable sans device réel :

- **Compilation propre**, sans warning, contre les headers kernel de deux
  versions de runner différentes (`ubuntu-22.04` et `ubuntu-24.04`) —
  attrape les régressions de compilation et les usages d'API kernel non
  portables.
- **Conformité `clang-format`** contre `.clang-format`.

C'est un choix assumé plutôt qu'une CI qui prétendrait valider le
matériel alors qu'elle ne le peut pas.
