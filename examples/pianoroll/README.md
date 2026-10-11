# Exemple : séquenceur piano roll (en construction)

Séquenceur piano roll ajouté à l'OP-1 (R246), **par-dessus op1r**, comme nouvelle entrée « roll » dans la liste
des séquenceurs. Exemple d'étude pour [docs/firmware-r246.md](../../docs/firmware-r246.md). **Non testé sur
matériel.** Le firmware d'origine et l'image op1r se reflashent toujours (le bootloader n'est pas modifié).

Greffé sur l'arpeggio d'origine (id interne 5, écran 0x2c), exactement comme op1r greffe euclid sur finger.
Chaque hook s'enchaîne derrière celui d'op1r, donc euclid continue de fonctionner. Il n'y a pas de collision
entre « roll » et euclid (id, drapeau, écran et table distincts).

**Construit sur op1r 10.** Les adresses des hooks d'op1r sont re-dérivées de l'image op1r 10 (le bloc de code
d'op1r change à chaque version) ; `build.sh` les vérifie et refuse de construire si elles ne correspondent plus.

## Étapes

- Étape 1 (fait, testée sur matériel) : « roll » dans la liste, écran avec grille et tête de lecture, motif de
  démo en boucle.
- Étape 2 (fait, testée sur matériel) : modèle de notes, lecture **polyphonique**, **édition** (chaque note a sa
  position et sa durée ; longueur de boucle réglable).
- **Étape 3 (ici)** : **sauvegarde**. Le motif, les réglages et la longueur de boucle de chaque mode sont écrits
  dans la table `rollseq` d'`op1.db`, en tâche de fond (comme euclid), et rechargés au démarrage.

## Commandes (étape 2)

Sur l'écran roll (séquenceur roll sélectionné) :

| | Rotation | Appui |
|---|---|---|
| **Bleu** | déplace le curseur dans le temps (par pas de grille) | change la grille (1/4 → 1/8 → 1/16 → 1/32) |
| **Vert** | fait défiler les hauteurs affichées | efface les notes sous le curseur |
| **Blanc** | durée des nouvelles notes | change la vélocité (127 / 100 / 64) |
| **Orange** | longueur de la boucle | **lecture / arrêt** |

- **HOLD** (encadré en haut à droite) : rouge atténué à l'arrêt, rouge vif en lecture. Le roll démarre à l'arrêt ;
  tout arrêt externe (séquenceur coupé, changement de séquenceur ou de mode) le remet sur HOLD. La lecture repart
  toujours du début de la boucle ; la tête de lecture n'est affichée qu'en lecture.

- **Touche du clavier** : pose une note de cette hauteur au curseur, avec la durée courante, puis avance le
  curseur (saisie pas à pas). La note sonne en même temps.
- **Shift + touche** : efface la note de cette hauteur sous le curseur.
- L'octave du clavier (flèches gauche/droite) décale la hauteur posée, comme d'habitude.

En-tête (police vectorielle d'op1r, chiffres et quelques symboles) : grille, durée de note, longueur de boucle
en nombre de pas.

## Essai

1. Construire et flasher (bootloader en mode mise à jour) :
   ```sh
   tools/build_bfin_toolchain.sh && export PATH=$PWD/work/bfin/bin:$PATH   # une fois
   examples/pianoroll/build.sh op1_246.op1 op1r-roll.op1
   ```
   L'écran de démarrage affiche `op1rroll` : c'est le repère que cette image tourne.
2. Passer en mode séquenceur, ouvrir la liste des séquenceurs, choisir **roll** (entre pattern et sketch, la
   liste est alphabétique).
3. Lancer le séquenceur : le motif de démo tourne en boucle. L'écran montre la grille et la tête de lecture.
   Fonctionne en mode synth comme en mode drum (motif différent).

## À regarder pour tes retours (étape 3)

- **Sauvegarde** : pose quelques notes, règle la boucle, éteins puis rallume l'OP-1 → le motif revient-il, en
  synth et en drum ? (Laisse quelques secondes après la dernière édition avant d'éteindre : la sauvegarde se fait
  en tâche de fond.)
- Tout ce qui marchait à l'étape 2 (édition, lecture polyphonique, encodeurs, pas de note bloquée) tient-il
  toujours ?
- Euclid et les séquenceurs d'origine marchent-ils toujours (y compris leur propre sauvegarde) ?
- Rien d'anormal au démarrage (c'est là que le motif est rechargé depuis la base) ?

Limite connue : changer l'octave pendant la lecture peut laisser une note bloquée (comme l'euclid d'op1r).

## Revenir à op1r ou au firmware d'origine

Quand « roll » est le séquenceur choisi, son nom est enregistré dans `op1.db` (table `sequencer`). Un firmware
qui ne connaît pas « roll » (op1r seul, ou l'original) s'arrête alors **au démarrage** sur l'assertion
`SeqIds.cpp:41 unknown sequencer name: 'roll'`. Avant de flasher une autre image :

1. dans l'image roll, choisir un autre séquenceur (pattern, finger, euclid…) **en mode synth et en mode drum** ;
2. puis flasher l'autre image.

Si c'est trop tard (l'OP-1 s'arrête au démarrage), reflasher l'image roll (le mode mise à jour du bootloader ne
dépend pas du système) et faire l'étape 1. En dernier recours, le « factory reset » du bootloader remet `op1.db`
à zéro (efface les données utilisateur).

## Fichiers

| Fichier | Rôle |
|---|---|
| `roll.c` | le séquenceur : hooks d'intégration, lecteur (vtable 7 méthodes), écran (dessin par code) |
| `fw.ld` | lien à `01e80000`, adresses du firmware R246 et des hooks op1r 5 |
| `roll.svg` | écran presque vide (grille et notes dessinées par le code) |
| `build.sh` | vérifie les cibles op1r, compile, lie, puis `tools/fwbuild.py` (blob + hooks + ligne SQL + SVG) |
