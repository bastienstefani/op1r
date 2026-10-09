# OP-1 custom firmware patcher (R246)

Turns the original Teenage Engineering OP-1 firmware **R246** (`op1_246.op1`) into the custom firmware
built with `synth_sdk/` in this repository:

- custom synth engines with their own screens: **supersaw** (chainsaw), **fmbell** (bell), **granular**
- **tape** echo FX with its own screen (vintage tape echo)
- hidden **OP-Z controller mode** in COM → CTRL, switched on by the SysEx `F0 00 20 76 00 01 03 F7`
  (see `tools/opz_mode_test.py`)

The repository contains no Teenage Engineering firmware. You provide the original update file, and the
patcher applies only the differences stored in `data/`. These are our code, our screen SVGs and small
byte edits.

## Usage

Requirements: Python 3.8 or newer, standard library only.

```sh
git clone <this repo> && cd <repo>
python3 patcher/patch_op1.py /path/to/op1_246.op1            # -> /path/to/op1_246-custom.op1
python3 patcher/patch_op1.py /path/to/op1_246.op1 -o op1-custom.op1
python3 patcher/patch_op1.py --info                          # what the patch contains + checksums
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
| output (reference build) | see `python3 patcher/patch_op1.py --info` |

Install the result like any OP-1 update (bootloader update mode). The bootloader is not modified, so the
original `op1_246.op1` can always be flashed back. **Use at your own risk.**

## Maintainers: rebuilding the patch data

```sh
python3 synth_sdk/make_firmware.py --opz                  # builds op1_246_custom/ (needs the toolchain)
python3 patcher/build_patch.py op1_246.op1 op1_246_custom # regenerates patcher/data/ + reference image
```

`build_patch.py` diffs the custom tree against the original image:
- **Changed files** (`OP1_vdk.ldr`, `content/op1_factory.db`): stored as byte edits plus one insertion,
  the new code block.
- **New files** (our SVGs): stored whole.
- **New files identical to a stock file**, such as `fxgeneric.svg` (= `iter.svg`): recorded as `copy_of` and
  recreated from the user's image.

It then runs the patcher once. It writes the reference image (`op1_246_custom-patched.op1`) and its
checksums to `data/manifest.json`, and checks that the reference unpacks to exactly the custom tree.
Flash-test the reference image before publishing a new patch.
