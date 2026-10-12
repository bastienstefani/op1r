# Guide de développement et de reprise

Ce guide sert à reprendre le travail sur les ajouts faits par-dessus op1r : le séquenceur **roll**, l'effet
**drive**, la construction sur une **image tierce**, ainsi que les outils associés. Il décrit l'état actuel, la
façon de construire et de vérifier, l'architecture (mémoire, hooks, identifiants), les procédures de mise à jour
et les pièges rencontrés.

L'étude du firmware lui-même (format, sous-systèmes, recette d'op1r) est dans
[firmware-r246.md](firmware-r246.md). Le mode d'emploi de chaque ajout est dans son README :
[pianoroll](../examples/pianoroll/README.md) et [drive](../examples/drive/README.md).

## 1. État des lieux (octobre 2026)

| Élément | Où | État |
|---|---|---|
| op1r (le patcher) | `patch_op1.py`, `build_patch.py`, `data/`, `web/` | maintenu en amont, sur `main` ; version **op1r 12** |
| Outils d'étude et de construction | `tools/` | `fwtool.py`, `fwbuild.py` (`--op1r`, `--base`), `fwcheck.py`, `analyze.sh`, `build_bfin_toolchain.sh` |
| MIDI panic | `examples/midi-panic` | exemple minimal, **jamais flashé** |
| Piano roll « roll » | `examples/pianoroll` | **testé sur matériel** : édition, lecture, sauvegarde, HOLD, départ calé sur le temps (sur op1r 10, puis 12) |
| Distorsion « drive » | `examples/drive` | **flashée** sur op1r 12 et sur op1_249, premiers retours bons ; voir la liste détaillée dans son README |
| Construction sur op1_249 | `examples/drive/build.sh … op1_249.op1` | **flashée**, « tout bon pour l'instant » |

Le travail se fait sur la branche `claude/brave-galileo-8ytbnt`, qui fusionne `main` (op1r amont) à chaque
nouvelle version. Les images ne sont jamais commitées (voir 10).

## 2. Prérequis

- **Firmware d'origine** `op1_246.op1`, sha256 `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9`.
  La façon de le récupérer est dans [firmware-r246.md](firmware-r246.md) §1.
- **Chaîne Blackfin** (binutils 2.42 + GCC 14.2 `bfin-elf`), une fois pour toutes, environ 30 minutes :
  ```sh
  tools/build_bfin_toolchain.sh
  export PATH=$PWD/work/bfin/bin:$PATH     # ou BFIN_PREFIX=/chemin/vers/bin/bfin-elf-
  ```
