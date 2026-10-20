# EG13

An open-source tribute synthesizer: a bi-timbral instrument and audio processor built on
the open-source Eurorack firmware by Émilie Gillet
([pichenettes/eurorack](https://github.com/pichenettes/eurorack)).

EG13 is not affiliated with or endorsed by Mutable Instruments. "Mutable Instruments" is a
registered trademark; EG13 uses its own names for each engine.

**Status:** M0 (skeleton): one monophonic Macro Oscillator voice, as the EG13 instrument and
EG13 FX plugins. See `docs/superpowers/specs/2026-10-20-eg13-design.md` for the full design.
Pre-1.0: saved projects and presets may not load in later versions until the parameter layout is frozen.

## Formats

CLAP and AU v2 (macOS 13+, universal), plus VST3 and Standalone.

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

Plugins are written to `build/mac/plugins`. The first configure downloads the VST3 and
AudioUnit SDKs.

## Building the core on Linux

```bash
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
```

## Credits and licences

- EG13 is licensed under GPL-3.0 (see `LICENSE`).
- Eurorack DSP: © Émilie Gillet, MIT licence.
- CLAP and clap-wrapper: free-audio, MIT licence.
- Catch2: Boost Software License 1.0. nlohmann/json: MIT licence.
- VST3 SDK (Steinberg, MIT), AudioUnitSDK (Apple, Apache-2.0), RtAudio/RtMidi (MIT-style, Standalone build).
