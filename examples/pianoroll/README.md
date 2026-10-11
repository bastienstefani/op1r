# Exemple : séquenceur piano roll (en construction)

Séquenceur piano roll ajouté à l'OP-1 (R246), **par-dessus op1r**, comme nouvelle entrée « roll » dans la liste
des séquenceurs. Exemple d'étude pour [docs/firmware-r246.md](../../docs/firmware-r246.md). **Non testé sur
matériel.** Le firmware d'origine et l'image op1r se reflashent toujours (le bootloader n'est pas modifié).

Greffé sur l'arpeggio d'origine (id interne 5, écran 0x2c), exactement comme op1r greffe euclid sur finger.
Chaque hook s'enchaîne derrière celui d'op1r, donc euclid continue de fonctionner. Il n'y a pas de collision
entre « roll » et euclid (id, drapeau, écran et table distincts).

## Étapes

- Étape 1 (fait, testée sur matériel) : « roll » dans la liste, écran avec grille et tête de lecture, motif de
  démo en boucle.
- **Étape 2 (ici)** : vrai modèle de notes avec lecture **polyphonique** et **édition**. Chaque note garde sa
  propre position et sa propre durée ; la longueur de boucle est réglable. **Pas encore** : la sauvegarde (le
  motif est perdu à l'extinction).
- Étape 3 : sauvegarde par mode dans `op1.db`, comme euclid.

## Commandes (étape 2)

Sur l'écran roll (séquenceur roll sélectionné) :

| | Rotation | Appui |
|---|---|---|
| **Bleu** | déplace le curseur dans le temps (par pas de grille) | change la grille (1/4 → 1/8 → 1/16 → 1/32) |
| **Vert** | fait défiler les hauteurs affichées | — |
| **Blanc** | durée des nouvelles notes | change la vélocité (127 / 100 / 64) |
| **Rouge** | longueur de la boucle | efface les notes sous le curseur |

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

## À regarder pour tes retours (étape 2)

- Poser des notes au clavier : apparaissent-elles au bon endroit, à la bonne hauteur, avec la bonne durée ?
- Le curseur (bleu), la grille (appui bleu), la durée (blanc), la longueur de boucle (rouge) répondent-ils comme
  attendu ? Le défilement des hauteurs (vert) ?
- La lecture est-elle bien polyphonique (plusieurs notes en même temps) et au bon tempo ? Pas de note bloquée en
  bouclant, en coupant, ou en changeant de séquenceur ?
- Shift + touche efface-t-il la bonne note ? L'appui rouge efface-t-il la colonne du curseur ?
- L'en-tête (grille / durée / longueur) est-il lisible et juste ?
- Euclid et les séquenceurs d'origine marchent-ils toujours ?

Limites connues de l'étape 2 : pas encore de sauvegarde (motif perdu à l'extinction) ; changer l'octave pendant
la lecture peut laisser une note bloquée (comme l'euclid d'op1r).

## Fichiers

| Fichier | Rôle |
|---|---|
| `roll.c` | le séquenceur : hooks d'intégration, lecteur (vtable 7 méthodes), écran (dessin par code) |
| `fw.ld` | lien à `01e80000`, adresses du firmware R246 et des hooks op1r 5 |
| `roll.svg` | écran presque vide (grille et notes dessinées par le code) |
| `build.sh` | vérifie les cibles op1r, compile, lie, puis `tools/fwbuild.py` (blob + hooks + ligne SQL + SVG) |