- **Python 3**, bibliothèque standard uniquement. Pillow est facultatif : il ne sert qu'aux aperçus d'écran.
- **Images de base facultatives** : `op1_249.op1` (<https://maikopruett.com/firmware/op1_249.op1>), sha256
  `b7adf5c0fdd523a726e81880e683eda48061a261944dfa1a7a52c57ebfc41066`.

## 3. Construire

| Image | Commande | Écran de démarrage |
|---|---|---|
| op1r seul (référence) | `python3 tools/fwbuild.py op1_246.op1 --op1r -o ref.op1` | `op1r 12` |
| op1r + roll | `examples/pianoroll/build.sh op1_246.op1 op1r-roll.op1` | `op1rroll` |
| op1r + roll + drive | `examples/drive/build.sh op1_246.op1 op1r-drive.op1` | `op1r+drv` |
| op1_249 + roll + drive | `examples/drive/build.sh op1_246.op1 op1r-249-drive.op1 op1_249.op1` | `249+drv` |

- La référence doit redonner exactement le sha256 `result` de `data/manifest.json` : c'est le contrôle que le
  patch op1r est bien appliqué.
- Chaque `build.sh` commence par vérifier l'image de départ (voir 6.6) et refuse de construire si quelque chose a
  bougé.
- Chaque `build.sh` écrit aussi la liste désassemblée du code ajouté, `<sortie>.dis`.

Pour flasher : démarrer l'OP-1 en mode mise à jour du bootloader et copier le `.op1`. Le bootloader n'est jamais
modifié, donc toute image se reflashe, y compris l'originale (voir 8 pour les noms enregistrés).

## 4. Tester sans OP-1

```sh
examples/run_tests.sh [dossier]     # compile et lance les tests ; les aperçus d'écran vont dans le dossier
```

| Test | Ce qu'il vérifie |
|---|---|
| `pianoroll/test_beat.c` | le départ calé sur le temps (`beat.h`) : 5 M de ticks avec des remises à zéro aléatoires, y compris pendant un tick ; aucun départ hors temps, attente ≤ 95 ticks |
| `drive/test_host.c` | le DSP (`dsp.h`) : toutes les combinaisons de réglages sous UBSan (aucun débordement), filtre stable ; chiffres par mode ; `curves.csv` |
| `drive/test_hooks.c` | les hooks de `drive.c` contre un firmware simulé : noms, aiguillage audio, choix de l'écran, pages, encodeurs, écriture dans le bon son |
| `drive/test_ui.c` + `render_ui.py` | l'écran de drive dessiné sur PC avec le même code (PNG) |

Les tests compilent le même C que le firmware (`beat.h`, `dsp.h`, `drive.c`). Ils ne vérifient pas les adresses
du firmware : c'est le rôle de `build.sh` et de `fwcheck.py`.

## 5. Vérifier une image avant de la flasher

`tools/fwcheck.py` regroupe les contrôles faits à chaque livraison :

```sh
python3 tools/fwcheck.py diff  op1r-drive.op1 ref.op1          # ou la base : … op1r-249-drive.op1 op1_249.op1
python3 tools/fwcheck.py calls op1r-drive.dis 01ee0000 01f00000
python3 tools/fwcheck.py refs  op1r-drive.dis 01e00000 01ee0000  # mémoire hors de notre bloc utilisée par notre code
python3 tools/fwcheck.py blockhash ref.op1 01e00000             # empreinte du bloc op1r (pour build.sh)
```

**`diff`** doit montrer :
- `[files]` : seulement `OP1_vdk.ldr`, `op1_factory.db` et nos SVG (`roll.svg`, `drive.svg`) ; jamais
  `te-boot.ldr` ;
- `[OP1_vdk.ldr]` : en-têtes / FIRST / FINAL ok, aucun chevauchement de blocs dans la zone libre ;
- `[memory]` : uniquement les sites de 6.3 (3 ou 4 octets chacun, plus le texte de démarrage en `018f738a`) et
  notre bloc à `01ee0000` ;
- `[content/op1_factory.db]` : nos lignes, rien d'autre (`seq_types (9,'roll')`, `fx_types (14,'drive',…)`).

**`calls`** ne doit lister que des fonctions d'origine (`fw_*`) et les fonctions d'op1r ou de la base déclarées
dans les `.ld`. **`refs`** (hors de notre bloc) ne doit montrer que les deux caches d'op1r que drive remet à zéro (`01e1b00c`,
`01e1b008`).
L'image doit aussi faire moins de 15 Mo (`diff` le signale).

## 6. Architecture

### 6.1 Principe

C'est la recette d'op1r ([firmware-r246.md](firmware-r246.md) §10), appliquée par-dessus op1r :

- un nouveau type est déclaré dans `op1_factory.db`, puis rendu équivalent à un type d'origine caché : roll sur
  arpeggio, drive sur filter ;
- chaque point d'accroche est détourné vers notre code, qui traite ce qui le concerne puis **appelle le hook
  précédent**, c'est-à-dire op1r ou la base ;
- notre code (C, `bfin-elf-gcc`) est lié en **un seul bloc** à `01ee0000` (`examples/pianoroll/fw.ld`, inclus par
  `examples/drive/drive.ld`) et inséré dans `OP1_vdk.ldr` par `tools/fwbuild.py`.

### 6.2 Carte mémoire de la zone libre (`01dfff34-01f00000`)

| Plage | Occupant |
|---|---|
| `01e00000-01e2a1d0` | bloc op1r 12 (code, données, BSS compris) ; il ne référence rien au-delà de `01e2a0c8` |
| `01e2a1d0-01e40000` | libre |
| `01e40000-01e76558` | op1_249 : code et données (seulement avec cette base) |
| `01e76558-01eb7464` | op1_249 : BSS (bloc FILL) |
| `01eb7464-01ee0000` | libre |
| `01ee0000-01ee7ac0` | **roll + drive** (31 424 octets ; roll seul : 20 776), place jusqu'à `01f00000` |
| `01f00000-01f21308` | te-boot (actif pendant le chargement : ne rien y mettre) |

