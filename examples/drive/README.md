# Exemple : effet de distorsion « drive »

Distorsion pour l'OP-1 (R246), ajoutée **par-dessus op1r 12 et le piano roll** comme nouvelle entrée « drive » de la
liste des effets. Le fonctionnement s'inspire du module Distortion de Serum (modes de waveshaping, drive, filtre
avant ou après, mix). L'écran reprend son affichage (courbe de transfert, forme d'onde déformée) dans le langage
visuel de l'OP-1 : fond noir, traits fins, une couleur par encodeur. Le firmware d'origine et l'image op1r se
reflashent toujours : le bootloader n'est pas modifié.

Pour reprendre le développement (architecture, chaîne des hooks, mise à jour d'op1r), voir
[docs/developpement.md](../../docs/developpement.md).

## État

| Fonction | État |
|---|---|
| Entrée « drive », écran, son | flashé sur op1r 12 puis sur op1_249 : premiers retours bons |
| Détail des modes, page 2 (filtre, résonance, niveau), sauvegarde avec le son, master, batterie | à confirmer un par un (liste ci-dessous) |
| Modulation par LFO, course des encodeurs d'origine (drive, fréquence, mix) | non vérifiées |
| DSP, hooks, écran | testés sur PC (`examples/run_tests.sh`) |

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

## Sur un autre firmware construit sur op1r

`build.sh` peut partir d'une image publiée par quelqu'un d'autre, si elle est construite sur op1r 12, au lieu
d'appliquer op1r lui-même :

```sh
examples/drive/build.sh op1_246.op1 op1r-249-drive.op1 op1_249.op1
```

Seules les images listées dans `build.sh` sont acceptées (vérifiées par leur sha256). Pour chacune, on a vérifié :

- que son bloc de code op1r est celui d'op1r 12, octet pour octet ;
- qu'elle ne modifie aucun des points que roll et drive détournent, sauf les touches et les encodeurs ;
- que ses hooks touches / encodeurs rappellent ceux d'op1r : la chaîne devient drive → roll → l'image → op1r ;
- quelle mémoire elle occupe : roll et drive sont chargés à `01ee0000`, au-dessus.

Aucun octet de ces images n'est copié dans le dépôt : l'image sert d'entrée, comme le firmware d'origine.

| Image | Construite sur | Ce qu'elle ajoute (d'après ses chaînes) | Mémoire | Démarrage |
|---|---|---|---|---|
| `op1_249.op1` (maikopruett.com, `b7adf5c0…`) | op1r 12 (`fc1ae37`) | undo / redo de la bande, quantification sur une gamme (`scale.cfg`), noms des snapshots, journaux dans `/log` | `01e40000-01eb7464`, `02800000-02980000` | `249+drv` |

## Revenir à op1r ou au firmware d'origine

Un son qui utilise drive garde `"fx_type": "drive"`. Un firmware qui ne connaît pas drive (op1r seul, ou l'original)
s'arrêtera alors **au démarrage** sur l'assertion `fx_types.cpp:42 unknown fx name`, comme pour « roll ». Avant de
flasher une autre image, choisir un autre effet sur **chaque son** (slots synthé et batterie) qui utilise drive. Le
master n'est pas concerné (il est enregistré comme « filter »). Pour le piano roll, la même règle s'applique (voir [../pianoroll/README.md](../pianoroll/README.md)).

## Paramètres et état interne

| Paramètre | Rôle | Plage stockée |
|---|---|---|
| 0 | drive (bleu, page 1) | bornes de filter, normalisées par `norm()` |
| 1 | mode (vert, page 1) | idem ; 13 zones, le vert pose le centre d'une zone |
| 2 | fréquence (blanc, page 1) | idem |
| 3 | mix (orange, page 1) | idem |
| 4 | **marqueur `0x5304`** | jamais modifié |
| 5 | routage × type de filtre (9 zones) | 0..32767 |
| 6 | résonance | 0..32767 |
| 7 | niveau | 0..32767 |

- Les valeurs par défaut sont dans la ligne SQL de `build.sh` : drive 30 %, TUBE, 550 Hz, mix ≈ 100 %, filtre
  OFF, résonance 20 %, 0 dB. Les paramètres 0 à 3 restent à l'intérieur des bornes de secours d'op1r.
- Il y a quatre états DSP : synth, batterie, master gauche et droite. Ils sont remis à zéro à chaque
  `set_position`.
- La crête de chaque bloc remonte vers l'écran pour le point blanc.
- La page affichée (`dv_page`) est globale.

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
| `drive.ld` | lien avec le piano roll à `01ee0000`, adresses R246 et op1r 12 |
| `tables.h`, `gen_tables.py` | tables (sinus, drive, fréquence, niveau), générées |
| `test_host.c` | test du DSP sur PC sous UBSan : toutes les combinaisons de réglages, pas de débordement |
| `test_hooks.c` | test des hooks sur PC contre un firmware simulé (`-DDV_HOST`) |
| `test_ui.c`, `render_ui.py` | aperçu de l'écran sur PC, dessiné avec le même code (PNG, Pillow) |
| `build.sh` | vérifie op1r (ou la base), compile roll + drive, lie, puis `tools/fwbuild.py` |

Tous les tests sur PC : `examples/run_tests.sh`.
