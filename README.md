# EG13

An open-source tribute synthesizer: a bi-timbral instrument and audio processor built on
the open-source Eurorack firmware by Émilie Gillet
([pichenettes/eurorack](https://github.com/pichenettes/eurorack)).

EG13 is not affiliated with or endorsed by Mutable Instruments. "Mutable Instruments" is a
registered trademark; EG13 uses its own names for each engine.

**Status:** M3 (filter and shaper): two parts × 8 voices, each Source → Filter → Shaper. Sources: Macro Oscillator, Digital Oscillator, Modal Synth, Resonator, Audio Input. Filters: Analog Filter, Classic Filters (205 types), State Variable Filter. Shapers: Ring Mod / Vocoder (every algorithm and a frequency shifter; the carrier is an internal oscillator, the voice, the Source's AUX, the input or the other part, and the modulator the voice, the Source's AUX, the input or the other part) and EQ. Filters and shapers run a second, right-channel instance only while their input is stereo. See `docs/superpowers/specs/2026-10-20-eg13-design.md` for the full design.
Pre-1.0: saved projects and presets may not load in later versions until the parameter layout is frozen.

## Formats

CLAP and AU v2 (macOS 14+, universal), plus VST3 and Standalone.

## Engines

- **Macro Oscillator** — based on Plaits
- **Digital Oscillator** — based on Braids
- **Modal Synth** — based on Elements
- **Resonator** — based on Rings
- **Audio Input** — the plugin input through a per-voice gate
- **Analog Filter** — the VCV model of Ripples
- **Classic Filters** — sst-filters (Surge's filters)
- **State Variable Filter** — the SVF from the Eurorack DSP library
- **Ring Mod / Vocoder** — based on Warps
- **EQ** — the VCV model of Shelves

## Building (macOS)

```bash
git clone --recursive https://github.com/SymbioticAudio/EG13.git
cd EG13
brew install cmake ninja
cmake --preset mac
cmake --build --preset mac --target EG13_all eg13_tests
ctest --preset mac
scripts/validate.sh            # clap-validator, auval, pluginval
```

A fresh Xcode install needs `sudo xcodebuild -runFirstLaunch` once. `scripts/validate.sh`
installs EG13.component into `~/Library/Audio/Plug-Ins/Components`, restarts
AudioComponentRegistrar and ad-hoc signs the built bundles.

After installing or rebuilding the AU, quit and reopen Logic so it rescans
plugins; if EG13 is still missing, use Logic Pro → Settings → Plug-in Manager →
Reset & Rescan Selection.

EG13 reports about 10.85 ms of latency at 48 kHz and EG13 FX about 16.48 ms: the Modal Synth's 32 kHz engine and the Ring Mod / Vocoder's 96 kHz engine set it, and every engine is padded to it so changing engines never shifts timing. Processed input lines up with Dry in EG13 FX; hosts compensate automatically. In the instrument, note onsets are aligned: sidechain input that excites the Resonator or Modal Synth arrives up to about 6 ms after Dry.

CPU, Apple M1 (base), 48 kHz, 128-sample blocks, 2 × 8 voices (one run each; repeats of the first line varied by about 0.1 point):

- Heaviest mono-source chain: Macro Oscillator -> Analog Filter -> Vocoder: 48.95% of one core over 5 s (budget 50%)
- Heaviest stereo chain: Modal Synth -> Analog Filter -> Vocoder: 88.18% of one core over 5 s (for information; not held to the budget)
- Heaviest mono-source chain with 1 side input in use (Modulator Input): 50.30% of one core over 5 s
- Heaviest mono-source chain with 2 side inputs in use (Carrier Source AUX, Modulator Input): 51.54% of one core over 5 s

The 50 % budget is met by the heaviest chain with a mono Source while the Ring Mod / Vocoder's Carrier and Modulator are the internal oscillator or the voice. With one or two side inputs carrying a signal (Source AUX, Input or Other part), the same chain is just over it, and stereo chains of the heaviest engines can use much more. Engines that run at their own sample rate convert a side input only while it carries a signal, and their right channel only while it differs from the left; the result is the same as converting everything.

Plugins are written to `build/mac/plugins`. The first configure downloads the VST3 and
AudioUnit SDKs.

## Building the core on Linux

```bash
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
```

## Credits and licences

- EG13 is licensed under GPL-3.0 (see `LICENSE`).
- Eurorack DSP: © Émilie Gillet, MIT licence.
- Analog Filter and EQ: the Ripples and Shelves models from VCV's AudibleInstruments (© 2020 Tyler Coy and contributors), GPL-3.0-or-later.
- Classic Filters: sst-filters and sst-basic-blocks by the Surge Synth Team, GPL-3.0-or-later.
- simde (SIMD Everywhere): MIT licence.
- CLAP and clap-wrapper: free-audio, MIT licence.
- Catch2: Boost Software License 1.0. nlohmann/json: MIT licence.
- VST3 SDK (Steinberg, MIT), AudioUnitSDK (Apple, Apache-2.0), RtAudio/RtMidi (MIT-style, Standalone build).