Ailleurs : op1_249 utilise aussi `02800000-02980000` (bloc FILL de 1,5 Mo). Le tas du firmware est une arène de
`0x840000` octets, de descripteur `0106b7cc`, créée par `01a5e364`. *Déduit* : l'arène s'étend de `0106b7cc` à
`018ab7cc`, la fin de la BSS, donc elle ne touche pas la zone libre. Avant d'ajouter un bloc, contrôler avec
`fwcheck.py diff` ; `fwbuild.py` refuse de toute façon un bloc qui en chevauche un autre, FILL compris.

### 6.3 Chaîne des hooks (image op1r + roll + drive)

| Site | Fonction d'origine | Chaîne |
|---|---|---|
| CALL `01a42a2e`, `01a42bdc`, `01a42ce0` | `SeqIds::idFromName` | `rp_id_from_name` → op1r `01e00a1c` → origine |
| JUMP.L `01a422ec` | `SeqIds::nameFromId` | `rp_name_from_id` → op1r `01e00af8` |
| CALL `01a0d1b0` | instance de l'arpeggio `01a46400` | `rp_arp_instance` → origine |
| JUMP.L `01a5e7d0` | remise à zéro de l'horloge séquenceur | `rp_clock_reset` (copie + drapeau) |
| CALL `01a3bd58` | construction des écrans | `dv_build_screens` → `rp_build_screens` → op1r `01e00f0c` → origine |
| CALL `019f07d2` | init des écrans | `dv_init_screens` → `rp_init_screens` → op1r `01e0121c` → origine |
| CALL `019c2d3e` | `key_dispatch` | `dv_key` → `rp_key` → [base `01e405f4` →] op1r `01e01468` → origine |
| CALL `019c2e94` | `encoder_dispatch` | `dv_encoder` → `rp_encoder` → [base `01e40658` →] op1r `01e01a80` → origine |
| JUMP.L `019aad8e` | comparaison de noms `fx_types` | `dv_fx_name_eq` → op1r `01e00d38` |
| mots `01910ca8` / `01910cb0` | filter mono : process / set_position | `dv_fx_mono*` → op1r `01e00988` / `01e007ec` → origine |
| mots `01910cc8` / `01910cd0` | filter stéréo (master) : process / set_position | `dv_fx_stereo*` → op1r `01e00830` / `01e00818` → origine |
| mot `018f7ff8` | ScreenId fx, `case` 5 | `dv_scr_case` → `dv_scr_id` → op1r `01e02018` |

Avec op1_249, seuls les deux maillons « base » s'ajoutent : ses hooks touches et encodeurs appellent ses modules,
puis ceux d'op1r. Ses autres sites (bande, clipboard, FAT, boucle principale, notes, snapshots) ne recoupent pas
les nôtres. `fwcheck.py diff` contre `op1_249.op1` le montre.

### 6.4 Identifiants réservés

| Espace | Origine | op1r | Nos ajouts |
|---|---|---|---|
| `seq_types.id` | 1-6 | 7 euclid, 8 dots | **9 roll** |
| séquenceur hôte (id interne) | 0-5 | euclid et dots sur finger (3) | roll sur arpeggio (**5**) + drapeau par mode |
| `fx_types.id` | 3-10 (+ filter 2 réactivé par op1r) | 11 tape, 12 clouds, 13 reverb | **14 drive** |
| marqueur d'effet (`params[4]`) | – | `0x5301` à `0x5303` (op1r accepte 1 ≤ n < 4) | **`0x5304`** |
| écrans (registre `01df4e54`) | – | `0xb` effets, `0x14` iter lab, `0x1c` quad, `0x2d` euclid/dots | **`0x2c`** roll (emplacement échangé), **`0xb`** drive (partagé) |
| tables de `op1.db` | – | `euclidseq`, etc. | **`rollseq`** |

