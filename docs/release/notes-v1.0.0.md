<!-- DRAFT: release notes for v1.0.0, awaiting the owner's approval (gate 13a). This file is the GitHub Release body; remove this comment once approved. -->
EG13 1.0 is the first release of EG13: a free, open-source two-part synthesizer and audio effect for macOS, built on the DSP of the open-source Eurorack firmware by Émilie Gillet.

## What it is

- **EG13**, the instrument: two parts, A and B, of up to eight voices each, played alone, layered, split across the keyboard or on separate MIDI channels. Each voice runs a Source, a Filter, a Shaper and an Amp; each part adds four mod slots, a 12-route modulation matrix and a Part FX slot.
- **EG13 FX**, the audio effect: the same two parts on an audio track, so the filters, the ring modulator and vocoder, and the granular, resonator and reverb effects process any sound. To process external audio, use EG13 FX: the instrument has no audio input. You can still play EG13 FX from MIDI.
- **Engines:** Macro Oscillator, Digital Oscillator, Modal Synth, Resonator and Audio Input as Sources; Analog Filter, Classic Filters and State Variable Filter; Ring Mod / Vocoder and EQ as Shapers; Envelope, LFO, Random / S&H and Function Generator in the mod slots; Granular, Granular Delay, Resonator and Reverb as Part FX.

## Highlights

- **A first sound that moves:** a new instance opens on **First Light**, a two-part ambient patch with a ring-modulated pad and a bowed string scattered into granular texture. Hold a chord and let it evolve.
- **Presets:** Bank 1 holds 40 presets made for EG13 (13 two-part Performances and 27 one-part Patches), Bank 2 seven presets rebuilt from the VCV Rack patch "~zero-g:catnap" by plantay (CC BY 4.0): the patch, three of its parts and three variations, and EG13 FX has its own bank of 12 effect presets.
- **Preset browser** in the header: previous and next arrows and a dropdown grouped by bank. Load a Patch into Part A or Part B, save your own sounds into the User bank, and share them as files.
- **A preset changes the sound, not your setup:** MIDI channels, output buses, MPE settings, CC assignments, Voice Gain, master level and Latency Mode stay as you set them.
- **Modulation you can see:** a wave mark on every modulated knob, a live trace on each mod card, and a full matrix editor.
- **MPE** (a lower or upper zone per part), polyphonic aftertouch, four assignable CCs and tempo sync.
- **Formats:** AU, VST3 and CLAP, each holding both plugins. macOS 14 (Sonoma) or later, Apple silicon and Intel.

## Install

1. Download `EG13-1.0.0-macOS.zip` below and open it.
2. Copy `EG13.component` to `~/Library/Audio/Plug-Ins/Components`, `EG13.vst3` to `~/Library/Audio/Plug-Ins/VST3` and `EG13.clap` to `~/Library/Audio/Plug-Ins/CLAP`.
3. This release is **not signed or notarised** by Apple, so macOS blocks the plugins the first time a host loads them. Clear the quarantine once, either with Open Anyway in System Settings > Privacy & Security, or with this Terminal command:

   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/EG13.component ~/Library/Audio/Plug-Ins/VST3/EG13.vst3 ~/Library/Audio/Plug-Ins/CLAP/EG13.clap
   ```

4. Restart your host, or rescan its plugins.

`INSTALL.txt` in the zip and the [install guide](https://github.com/SymbioticAudio/EG13/blob/v1.0.0/docs/manual/install.md) have the details, including rescanning in Logic Pro, REAPER and Bitwig Studio.

## Known limitations

- macOS only: no Windows, Linux, AU v3 or iPadOS version yet.
- Host undo covers route amounts only; changing a route's source, via or destination cannot be undone yet.
- No colour-blind palette option, no MIDI learn, no drag-to-assign for modulation and no editing of custom envelope shapes yet.
- The preset browser has no tags or search.
- Typing a value into a host's parameter field does nothing; use the control.

The full list is in the manual's [Known limitations](https://github.com/SymbioticAudio/EG13/blob/v1.0.0/docs/manual/limitations.md).

## Credits

- DSP based on the open-source Eurorack firmware by Émilie Gillet.
- The Analog Filter and EQ use the Ripples and Shelves models from VCV's AudibleInstruments by Tyler Coy; the Classic Filters are sst-filters by the Surge Synth Team.
- Bank 2 is a rebuild of "~zero-g:catnap : vcv rack jam" by plantay (<https://patchstorage.com/zero-gcatnap-vcv-rack-jam/>), licensed under CC BY 4.0 (<https://creativecommons.org/licenses/by/4.0/>) and changed for EG13; the [presets page](https://github.com/SymbioticAudio/EG13/blob/v1.0.0/docs/manual/presets.md#credits) lists the changes.
- Every component and its licence: `THIRD-PARTY-NOTICES.md` in the zip.

EG13 is not affiliated with or endorsed by Mutable Instruments.

EG13 is free software under the GNU General Public License, version 3. The source code of this release is at the `v1.0.0` tag.
