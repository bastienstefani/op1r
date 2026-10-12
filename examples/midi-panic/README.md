# Exemple : MIDI panic (Shift + touche → All Notes Off)

Exemple d'étude pour [docs/firmware-r246.md](../../docs/firmware-r246.md), section 11. **Non testé sur
matériel.** L'`op1_246.op1` d'origine se reflashe toujours (le bootloader n'est pas modifié).

**Shift + F du milieu du clavier** (code de touche `0x1f`) envoie sur le canal MIDI courant :

- CC 120 (All Sound Off) ;
- CC 123 (All Notes Off).

Les autres touches et combinaisons ne changent pas.

| Fichier | Rôle |
|---|---|
| `panic.c` | `key_hook(key, pressed)` : intercepte Shift + `PANIC_KEY`, passe tout le reste à la cible précédente |
| `fw.ld` | lien à `01e80000` (fenêtre libre de la SDRAM), adresses du firmware R246 (`fw_midi_cc` = `019e3004`, `fw_shift_held` = `01dd7f78`) |
| `build.sh` | compile, lit la cible actuelle du `CALL` de `019c2d3e` dans l'image de base, lie, puis appelle `tools/fwbuild.py` |

```sh
tools/build_bfin_toolchain.sh && export PATH=$PWD/work/bfin/bin:$PATH
examples/midi-panic/build.sh op1_246.op1 op1_246-panic.op1            # firmware d'origine
examples/midi-panic/build.sh op1_246.op1 op1r-panic.op1 --op1r        # par-dessus op1r (chaîné à son hook)
PANIC_KEY=0x35 examples/midi-panic/build.sh op1_246.op1 out.op1       # autre touche (E aigu)
```

Le texte de démarrage devient `R246 pnc` (ou `op1r5pnc`), pour voir d'un coup d'œil quelle image tourne. Le
listing du code généré est écrit à côté de l'image (`*.dis`).

Codes de touches : voir la section 8.6 du document. Avec `--op1r`, évitez les touches qu'op1r prend avec Shift :
les quatre premières blanches, les six dernières blanches, C# et D#.