Un nouvel ajout prend le **suivant libre** dans chaque espace, et le vérifie dans `op1_factory.db` de l'image de
départ. Un id déjà pris fait échouer l'`INSERT` de `fwbuild.py`.

### 6.5 Contextes d'exécution

| Où | Fil | Règles |
|---|---|---|
| `roll_tick`, `roll_stop` | fil séquenceur (un tick = 1/96 de noire) | court ; les notes passent par `fw_note_on`/`fw_note_off` |
| `dv_fx_mono`, `dv_fx_stereo` | **interruption audio IVG11**, 64 échantillons par 1,45 ms | aucun appel au firmware, pas de division 64 bits, peu de pile ; le 4e argument stéréo est sur la pile |
| `*_draw` | fil d'affichage | lignes `fw_line`, coordonnées 22.10 |
| `*_key`, `*_encoder` | fil KeyHandler | rendre la main vite ; appeler le maillon suivant pour tout ce qui ne nous concerne pas |
| `*_build_screens`, `*_init_screens` | démarrage (OP_Main) | le séquenceur sauvé est chargé **avant** les écrans (voir 8) |
| `roll_save` | fil FileWorker (`bg_saver`) | SQL par `fw_db_exec` |

Les échanges entre fils passent par des variables `volatile` (drapeaux) ; les écritures alignées de 8, 16 et 32 bits
sont atomiques.

### 6.6 Où sont les adresses, et comment elles sont vérifiées

- `examples/pianoroll/fw.ld` contient deux sections :
  - **adresses d'origine** R246, uniquement : `build.sh` refuse une adresse `0x01e…` dans cette section ;
  - **hooks op1r** que l'on chaîne.

  Le maillon suivant des touches et encodeurs, `_rp_next_key` / `_rp_next_encoder`, vaut op1r par défaut, ou la
  base si `--defsym _base_key=… _base_encoder=…`.
- `examples/drive/drive.ld` inclut `fw.ld` et ajoute les adresses d'origine de drive, les fonctions internes
  d'op1r qu'il appelle, et la chaîne `drive → roll`.
- `examples/drive/build.sh` :
  - **épingle le bloc op1r par son sha256** (`b5bc2168…`, op1r 12), car drive appelle des fonctions internes
    d'op1r ;
  - vérifie chaque site de 6.3 (`check` pour les CALL/JUMP.L, `word` pour les mots de table) ;
  - vérifie que les octets de `01a5e7d0` sont ceux d'origine ;
  - accepte une base seulement si son sha256 est dans sa liste.
- `examples/pianoroll/build.sh` fait les mêmes contrôles pour roll seul.

## 7. Procédures

### 7.1 Nouvelle version d'op1r sur `main`

1. `git fetch origin main && git merge origin/main`. En cas de conflit dans `CHANGELOG.md`, garder les deux
   historiques, le plus récent en tête.
2. Construire la référence `ref.op1` (§3) et vérifier son sha256 contre `data/manifest.json`.
3. Dépaqueter et désassembler :
   ```sh
   python3 tools/fwtool.py unpack op1_246.op1 W/orig && python3 tools/fwtool.py apply W/orig W/op1r
   python3 tools/fwtool.py hooks W/orig > W/hooks.txt          # tous les sites d'op1r et leurs cibles
   python3 tools/fwtool.py mem W/op1r/OP1_vdk.ldr W/mem
   python3 tools/fwtool.py dump W/mem/regions.json 0x01e00000 <fin du bloc> W/op1r.bin
   bfin-elf-objdump -D -b binary -m bfin --adjust-vma=0x01e00000 W/op1r.bin > W/op1r.dis
   ```
4. Relever les nouvelles cibles :
   - les sites de 6.3 viennent de `hooks.txt` ;
   - les mots de table : `fwtool.py rd W/mem/regions.json 0x01910ca8 4`, `… 0x01910cc8 4`, `… 0x018f7ff8 1` ;
   - le choix de l'écran d'effet est la cible du `CALL` placé au début du `case` (`018f7ff8`) ;
   - les deux caches de l'aiguilleur (« quel effet op1r a tourné en dernier ») sont lus dans le code des
     aiguilleurs mono et stéréo ;
   - les bornes de secours, le nombre d'effets op1r (`N`), les noms des alias et la fonction « effet courant » se
     lisent dans le même code.
