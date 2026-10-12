# Notes pour reprendre le travail (agents et humains)

Ce dépôt contient le patcher **op1r** (amont, sur `main`) et, par-dessus, nos exemples **roll** (séquenceur piano
roll) et **drive** (distorsion), avec les outils d'étude et de construction du firmware OP-1 R246.

À lire d'abord :
- [docs/developpement.md](docs/developpement.md) : état des lieux, commandes, carte mémoire, chaîne des hooks,
  identifiants réservés, procédures (nouvelle version d'op1r, image tierce, nouvelle fonction), pièges ;
- [docs/firmware-r246.md](docs/firmware-r246.md) : étude du firmware (formats, sous-systèmes, recette d'op1r) ;
- `examples/pianoroll/README.md` et `examples/drive/README.md` : mode d'emploi, branchement, état.

## Règles

- **Ne jamais commiter de firmware** : ni Teenage Engineering, ni image tierce (`*.op1`, `*.ldr`, arbres
  dépaquetés et `work/` sont ignorés). Seuls des diffs, des adresses et des sha256 vont dans git.
- Une adresse dans le bloc d'op1r (`01e0xxxx`-`01e2xxxx`) change à chaque version d'op1r. Elle doit être déclarée
  dans la section op1r d'un `.ld` et vérifiée par `build.sh` (`check`, `word`, empreinte du bloc), jamais dans la
  section « stock ».
- Chaque hook passe au maillon précédent tout ce qui ne le concerne pas (6.3 du guide).
- Ne pas dire « testé » sans flash sur l'OP-1 ; l'utilisateur teste et fait ses retours.
- Docs d'étude et README des exemples en français ; `README.md` et `CHANGELOG.md` en anglais. Une entrée de
  changelog par lot poussé.

## Commandes utiles

```sh
tools/build_bfin_toolchain.sh && export PATH=$PWD/work/bfin/bin:$PATH        # chaîne bfin-elf (une fois)
python3 tools/fwbuild.py op1_246.op1 --op1r -o ref.op1                         # référence op1r
examples/drive/build.sh op1_246.op1 op1r-drive.op1 [op1_249.op1]               # op1r (ou base) + roll + drive
examples/pianoroll/build.sh op1_246.op1 op1r-roll.op1                          # op1r + roll
examples/run_tests.sh                                                          # tests sur PC
python3 tools/fwcheck.py diff op1r-drive.op1 ref.op1                           # contrôle avant flash
```

Firmware d'origine attendu : `op1_246.op1`, sha256 `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9`.
