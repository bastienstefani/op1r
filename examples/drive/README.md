# Exemple : effet de distorsion « drive »

Distorsion pour l'OP-1 (R246), ajoutée **par-dessus op1r 12 et le piano roll** comme nouvelle entrée « drive » de la
liste des effets. Le fonctionnement s'inspire du module Distortion de Serum (modes de waveshaping, drive, filtre
avant ou après, mix). L'écran reprend son affichage (courbe de transfert, forme d'onde déformée) dans le langage
visuel de l'OP-1 : fond noir, traits fins, une couleur par encodeur. Exemple d'étude pour
[docs/firmware-r246.md](../../docs/firmware-r246.md). **Non testé sur matériel.** Le firmware d'origine et l'image
op1r se reflashent toujours (le bootloader n'est pas modifié).

Les modes sont nos propres formules, nommées d'après ceux de Serum et réglées pour s'en approcher à l'oreille : ce
ne sont pas les courbes de Serum.

## Commandes

Sur l'écran de l'effet (drive choisi comme effet du synthé, de la batterie ou en effet master) :

| | Page 1 | Page 2 |
|---|---|---|
| **Bleu** | drive | routage du filtre : OFF / PRE / POST |
| **Vert** | mode (un cran = un mode) | type de filtre : LP / BP / HP |
| **Blanc** | fréquence du filtre (30 Hz – 10 kHz) | résonance |
| **Orange** | mix (sec / traité) | niveau de sortie (−24 à +6 dB) |

- **Appui sur un encodeur** : passe de la page 1 à la page 2 et inversement (deux points sous la courbe).
- 13 modes : TUBE, SOFT CLIP, HARD CLIP, DIODE 1, DIODE 2, LIN FOLD, SIN FOLD, ZERO-SQUARE, DOWNSAMPLE, ASYM,
  RECTIFY, X-SHAPER, X-SHAPER ASYM. En DOWNSAMPLE le drive règle la réduction (1 à 64 échantillons). En RECTIFY il
  va du signal propre (0 %) au redressement simple alternance (50 %) puis double alternance (100 %). Les deux FOLD
  vont jusqu'à 10× ; les autres modes jusqu'à 40× (32 dB).
- Chaîne : entrée → filtre PRE → drive → mode → filtre POST → mix → niveau. Un passe-haut à 35 Hz recentre les modes
  asymétriques (pas de DC).
- Tous les réglages sont écrits dans le son, comme les autres effets : ils sont sauvés avec lui et voyagent avec
  les presets. Un LFO qui vise l'effet devrait moduler les réglages de la page 1 (drive, mode, fréquence, mix) ; à
  vérifier.

## L'écran

- En haut : le nom du mode (vert, la couleur de son encodeur) et l'état du filtre (« PRE LP », « FILTER OFF »…).
- Au centre : la **courbe de transfert** du mode au drive courant, mix compris (trait bleu avec un halo, comme dans
  Serum). Le **point blanc** montre où se trouve la crête du signal réel sur la courbe.
- À gauche, l'onde d'entrée ; à droite, la même onde après la courbe. Elles défilent doucement.
- En bas : les quatre réglages de la page, aux couleurs des encodeurs, avec une barre chacun.

## Essai

1. Construire et flasher (bootloader en mode mise à jour) :
   ```sh
   tools/build_bfin_toolchain.sh && export PATH=$PWD/work/bfin/bin:$PATH   # une fois
   examples/drive/build.sh op1_246.op1 op1r-drive.op1
   ```
   L'écran de démarrage affiche `op1r+drv`. L'image contient aussi le piano roll.
2. Sur un synthé : touche de l'effet, choisir **drive** dans la liste.
3. Jouer en tournant le bleu (drive) puis le vert (mode).

## À regarder pour tes retours

- L'entrée « drive » apparaît-elle dans la liste, et l'écran s'affiche-t-il ?
- Le son : chaque mode, le drive, le mix. Pas de craquement en tournant les encodeurs ?
- La page 2 : le filtre en PRE et en POST, la résonance au maximum, le niveau.
- Le vert change-t-il bien de mode à chaque cran ? Les autres encodeurs ont-ils une bonne course ?
- Sauvegarde : régler, changer de son, revenir, éteindre puis rallumer → les réglages reviennent-ils ?
- En effet **master** et sur la **batterie**.
- tape, clouds, reverb et les effets d'origine marchent-ils toujours ? Et le piano roll et euclid ?

Limite connue (la même que pour les effets d'op1r) : en effet master, la liste affiche « filter » après un
redémarrage, mais le son et les réglages restent ceux de drive.

## Revenir à op1r ou au firmware d'origine

Un son qui utilise drive garde `"fx_type": "drive"`. Un firmware qui ne connaît pas drive (op1r seul, ou l'original)
s'arrêtera alors **au démarrage** sur l'assertion `fx_types.cpp:42 unknown fx name`, comme pour « roll ». Avant de
flasher une autre image, choisir un autre effet sur **chaque son** (slots synthé et batterie) qui utilise drive. Le
master n'est pas concerné (il est enregistré comme « filter »). Pour le piano roll, la même règle s'applique (voir [../pianoroll/README.md](../pianoroll/README.md)).

## Comment c'est branché

Exactement comme les effets d'op1r (tape, clouds, reverb), sur l'effet d'origine caché « filter » (id interne 5) :

| Point | Quoi |
|---|---|
| `op1_factory.db` | ligne `fx_types` « drive » ; ses paramètres par défaut portent le marqueur `0x5304` en 5e valeur (op1r utilise `0x5301` à `0x5303`) |
| JUMP.L `019aad8e` | comparaison de noms des effets : « drive » vaut « filter », sinon la fonction d'op1r |
| mots `01910ca8/b0`, `01910cc8/d0` | `process` et `set_position` du filter (mono, master stéréo) : drive si le marqueur est le sien, sinon l'aiguillage d'op1r |
| mot `018f7ff8` | `case` 5 de ScreenId fx : notre écran (id `0xb`, celui des effets d'op1r) si l'effet courant est drive |
| CALL `01a3bd58`, `019f07d2`, `019c2d3e`, `019c2e94` | construction et init des écrans, touches, encodeurs, enchaînés drive → piano roll → op1r |

`build.sh` épingle le bloc de code d'op1r 12 par son sha256 (drive appelle ses fonctions internes) et vérifie
chaque site avant de construire.

## Fichiers

| Fichier | Rôle |
|---|---|
| `dsp.h` | le cœur audio, en virgule fixe (modes, filtre à variable d'état suréchantillonné 2×, mix, niveau) |
| `ui.h`, `screen.h` | l'écran : police vectorielle, mise en page, courbe, ondes, réglages |
| `drive.c` | les hooks : nom, audio, écran, touches, encodeurs |
| `drive.ld` | lien avec le piano roll à `01e80000`, adresses R246 et op1r 12 |
| `tables.h`, `gen_tables.py` | tables (sinus, drive, fréquence, niveau), générées |
| `test_host.c` | test du DSP sur PC sous UBSan : toutes les combinaisons de réglages, pas de débordement |
| `test_hooks.c` | test des hooks sur PC contre un firmware simulé |
| `build.sh` | vérifie op1r, compile roll + drive, lie, puis `tools/fwbuild.py` |