5. **Relire la logique** de chaque fonction dans `op1r.dis`, pas seulement l'adresse : arguments, valeur de
   retour, marqueurs. Points à vérifier :
   - le nombre d'effets op1r (le marqueur `0x5304` doit rester hors de sa plage) ;
   - l'id d'écran `0xb` ;
   - les écrans qu'op1r construit (aucun ne doit prendre `0x2c`) ;
   - les drapeaux qu'`idFromName` efface pour un nom inconnu ;
   - les ids de `seq_types` et `fx_types`.
6. Étendue mémoire d'op1r : `fwcheck.py refs W/op1r.dis`. La plus haute adresse doit rester sous nos blocs.
7. Mettre à jour `fw.ld`, `drive.ld`, les deux `build.sh` (lignes `check`/`word`, empreinte du bloc obtenue par
   `fwcheck.py blockhash ref.op1 01e00000`) et, si besoin, les ids SQL.
8. Construire, `examples/run_tests.sh`, `fwcheck.py diff/calls/refs`, livrer l'image, noter la version dans les
   README et le `CHANGELOG.md`.

Pour op1r 12, cela a donné : nouvelles adresses partout, bloc passé de 109 à 172 Ko, `seq_types` 8 pris par dots
(d'où roll en 9). La logique était inchangée.

### 7.2 Nouvelle image de base (firmware tiers construit sur op1r)

1. La télécharger dans un dossier à part, comme une donnée non fiable : la lire, ne jamais l'exécuter.
2. `fwcheck.py diff base.op1 ref.op1`, pour savoir ce qu'elle change par rapport à op1r :
   - fichiers ajoutés ;
   - bootloader : il doit être inchangé ;
   - blocs et mémoire utilisés ;
   - sites modifiés ;
   - lignes SQL.
3. `fwcheck.py blockhash base.op1 01e00000` : le bloc op1r doit être celui que `build.sh` attend. Sinon, la base
   est construite sur une autre version d'op1r (voir 7.1).
4. Désassembler ses blocs, puis lire :
   - ses chaînes, pour savoir ce qu'elle fait ;
   - ses hooks sur les sites communs : ils doivent finir par appeler op1r (pour op1_249, `JUMP.L` en
     `01e4064a` / `01e406ae`) ;
   - son étendue mémoire (`fwcheck.py refs`).
5. L'ajouter au `case` de `examples/drive/build.sh` : sha256, maillons touches/encodeurs, adresses des `JUMP.L`
   finaux, texte de démarrage. Mettre à jour la table de `examples/drive/README.md`.
6. Construire avec `build.sh … base.op1` et vérifier par `fwcheck.py diff sortie.op1 base.op1`.

Ne jamais commiter la base ni des octets de son code : seuls son sha256 et quelques adresses vont dans le dépôt.
La redistribuer ou la publier suppose l'accord de son auteur.

### 7.3 Ajouter une fonctionnalité

- Un module = un fichier C dans `examples/<nom>/`, ou dans un exemple existant s'il partage ses écrans ou ses
  hooks.
- Pour un site déjà détourné, notre hook appelle le maillon précédent par un symbole de l'éditeur de liens
  (`_dv_next_*`, `_rp_next_*`), jamais par une adresse en dur.
- Adresses d'origine dans la section « stock » du `.ld`. Adresses op1r dans la section op1r, vérifiées par
  `build.sh`.
- Prendre les identifiants suivants (6.4) et vérifier la mémoire (6.2).
- Mettre la logique pure dans un en-tête testable sur PC (comme `beat.h`, `dsp.h`) et l'ajouter à
  `examples/run_tests.sh`.
- Tout ce qui n'est pas vérifié sur l'OP-1 est marqué comme tel dans le README.

## 8. Pièges rencontrés

| Symptôme | Cause | Règle |
|---|---|---|
| Arrêt au démarrage `SeqIds.cpp:41 unknown sequencer name: 'roll'` | « roll » passé à l'`idFromName` d'origine, ou image sans roll alors que roll est le séquenceur enregistré | ne passer à l'origine que des noms qu'elle connaît (`host_name` = « arpeggio ») ; avant de flasher une image sans roll, choisir un autre séquenceur en synth **et** en drum |
| Même chose pour un effet (`fx_types.cpp:42 unknown fx name`) | un son enregistré avec `"fx_type": "drive"` et une image sans drive | changer l'effet de chaque son qui utilise drive avant de flasher une image sans drive |
| Plantage EXCAUSE 0x21 (lecture à une adresse invalide) | adresse interne d'op1r périmée (police d'op1r 5 utilisée sur op1r 10) | aucune adresse op1r hors de la section vérifiée ; empreinte du bloc op1r |
| Le séquenceur sauvé est déjà actif quand les écrans sont construits | ordre de démarrage d'OP_Main | ne jamais remettre les drapeaux à zéro dans `build_screens` ; initialiser seulement ce qui ne l'est pas |
| Bloc d'op1_249 sur notre ancienne adresse `01e80000` | sa BSS (FILL) occupe `01e76558-01eb7464` | blocs à `01ee0000` ; `fwbuild.py` refuse les chevauchements |
| `INSERT` refusé dans `seq_types` | op1r 11 a pris l'id 8 | ids suivants libres (6.4) |
| SQL tronqué ou faux | `db2` passe le SQL deux fois par `vsnprintf`, 8 Ko au plus | pas de `%` ni de `'` dans les données ; chiffres séparés par des virgules |
| Assertion `… fx param out of range` (fatale) | un paramètre 0-3 de l'effet hôte hors des bornes de filter | défauts dans les bornes ; drive lit les bornes réelles (`01de5c50`, ligne 5) et corrige ses valeurs comme op1r |
| Pas de presets pour fmbell, supersaw, granular, iter | ces moteurs n'ont pas de presets d'usine ; la liste Shift + T1 ne montre de presets que sous SAMPLER (origine) | les moteurs sont dans Shift + T1, les presets dans Shift + 1…8 |
| Le roll démarre à contretemps | l'horloge passée au lecteur compte modulo 48 (demi-temps) | `beat.h` compte les tours ; `rp_clock_reset` signale chaque remise à zéro |

