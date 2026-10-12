# Exemple : séquenceur piano roll « roll »

Séquenceur piano roll ajouté à l'OP-1 (R246) **par-dessus op1r 12**, comme nouvelle entrée « roll » dans la liste
des séquenceurs. Chaque note a sa position et sa propre durée, et la boucle a une longueur réglable. Le roll joue
en polyphonie, se sauve et démarre calé sur le temps. Le firmware d'origine et l'image op1r se reflashent
toujours : le bootloader n'est pas modifié.

Pour reprendre le développement (architecture, chaîne des hooks, mise à jour d'op1r), voir
[docs/developpement.md](../../docs/developpement.md).

## État

| Fonction | État |
|---|---|
| Entrée « roll » dans la liste, écran grille + tête de lecture | testé sur matériel |
| Notes (position, durée, hauteur, vélocité), lecture polyphonique, édition | testé sur matériel |
| Sauvegarde (table `rollseq` d'`op1.db`) et rechargement au démarrage | testé sur matériel |
| Lecture / arrêt par l'appui orange, encadré HOLD | testé sur matériel |
| Départ calé sur le prochain temps | testé sur matériel (« ça marche nickel ») |
| Reconstruction sur op1r 12, et sur op1_249 avec drive | flashé, fonctionne |

## Commandes

Sur l'écran roll (séquenceur roll sélectionné) :

| | Rotation | Appui |
|---|---|---|
| **Bleu** | déplace le curseur dans le temps (par pas de grille) | change la grille (1/4 → 1/8 → 1/16 → 1/32) |
| **Vert** | fait défiler les hauteurs affichées | efface les notes sous le curseur |
| **Blanc** | durée des nouvelles notes | change la vélocité (127 / 100 / 64) |
| **Orange** | longueur de la boucle | **lecture / arrêt** |

- **Touche du clavier** : pose une note de cette hauteur au curseur, avec la durée courante, puis avance le
  curseur (saisie pas à pas). La note sonne en même temps.
- **Shift + touche** : efface la note de cette hauteur sous le curseur.
- **Octave** : les flèches gauche/droite décalent la hauteur posée, comme d'habitude.
- **HOLD**, l'encadré en haut à droite :
  - il est rouge atténué à l'arrêt et rouge vif en lecture ;
  - un arrêt externe (séquenceur coupé, changement de séquenceur ou de mode) remet le roll sur HOLD ;
  - la lecture repart toujours du début de la boucle.
- **Départ calé sur le temps** :
  - l'appui orange arme la lecture, et le roll démarre sur le prochain temps (noire) de l'horloge du séquenceur ;
  - le temps 1 est fixé par la dernière remise à zéro de cette horloge : PLAY de la bande, Start MIDI ou départ
    d'un séquenceur d'origine ;
  - l'alignement sur le clic du métronome n'est garanti qu'après un Start MIDI, car le métronome est dans la
    partie audio encodée.
- **En-tête** : grille, durée des nouvelles notes (en fraction de ronde), longueur de la boucle en pas.

Le roll démarre vide sur une boucle d'une mesure de 16 pas. Il marche en mode synth comme en mode drum, avec un
motif par mode.

## Construire et essayer

```sh
tools/build_bfin_toolchain.sh && export PATH=$PWD/work/bfin/bin:$PATH   # une fois
examples/pianoroll/build.sh op1_246.op1 op1r-roll.op1                    # roll seul sur op1r 12
examples/drive/build.sh     op1_246.op1 op1r-drive.op1                   # roll + drive (voir ../drive)
examples/run_tests.sh                                                    # tests sur PC (dont le calage)
```

L'écran de démarrage affiche `op1rroll` pour la première image. Pour essayer :

1. Passer en mode séquenceur et choisir **roll** dans la liste (alphabétique).
2. Poser quelques notes au clavier, puis appuyer sur orange.

## Comment c'est branché

Il est greffé sur l'arpeggio d'origine (id interne 5, écran `0x2c`), comme op1r greffe euclid et dots sur finger.
Chaque hook traite ce qui concerne roll et passe le reste au hook précédent (op1r, ou la base).

| Site | Rôle |
|---|---|
| CALL `01a42a2e`, `01a42bdc`, `01a42ce0` (`idFromName`) | « roll » → id 5 et drapeau `roll_flag[mode]` ; op1r reçoit « arpeggio » (jamais « roll ») |
| JUMP.L `01a422ec` (`nameFromId`) | id 5 avec le drapeau → « roll » |
| CALL `01a0d1b0` (instance de l'arpeggio) | renvoie le lecteur roll quand le drapeau est mis |
| JUMP.L `01a5e7d0` (remise à zéro de l'horloge) | copie des trois écritures d'origine + drapeau pour le calage |
| CALL `01a3bd58` / `019f07d2` (écrans) | construit l'écran roll (`roll.svg`), échange l'emplacement `0x2c` du registre |
| CALL `019c2d3e` / `019c2e94` (touches, encodeurs) | appuis et rotations sur l'écran roll ; le reste passe au suivant |
| `op1_factory.db` | `INSERT INTO seq_types VALUES (9, 'roll')` (op1r 11 a pris l'id 8 pour dots) |

**Lecteur** : `roll_player_vt`, la vtable de 7 méthodes décrite dans
[firmware-r246.md](../../docs/firmware-r246.md) §8.5.
- `tick(self, tick48)` est appelé 96 fois par noire avec le compteur modulo 48 ; `beat.h` en déduit les temps.
- Les notes partent par `fw_note_on` / `fw_note_off`, avec 16 voix suivies et coupées en fin de note.

**Sauvegarde** :
- La table est `rollseq (is_synth INTEGER PRIMARY KEY, data TEXT)`, avec une ligne par mode. La clé vaut **0 pour
  le synthé et 1 pour la batterie** ; le nom `is_synth` est trompeur, mais il est utilisé tel quel à l'écriture
  comme à la lecture.
- `data` = `boucle,grille,vélocité,haut_de_vue,durée,{début,durée,hauteur,vélocité}*`, en décimal.
- Elle est écrite en tâche de fond par `bg_saver`, comme euclid. Pas de `%` ni de `'` dans les données : `db2`
  passe le SQL deux fois par `vsnprintf`. La taille reste sous 8 Ko.

`build.sh` vérifie que chaque site contient encore la cible attendue (op1r 12) et que `01a5e7d0` est intact, puis
refuse de construire si quelque chose a bougé.

## Limites connues

- Changer l'octave pendant la lecture peut laisser une note bloquée (comme euclid).
- L'état lecture / HOLD n'est pas sauvé : le roll redémarre sur HOLD.

## Revenir à op1r ou au firmware d'origine

Quand « roll » est le séquenceur choisi, son nom est enregistré dans `op1.db` (table `sequencer`). Un firmware
qui ne connaît pas « roll » (op1r seul, ou l'original) s'arrête alors **au démarrage** sur l'assertion
`SeqIds.cpp:41 unknown sequencer name: 'roll'`. Avant de flasher une autre image :

1. dans l'image roll, choisir un autre séquenceur (pattern, finger, euclid…) **en mode synth et en mode drum** ;
2. puis flasher l'autre image.

Si c'est trop tard (l'OP-1 s'arrête au démarrage), reflasher l'image roll et faire l'étape 1 : le mode mise à jour
du bootloader ne dépend pas du système. En dernier recours, le « factory reset » du bootloader remet `op1.db` à
zéro, ce qui efface les données utilisateur.

## Fichiers

| Fichier | Rôle |
|---|---|
| `roll.c` | le séquenceur : hooks, lecteur, écran (dessin par code), sauvegarde, remise à zéro de l'horloge (asm) |
| `beat.h` | calage sur le temps (`clk_on_beat`), partagé avec le test |
| `test_beat.c` | test du calage sur PC (lancé par `examples/run_tests.sh`) |
| `fw.ld` | lien à `01ee0000` ; adresses R246 (section « stock ») et hooks op1r 12 ; maillon suivant des touches et encodeurs |
| `roll.svg` | fond noir (la grille et les notes sont dessinées par le code) |
| `build.sh` | vérifie les cibles op1r et l'horloge d'origine, compile, lie, puis `tools/fwbuild.py` |
