# OP-1 custom firmware patcher (R246)

Turns the original Teenage Engineering OP-1 firmware **R246** (`op1_246.op1`) into a custom firmware
with:

- custom synth engines with their own screens: **supersaw** (chainsaw), **fmbell** (bell), **granular**
- animated **lab screen for the stock iter synth**: gauges follow the knobs; bubbles, flame, falling dashes, voice
  dots and a note display react to the knobs and to what you play (graphic: `iter-lab.svg` from
  [op1repacker](https://github.com/op1hacks/op1repacker), MIT License, © 2016 op1hacks)
- boot screen shows `op1r <version>` under the TE logo
- custom FX with their own screens, usable on instruments **and as master FX** (mixer):
  - **tape**: vintage tape echo (time with tape-speed pitch bends, feedback up to self-oscillation, wow &
    flutter, age)
  - **clouds**: granular texture FX, a port of Mutable Instruments Clouds (position, size, pitch, density;
    encoder presses switch to a second page, red press freezes)
- **quad** LFO: four LFOs in one slot, shown as four lanes over the full screen
  - blue frequency (0.05–20 Hz), green amount (bipolar), white target, red destination
  - works on synths and drums; targets: engine knob, envelope (synth ADSR, drum envelope) or FX knob
  - each LFO runs **per voice** (locked to the notes, like the stock LFOs) or **free-running** (white dot next
    to the target)
  - press an encoder to select LFO 1–4
- **euclid** sequencer, an extra sequencer next to the stock ones (pick it in the sequencer list), for synth and drums
  - 6 sequencer slots, each playing one note; **Shift + white** selects the slot, a note key on the euclid screen
    assigns its note, at the octave of that moment
  - blue steps, green pulses, white rotation, red start/stop (**Shift + red**: all slots); press blue/green/white for
    page 2: rate (1/4 … 1/32, triplets), gate, octave
  - **Shift + G A B C D E** (the last six white keys) mutes/unmutes slots 1–6, **Shift + D#** mutes/unmutes all;
    a muted slot keeps running (dimmed dots) but plays nothing; works from any screen
  - runs at the OP-1 tempo, plays polyphonically and sends MIDI; slot settings and mutes are saved (slots come
    back stopped)
- hidden **OP-Z controller mode** in COM → CTRL, switched on by the SysEx `F0 00 20 76 00 01 03 F7`
  from the host (any MIDI tool that can send SysEx; `F0 00 20 76 00 01 00 F7` switches it off)

## Screens

Rendered on the PC from the same screen code that runs on the OP-1 (host harness in the synth SDK).

| Synth engines | |
|---|---|
| ![supersaw](docs/images/synth_supersaw.png) **supersaw** | ![fmbell](docs/images/synth_fmbell.png) **fmbell** |
| ![granular](docs/images/synth_granular.png) **granular** | ![iter lab](docs/images/synth_iterlab.png) **iter** (lab screen) |

| Effects | |
|---|---|
| ![tape](docs/images/fx_tape.png) **tape** echo | ![clouds](docs/images/fx_clouds.png) **clouds** |

| LFO | Sequencer |
|---|---|
| ![quad LFO](docs/images/lfo_quad.png) **quad** LFO | ![euclid](docs/images/seq_euclid.png) **euclid** sequencer |

The repository contains no Teenage Engineering firmware. You provide the original update file, and the
patcher applies only the differences stored in `data/`. These are our code, our screen SVGs and small
byte edits.

## Usage

Requirements: Python 3.8 or newer, standard library only.

```sh
git clone https://github.com/pcppcp/op1r.git && cd op1r
python3 patch_op1.py /path/to/op1_246.op1            # -> /path/to/op1_246-custom.op1
python3 patch_op1.py /path/to/op1_246.op1 -o op1-custom.op1
python3 patch_op1.py --info                          # what the patch contains + checksums
```

The patcher:
1. Checks the input SHA-256 against the original R246 image and refuses anything else.
2. Unpacks it (CRC-32 → LZMA → tar).
3. Checks every file it touches, both before and after patching (SHA-256 per file).
4. Repacks deterministically: original tar order and metadata, op1repacker's LZMA settings.
5. Checks the result against the reference build's SHA-256.

If only the last step differs while the archive content matched, your Python has a different liblzma
version. The firmware content is still verified identical, and `--accept-recompressed` writes it anyway.

| | SHA-256 |
|---|---|
| input `op1_246.op1` (original R246) | `c5315218f825f143b415ca554516541898abee843d3a236df0b54c04e1fb13a9` |
| output (reference build) | `6dfdb643b2c8337304e2ff226e0c50f6b77aa20a56c5f815b3ffee6d1a5ea40e` (also `python3 patch_op1.py --info`) |

Install the result like any OP-1 update (bootloader update mode). The bootloader is not modified, so the
original `op1_246.op1` can always be flashed back. **Use at your own risk.**

Original firmware files (`*.op1`, `*.ldr`, unpacked trees) are git-ignored and must never be committed.

See [CHANGELOG.md](CHANGELOG.md) for what changed between versions.

## Maintainers: rebuilding the patch data

The patch data is generated from a custom firmware tree built with the synth SDK (`make_firmware.py`,
not part of this repository; needs the Blackfin toolchain):

```sh
python3 <sdk>/synth_sdk/make_firmware.py --opz                    # builds <sdk>/op1_246_custom/
python3 build_patch.py /path/to/op1_246.op1 <sdk>/op1_246_custom  # regenerates data/ + reference image
```

`build_patch.py` diffs the custom tree against the original image:
- **Changed files** (`OP1_vdk.ldr`, `content/op1_factory.db`): stored as byte edits plus one insertion,
  the new code block.
- **New files** (our SVGs): stored whole.
- **New files identical to a stock file**, such as `fxgeneric.svg` (= `iter.svg`): recorded as `copy_of` and
  recreated from the user's image.

It then runs the patcher once. It writes the reference image (`<custom tree>-patched.op1`, or
`--reference <file>`) and its checksums to `data/manifest.json`, and checks that the reference unpacks to exactly the custom tree.
Flash-test the reference image before publishing a new patch, and commit `data/` together with the
updated output checksum in this README.
