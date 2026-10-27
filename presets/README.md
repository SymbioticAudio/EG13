# EG13 factory presets

Each preset is one readable JSON *source* file. The build compiles them into the factory table
(`eg13_preset table`) that ships inside the plugin; nothing under `presets/` is read at run time.

| Directory | Bank shown | Plugin |
| --- | --- | --- |
| `bank1/` | Bank 1 | instrument |
| `bank2-catnap/` | Bank 2 | instrument |
| `fx/` | FX | EG13 FX |
| `test/` | (never in the table) | fixtures for the tool's tests |
| `reference/` | (git-ignored) | local reference material while drafting |

A missing or empty directory gives an empty bank. One file per preset, `<slug>.json`.

## Source format

```json
{
  "kind": "performance",            // "performance" or "patch"
  "plugin": "instrument",           // "instrument" or "effect"
  "name": "Test Performance",
  "note": "One line shown in the browser.",
  "credit": "Who made it",
  "licence": "GPL-3.0-or-later",
  "order": 10,                      // position within the bank (integer; ties sort by name)
  "firstLoad": true,                // optional: a new instrument instance opens on this preset
  "master": { "Mode": "Layer", "Dry": 0.0 },
  "parts": {
    "A": {
      "Source":  { "engine": "Digital Oscillator", "params": { "Shape": 0.3, "Timbre": 0.55 } },
      "Filter":  { "engine": "State Variable Filter", "params": { "Cutoff": 0.7 } },
      "Shaper":  { "engine": "EQ", "params": {} },
      "Part FX": { "engine": "Reverb", "params": { "Amount": 0.3 } },
      "Mod 1":   { "engine": "LFO", "scope": "per part", "params": { "Frequency": 0.4 } },
      "settings": { "Level": 0.8, "Pan": -0.25, "Voice Mode": "Poly" },
      "routes": [
        { "source": "Velocity", "destination": "Filter: Cutoff", "amount": 0.35 },
        { "source": "Mod Wheel", "via": "Velocity", "destination": "Pitch", "amount": -0.5, "slot": 3 }
      ]
    },
    "B": { ... }
  }
}
```

- **Complete state.** A source is compiled onto a fresh Init synth of its plugin, so anything it does
  not mention keeps the Init value. A source is therefore a delta from Init, and the compiled preset
  is complete.
- **Patch.** Uses only part `"A"` and has no `master`. It is applied to whichever part the user
  chooses.
- **Slots.** Keys `Source`, `Filter`, `Shaper`, `Part FX`, `Mod 1` to `Mod 4`; all optional. `engine`
  is the engine's name as the editor shows it (a slot without `engine` keeps the Init engine).
  `params` names the engine's own parameters (`P1` to `P16` as the engine labels them). `scope`
  (mod slots only) is `"per voice"` or `"per part"`. Do `Mod` slots before `routes`: route source
  and destination names depend on the mod engines and the slot engines.
- **Values.**
  - Engine parameters: a number 0 to 1 (normalised), or the text the parameter shows
    (`"Reverb"`, `"Per part"`). Text matches ignoring case and picks the middle of the matching
    stretch of the knob.
  - `settings` (part) and `master`: a number in the setting's own units (Pan -1..1, Transpose -24..24
    semitones, Level 0..1, Voices 1..8, ...), or for a stepped setting its option text as the editor
    shows it (`"Poly"`, `"C4"`, `"Layer"`).
  - Master `Level`, `Voice Gain` and `Latency Mode`, and per part `MIDI Channel`, `Output`, `MPE`,
    `MPE Bend Range`, `CC A` to `CC D`, are setup fields: they compile but applying a preset never
    changes the user's values. Leave them out. A Patch also never changes `Key Low` / `Key High`.
- **Routes.** At most 12. Names are those of the matrix pickers (`"Velocity"`, `"Mod 3 LFO"`,
  `"Filter: Cutoff"`, `"Pitch"`). `amount` is the route amount, -1 to 1 (for Pitch, amount x 24
  semitones). `via` is optional. `slot` (1..12) is optional; routes without it fill the slots in
  order. If `routes` is present every other route slot is cleared; if absent the Init routes stay.
- **Not in the format yet.** Custom envelope shapes: a preset takes the Init shapes.
- Any unknown key, engine, parameter, option, setting or route name fails the build with the file and
  the name, so a source can never ship broken after an engine is renamed.

## The tool

`eg13_preset` is built with the project (`build/mac/tools/preset_tool/eg13_preset`).

```
eg13_preset compile <source.json> -o <file> [--bank <name>]
eg13_preset decompile <file> -o <source.json>
eg13_preset render <source.json|file> -o <out.wav> [--seconds 12] [--part A|B]
eg13_preset golden [--write] [--presets <dir>] [--file <preset_golden.json>]
eg13_preset table <dir>... -o <factory_presets.cpp>
```

- `decompile` writes every parameter of every selected engine, all settings and the routes in use,
  which makes it the way to turn a sound saved in the plugin (`.eg13perf` / `.eg13patch`) into a
  source to tidy and commit. Delete the keys that are Init values to keep a source short.
- `render` writes a 16-bit 48 kHz stereo WAV for listening. It drives a synth directly, no host.
  A Patch plays in part A of an Init instance (`--part B` for B, with the Init part A muted). An
  effect preset gets a fixed 110 Hz saw and noise as its input.
- The **note script**, fixed: C3, G3, D4 and A4 held together at velocity 100 for 8 s, then released,
  then a tail, 12 s in all.
- `golden` renders every factory preset with the note script and compares its level envelope (RMS in
  dB per 250 ms) with `tests/data/preset_golden.json`. A frame louder than -60 dB must be within
  1.5 dB. `--write` rewrites the file; do that when a preset is added, removed or changed on
  purpose, and look at the diff. The same check runs as the ctest "Each factory preset matches its
  golden level envelope".
- `table` is what the build runs (`presets/bank1`, `presets/bank2-catnap`, `presets/fx`) to produce
  `build/<preset>/generated/factory_presets.cpp`.

## By-ear workflow

See Task 6 and `scripts/preset-listen.sh` (coming).
