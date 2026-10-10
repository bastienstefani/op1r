# Changelog

Newest first. Each entry lists the changes since the previous push and the output checksum of
`patch_op1.py` for that version. The input is always the original R246 image.

## 2026-10-10 10:30

Output `a8bc18bc27fa35ae08c42f0eccf33a8d582c7c5fea218f0d02d5aeb158aeaccf`

### Added
- **euclid** sequencer: an extra entry in the sequencer list (finger and the other stock sequencers are unchanged),
  for synth and drums.
  - 6 slots per mode, each a euclidean rhythm on one note. Shift + white selects the slot; a note key pressed on the
    euclid screen assigns its note at the current octave (later octave changes keep the slot's pitch, also on drums).
  - Encoders: steps, pulses, rotation (clockwise), red start/stop; Shift + red starts/stops all slots with a note.
    Encoder presses switch blue/green/white to rate, gate and octave.
  - Locked to the OP-1 tempo clock, polyphonic, sends MIDI notes. The left/right buttons change the keyboard octave
    as usual. Settings are live only (not saved yet).

## 2026-10-10 08:10 — v1.0

Output `b720d371722f1c48850358dc5eebba13602155e142f5f2ea30480389d4caf557`

### Added
- Quad LFO modulates **drums**: on the drum page, the synth target moves the drum engine knobs and the fx target
  moves the drum FX knobs, just like on the synth. Synth and drum LFOs run independently.

### Changed
- Drum page: the white encoder skips the envelope targets (drums have no envelope to modulate). Voice-mode LFOs
  on drums advance every block, so they behave like free-running ones.

## 2026-10-10 07:45

Output `c19c52e44440ef5b6930fbfab4a1f718e1f3e76db7e591e89ddf19dc92254926`

### Added
- Custom FX (tape, clouds) work as **master FX** in the mixer. The effect processes the mono sum and is added to
  both channels, so the dry stereo image is kept. Master and instrument instances keep separate state, so the
  same FX can run on an instrument and on the master at the same time.

### Fixed
- Selecting the filter as master FX while an instrument used a custom FX showed the instrument's FX screen,
  and the master ran the plain filter instead of the custom FX.

### Changed
- README: feature list rewritten (how to use tape, clouds and quad LFO); this changelog added.

## 2026-10-09 20:46

Output `138b6aa6a2e5ef055825b0160f5d42dd9a298a92d49609ce3b949a9e0a3f47cc`

### Added
- **clouds** granular FX (port of Mutable Instruments Clouds) with its own screen. Encoder presses switch to a
  second page; the red press freezes.
- **quad** LFO: four LFOs in one slot (frequency, amount, target, destination), full-screen page. Press an
  encoder to select LFO 1–4.
  - Targets: synth knob, envelope A/D/S/R, FX knob.
  - Each LFO runs per **voice** or **free-running** (white dot).

### Fixed
- "patch.cpp:390 Invalid synth patch … error 27" when loading patches that use the quad LFO.
- Quad LFO with FX targets had no effect.
- Tape echo screen synced to the audio clock. Wow depth increased.

## 2026-10-09 12:56

Output `c7031a7b1ad801e2b32a1d9c805802000641f5691a93ce302323a7952234d2bf`

### Fixed
- Tape echo could store a knob value outside the allowed range, which locked up the OP-1 ("Instr fx param out
  of range") on every boot. Bad stored values are now repaired automatically.

## 2026-10-09 11:58 — initial release

- Custom synth engines with their own screens: supersaw (chainsaw), fmbell (bell), granular.
- Tape echo FX with its own screen.
- Hidden OP-Z controller mode in COM → CTRL (SysEx `F0 00 20 76 00 01 03 F7` on, `… 00 F7` off).
- Patcher: checksum-verified, deterministic repack, contains no Teenage Engineering firmware.