## 9. Limites connues et pistes

- **roll** :
  - changer d'octave pendant la lecture peut laisser une note bloquée (comme euclid) ;
  - l'état lecture/HOLD n'est pas sauvé ;
  - le calage sur le clic du métronome n'est garanti qu'après un Start MIDI, car le métronome est dans la L1
    encodée.
- **drive** :
  - en effet master, la liste affiche « filter » après redémarrage (même limite que tape, clouds et reverb
    d'op1r) ;
  - la modulation par LFO et la course des encodeurs d'origine (drive, fréquence, mix) ne sont pas vérifiées ;
  - les modes sont nos formules, pas celles de Serum.
- **op1_249** : ses fonctions ne sont connues que par ses chaînes de texte. On ne sait pas encore si sa
  quantification s'applique aux notes jouées par roll.
- **Pistes** :
  - dossiers de presets pour les moteurs d'op1r dans le navigateur Shift + 1…8 (lignes `synth_presets`, sans
    code) ;
  - presets par moteur dans Shift + T1 (hook du constructeur de liste `01a02444`, voir l'étude du navigateur
    en [firmware-r246.md](firmware-r246.md) §8.4) ;
  - autres pistes dans [firmware-r246.md](firmware-r246.md) §11.

## 10. Conventions du dépôt

- **Jamais de firmware TE ni d'image tierce dans git** : `*.op1`, `*.ldr`, les arbres dépaquetés et `work/` sont
  ignorés. Seuls des diffs, des adresses et des sha256 sont commités. Les SVG de TE ne sont pas copiés ; les nôtres
  sont dessinés par le code.
- Les listes `.dis` produites par les `build.sh` ne contiennent que notre code ; elles sont ignorées aussi.
- Langue : l'étude, ce guide et les README des exemples sont en français ; le README principal et le
  `CHANGELOG.md` sont en anglais, comme en amont.
- Une entrée de `CHANGELOG.md` par lot poussé, avec l'empreinte de sortie d'op1r (inchangée pour les exemples).
- Rien n'est dit « testé » s'il n'a pas été flashé.
