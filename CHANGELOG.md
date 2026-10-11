# Changelog

Newest first. Each entry lists the changes since the previous push and the output checksum of
`patch_op1.py` for that version. The input is always the original R246 image.

## 2026-10-11 17:35 — op1r 12

Output `d294dcb3f419848cec66b57bc7f587ea86fd40e03ddd4fe726492804d6a28f2c`

### Added
- dots: **trigger parameters**, edited with white while recording (press white to switch between value and
  parameter): gate 5–100 % or tie, ratchet 1–8, chance 0–100 %, condition (ALL, 1:2 … 4:4), nudge ±12/96 beat,
  ratchet level ramp (off / up / down). New triggers start with defaults; recording over a trigger only changes
  its note.
- dots: **live recording**. The OP-1 REC key switches recording on / off on the dots screen (it no longer arms the
  tape there); pressing red switches the type between REC (step record) and LIVE. LIVE records keys at the playhead
  of the running slot, hard quantised, with the gate from the key hold time (longer than a step = tie); while the
  slot is stopped it records at the cursor.

### Changed
- dots: record mode is no longer toggled by pressing red (REC key instead); `REC` / `LIVE` moved to the top right.
- dots: saved data now includes the trigger parameters; patterns saved with op1r 11 still load.
- Boot screen version: `op1r 12`.

## 2026-10-11 15:29 — op1r 11

Output `b43716c5df59c5eeedfa4b6687bbd2a05945f46fc89955bec91e2bd65ca716f3`

### Added
- **dots** step sequencer (first iteration): 6 slots playing together, up to 64 steps each, shown as rows of
  16 dots. Blue cursor, Shift + white slot, red start/stop, Shift + red / Shift + C# start/stop all. Record mode
  (press red): keys add triggers at the cursor step by step, red = trigger level, Shift + blue erases, green length
  ±1, Shift + green extends to the next full row copying the row above. Mutes, 4 patterns, copy / paste / cut and
  saving as in euclid. Step rate (1/16) and gate are fixed for now.

### Changed
- Boot screen version: `op1r 11`.

## 2026-10-11 11:28 — op1r 10

Output `a3b0b6478dac3398a8fc0bb370aaf41f86e519116c869e63da7e2bd010984758`

### Added
- **MIDI CC control** (USB MIDI, OP-1 MIDI channel):
  - all front-panel keys except the keyboard: CC = key code + 5 (the OP-1 CTRL mode numbering), value ≥ 64 = press,
    < 64 = release, so combinations like REC + PLAY or Shift + key work. Transport: CC 38 REC, 39 PLAY, 40 STOP
  - the four encoders as relative CCs 102–105 (1–63 up, 65–127 down)
  - mixer: CC 106–109 track volume, 110–113 track pan (64 = centre), 114–117 track mute; CC 118 tape input level
  - CC 1–4 still go to the MIDI LFO; encoder presses are not mapped (CC 64 is the sustain pedal)

### Changed
- Boot screen version: `op1r 10`.
- `build_patch.py` refuses local builds (build time on the boot screen); build with `make_firmware.py --opz --release`.

## 2026-10-11 09:20 — op1r 9

Output `6057c2258b75741a69611b6f52cc577279e48be3fcbd3af025ccdc46cc2b64cd`

### Fixed
- quad LFO settings, custom FX page-2 values (clouds pitch, dry/wet, reverb) and the granular sample gain are now
  written into the instrument's patch, so they are saved with it. (Picking another LFO / FX / engine type still
  loads that type's defaults, as on the stock OP-1.)

### Changed
- Boot screen version: `op1r 9`.

## 2026-10-11 08:35 — op1r 8

Output `794934277973cd8a8ad9a3b2ec3f51149a6e15284dfac34793536f487d4da90a`

### Added
- quad LFO: **Shift page** with a table of all four LFOs. Blue frequency (Hz) or tempo division, green smoothing
  (0–7), white waveform (sine, triangle, ramp up, ramp down, square, sample & hold), red tempo sync on/off
  (4/1, 2/1, 1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32).
- Patches saved with earlier versions keep their LFO frequencies (sine, free, no smoothing).

### Changed
- Boot screen version: `op1r 8`.

## 2026-10-10 21:18 — op1r 7

Output `ce6405cfe24c8f9fea06df67a13477fb51f1e6d247a2405b807dd49bbf88174c`

### Added
- granular: the **input** key opens "keyboard to sample" on a granular slot like on the sampler; the recording goes
  into the slot's sample buffer and is saved. The slot stays granular with its knobs (no switch to the sampler).
