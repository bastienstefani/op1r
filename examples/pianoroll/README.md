# Exemple : séquenceur piano roll (en construction)

Séquenceur piano roll ajouté à l'OP-1 (R246), **par-dessus op1r**, comme nouvelle entrée « roll » dans la liste
des séquenceurs. Exemple d'étude pour [docs/firmware-r246.md](../../docs/firmware-r246.md). **Non testé sur
matériel.** Le firmware d'origine et l'image op1r se reflashent toujours (le bootloader n'est pas modifié).

Greffé sur l'arpeggio d'origine (id interne 5, écran 0x2c), exactement comme op1r greffe euclid sur finger.
Chaque hook s'enchaîne derrière celui d'op1r, donc euclid continue de fonctionner. Il n'y a pas de collision
entre « roll » et euclid (id, drapeau, écran et table distincts).

## Étapes

- **Étape 1 (ici)** : « roll » apparaît dans la liste et est sélectionnable ; un écran affiche une grille
  hauteur × temps avec une tête de lecture rouge qui défile ; un motif de démo d'une mesure tourne en boucle,
  joué sur le moteur interne et envoyé en MIDI, avec extinction propre des notes à l'arrêt / au changement de
  séquenceur. **Pas encore** : édition, longueur de boucle réglable, sauvegarde.
- Étape 2 : édition (curseur, pose/suppression de notes, durée par note, longueur de boucle) aux encodeurs et au
  clavier.
- Étape 3 : sauvegarde par mode dans `op1.db`, comme euclid.

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

## À regarder pour tes retours

- L'appareil démarre-t-il normalement (pas de blocage) et `roll` est-il bien dans la liste ?
- La sélection de `roll` est-elle stable (y compris après un redémarrage) ?
- Le motif de démo joue-t-il en boucle, au bon tempo, et s'arrête-t-il proprement quand on coupe le séquenceur
  ou qu'on change de séquenceur (pas de note bloquée) ?
- L'écran s'affiche-t-il correctement (grille, notes, tête de lecture) ? Lisibilité, position, taille ?
- Euclid et les séquenceurs d'origine marchent-ils toujours ?

Limite connue de l'étape 1 : changer l'octave pendant que la démo joue peut laisser une note bloquée (même
limite que l'euclid d'op1r ; corrigée dans une étape ultérieure).

## Fichiers

| Fichier | Rôle |
|---|---|
| `roll.c` | le séquenceur : hooks d'intégration, lecteur (vtable 7 méthodes), écran (dessin par code) |
| `fw.ld` | lien à `01e80000`, adresses du firmware R246 et des hooks op1r 5 |
| `roll.svg` | écran presque vide (grille et notes dessinées par le code) |
| `build.sh` | vérifie les cibles op1r, compile, lie, puis `tools/fwbuild.py` (blob + hooks + ligne SQL + SVG) |