- granular: **red press** toggles page 2 = sample gain (0–4×, default 1×), stored per slot; the red dial gets a ring
  and shows the gain.

### Changed
- Boot screen version: `op1r 7`.

## 2026-10-10 20:43 — op1r 6

Output `1acdf3f18d1d2ea5e169b6428530ffec0dd8348844e3e0e934f0ec2b81a95bc1`

### Added
- Euclid: **level per slot** (note velocity 1–127, default 100) on page 2 of the red encoder, stored per pattern and
  saved. Always shown as a red bar at the right border (dim above the level); a triangle marks red page 2.
- Euclid: **lift / drop / scissors** on the euclid screen copy / paste / cut the selected slot; with **Shift** the whole
  pattern. Running slots keep running.
- Custom synth engines (supersaw, fmbell, granular) follow note velocity (euclid level, MIDI in).

### Changed
- Boot screen version: `op1r 6`.

## 2026-10-10 18:44 — op1r 5

Output `d4daaea0b7af55f58be545008346f8f54a11abd652945721084b0a6876ab717d`

### Added
- Euclid patterns: 4 per synth/drum, recalled with **Shift + F G A B** (any screen, also the tape view). Each
  pattern stores note, steps, pulses, rotation, rate, gate, octave and mute of all 6 slots and is saved. Switching
  is immediate; playing slots stay in time. P1–P4 is shown at the bottom left of the euclid screen.

### Changed
- Euclid: which slots are playing is global across patterns. Shift + red / Shift + C# start all six slots; a slot
  without a note in the current pattern runs silently and plays when a pattern with a note there is recalled.
- Euclid screen: the red play/stop row is gone; a playing slot shows a red dot in the centre of the big ring
  (and of its small ring, also when it has no note).
- Euclid settings saved by earlier versions load into pattern 1.
- Boot screen version: `op1r 5`.

## 2026-10-10 17:34 — op1r 4

Output `6747a7d67133990d690ac40dd5544c2484812d71fde0f0b7c2a72b4f78fc5213`

### Added
- Euclid: **Shift + C#** starts all slots at once, or stops all if any is running (any screen).
- Euclid: holding **REC** (tape armed) while Shift + C# starts the slots also starts the tape, like REC + PLAY, so
  the recording begins with the first step.

### Changed
- Boot screen version: `op1r 4`.

## 2026-10-10 16:27 — op1r 3

Output `b07eadec568f457325381007fadf0b12c2d74f5873c7307bb04a20832a989c8a`

### Added
- **reverb** FX: port of the Mutable Instruments Elements reverb (MIT, © Emilie Gillet), on instruments and as
  master FX. Blue time (decay, up to near-infinite), green mix, white diffusion, red tone (damping, dark to
  bright). Generic FX screen.

### Changed
- Boot screen version: `op1r 3`.

## 2026-10-10 15:28 — op1r 2

Output `6dfdb643b2c8337304e2ff226e0c50f6b77aa20a56c5f815b3ffee6d1a5ea40e`

### Added
- Euclid quick mute: Shift + G A B C D E (the last six white keys) mutes/unmutes slots 1–6, Shift + D# mutes or
  unmutes all. A muted slot keeps running (playhead moves, green dots dimmed) but plays no notes. Works from any
  screen while the sequencer is on.
- Euclid settings are saved: note, steps, pulses, rotation, rate, gate, octave and mute of every slot, plus the
  selected slot, per synth/drum. Saved in the background like the stock sequencers; slots come back stopped.

### Changed
- Boot screen version: `op1r 2`.

## 2026-10-10 14:15 — op1r 1

Output `4e037a342db27244ad4db863ded47e023e893e210d1ed42634824a08cda5ac69`

### Added
- Version number on the boot screen: `op1r 1` under the TE logo (replaces `R. 00246`).
- Animated **lab screen for the stock iter synth** (graphic `iter-lab.svg` from op1repacker, MIT License,
  © 2016 op1hacks). The gauges follow the knobs. Bubble speed follows the blue knob and the falling dashes the red
  knob. Bubbles, the flame (brighter with the volume), voice dots and the last played note react to playing.
  The pipes are brighter than in the original graphic.
- Quad LFO on drums: the envelope target now modulates the drum envelope.

### Fixed
- Quad LFO on drums: engine knob targets had no effect. They were applied in the drum envelope pass instead of the
  per-voice pass.

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
