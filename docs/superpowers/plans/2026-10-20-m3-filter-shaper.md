# EG13 M3 (Filter + Shaper) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the Filter slot (Off, Analog Filter from the VCV Ripples model, Classic Filters from sst-filters, State Variable Filter from stmlib) and the Shaper slot (Off, Ring Mod / Vocoder from Warps with all its algorithms and four modulator sources, EQ from the VCV Shelves model) to every voice, with stereo-when-needed, constant slot latency, per-engine settings stashes, host parameters and state schema 4.

**Architecture:** Each voice becomes Source → Filter → Shaper. A `ProcessorSlot` per voice and slot holds every engine of that slot (preallocated, each in its own `NativeRateWrapper`), pads them all to one slot latency, detects whether its input is stereo and crossfades when the engine changes. Engines run their right-channel instance only while the input is stereo (`StereoLink`). The wrapper gains a third channel: a side input (the Warps modulator) and an AUX output (Plaits AUX, Rings' even output). The Synth generalises M2's Source stash and engine switching to all three slots, and routes the part's Input and the other part's output to the voices for the Warps modulator.

**Tech Stack:** C++17, CMake + Ninja, Catch2 v3, r8brain-free-src 7.6, upstream pichenettes/eurorack (Plaits, Braids, Elements, Rings, Warps + stmlib, unmodified), VCVRack/AudibleInstruments (Ripples and Shelves models, unmodified, through a minimal Rack SDK stand-in), surge-synthesizer/sst-filters and sst-basic-blocks (their `last_cpp_17` tags), simd-everywhere/simde v0.8.2, CLAP 1.2.10, clap-wrapper v0.16.0.

**Spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md` (M3 row of §11; §2.1 repository layout; §2.2 key rules; §3 engines, native rate and adapters; §4 Filter and Shaper rows and the Ring Mod / Vocoder modulator sources; §5.1 stereo only when needed; §7.1 Filter and Shaper parameters; §7.2 state; §9 error handling; §10 testing and CPU budget).

**Branch:** `m3-filter-shaper`, created from `main` after the M2 merge (`759ca30`).

## Global Constraints

- EG13 is GPL-3.0. Never use "Mutable Instruments" or module names in the UI or product. Engines show traditional names: Filter **Off**, **Analog Filter**, **Classic Filters**, **State Variable Filter**; Shaper **Off**, **Ring Mod / Vocoder**, **EQ**.
- Licences of everything M3 adds are GPL-3.0-compatible: AudibleInstruments `src/` is GPL-3.0-or-later (its `LICENSE.md`; `src/Ripples/ripples.hpp:1-15` and `src/Shelves/shelves.hpp:1-15` © 2020 Tyler Coy, GPL-3.0-or-later); sst-filters and sst-basic-blocks are GPL-3.0-or-later (their `LICENSE` and file headers); simde is MIT (`COPYING`); Warps is part of the MIT eurorack tree. EG13 uses none of AudibleInstruments' `res/` panel graphics (© Émilie Gillet).
- macOS 14+ deployment target, universal binary (arm64 + x86_64). C++17. `/core` builds and tests on macOS and Linux (CI: GCC 13, `-Werror` on `eg13_core`). Check Linux locally with:
  ```bash
  docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c "apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ >/dev/null && cmake --preset linux >/dev/null && cmake --build --preset linux && ctest --preset linux"
  ```
- The audio thread never allocates, locks or waits. Allocation happens only in constructors and `prepare`. Prove it with `eg13::test::ScopedAllocationCounter`, reading `counter.count()` into a local before `REQUIRE`.
- `third_party/eurorack`, `third_party/AudibleInstruments`, `third_party/sst-filters`, `third_party/sst-basic-blocks` and `third_party/simde` stay unmodified. Patches go only in `third_party/eurorack-patches` with a row in its README (M3 needs none). Upstream headers are included through SYSTEM include directories so GCC/Clang warnings in them do not break `-Werror`.
- Every TU that includes eurorack headers is compiled with `TEST` defined and sees `third_party/eurorack-patches` first on the include path (unchanged from M2).
- Parameter IDs are permanent: `id = (part << 16) | (slot << 8) | index`. Source IDs keep their meaning. New in M3: Filter type `id(part, Slot::Filter, 0)` (stepped, fixed range 0..31), Filter P1..P4 `id(part, Slot::Filter, 1..4)`, Shaper type `id(part, Slot::Shaper, 0)` (stepped, 0..31), Shaper P1..P12 `id(part, Slot::Shaper, 1..12)`. All P values are normalized 0..1.
- Engine registry indices are permanent. Filter: 0 Off, 1 Analog Filter, 2 Classic Filters, 3 State Variable Filter. Shaper: 0 Off, 1 Ring Mod / Vocoder, 2 EQ. Selector values without an engine fall back to Off.
- Classic Filters type indices are permanent: the order is frozen in `tests/data/classic_filter_types.txt` (205 types at the pinned sst-filters).
- Native rates/blocks: Warps 96000 Hz / 60 samples (plus a fixed 60-sample block FIFO, reported as engine latency). Analog Filter, Classic Filters, State Variable Filter, EQ and Off run at the host rate.
- Smoothing: every continuous parameter is smoothed the way its firmware or VCV port does, with `core/engine/pot_smoother.h` (`PotSmoother`, `ParamRamp`, `Declicker`). The first block after `reset()` snaps to its targets. Stepped parameters are not smoothed. Each engine has tests for gradual steps, convergence and snapping.
- Alignment: every engine in a slot is padded to that slot's latency (its largest input-to-output latency). Voice latency = Source latency + Filter slot latency + Shaper slot latency. Switching Filter or Shaper type never moves the output in time, and the plugin reports the total.
- Signal levels for the VCV-derived adapters (Analog Filter, EQ, Ring Mod / Vocoder inputs): EG13's ±1.0 is ±5 V, VCV's audio level.
- Every adapter memsets an upstream object before `Init()` when upstream leaves members uninitialised (Warps `Modulator`, as `AudibleInstruments/src/Warps.cpp:61-62` does).
- Every user-facing behaviour choice is documented in docs/manual in the same task that implements it.
- The two VCV-model adapters (`core/engine/ripples_engine.cpp`, `core/engine/shelves_engine.cpp`) are compiled with Rack's own optimisation flags, `-O3 -funsafe-math-optimizations` (`VCVRack/Rack compile.mk:17`, `FLAGS += -O3 -funsafe-math-optimizations -fno-omit-frame-pointer`); their fidelity tests compare with the bare model, built without that flag in the test TU, to a tolerance of 1e-5 × max(1, |expected|) (owner, 2026-10-20). Every other engine stays bit-exact.
- Commits are authored by `Westlee Latta <symbioticaudio@proton.me>` (use `git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit ...`) and end with:
  ```
  Co-Authored-By: <the model that wrote the change> <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BfbDAN2RSSY1ZShBua2VNM
  ```

## Scope notes (decisions where the spec is silent)

- **Slot sizes (Task 1 amends spec §7.1).** Every control of each module was listed before sizing:
  - Analog Filter (Ripples, `AudibleInstruments/src/Ripples.cpp:6-26`): Resonance, Frequency, FM knob; inputs Res/Freq/FM CV, Audio, Gain; outputs BP2, LP2, LP4, LP4 VCA. EG13 keeps **Frequency, Resonance, Mode (LP4 / LP2 / BP2)**: 3. The FM knob only scales the FM jack, and v1 has no audio-rate FM source (spec §6); the CV jacks are the modulation matrix (M4); LP4 VCA is LP4 through the Gain-input VCA, which with Gain unpatched is LP4 AC-coupled at a fixed level.
  - Classic Filters (sst-filters `filters++` API at `last_cpp_17`): cutoff, resonance, one model + configuration from 205 (`Filter::availableModels()` × `availableModelConfigurations()`), and at most one extra coefficient (`Filter::coefficientsExtraCount` ≤ 1: OB-Xd Morph, K35 Continuous saturation, Fast SVF shelf/bell gain, Comb variable mix). EG13: **Cutoff, Resonance, Type, Character**: 4.
  - State Variable Filter (stmlib `Svf`): **Cutoff, Resonance, Mode (LP / BP / HP)**: 3.
  - Filter slot: **P1–P4** (the spec's count holds).
  - Ring Mod / Vocoder (Warps, `AudibleInstruments/src/Warps.cpp:6-27`, `warps/cv_scaler.cc:123-174`, `warps/ui.cc` easter egg): Algorithm (continuous over nine algorithms, `warps/dsp/parameters.h:43-53`), Timbre, Level 1, Level 2, the internal-oscillator button (off, or one of three oscillator modes: `carrier_shape` 0..3, `Warps.cpp:68-69`), the frequency-shifter easter egg. EG13 replaces the patch cables with two selectors and adds the oscillator's pitch offset: **Algorithm, Timbre, Carrier Level, Mod Level, Carrier (Internal Osc / Voice / Source AUX / Input / Other part), Modulator (Voice / Source AUX / Input / Other part), Osc Pitch, Osc Shape (the button's three modes), Mode (Modulation / Frequency shifter)**: 9.
  - EQ (Shelves, `AudibleInstruments/src/Shelves.cpp:16-53`): ten knobs (high shelf freq/gain, two parametric freq/gain/Q, low shelf freq/gain), the −6 dB input pre-gain option (`Shelves.cpp:61,107,123`), six filter-tap outputs (P1/P2 HP/BP/LP). EG13: the ten knobs, **Input Pad**, **Output (EQ or one of the six taps)**: 12.
  - Shaper slot: **P1–P12** (raised from 6; new IDs `id(part, Slot::Shaper, 7..12)`, nothing renumbered).
- **Knob mappings** follow the VCV ports (spec §3.3): Ripples `freq_knob = p` (20 Hz·1000^p, `Ripples.cpp:36,75`), `res_knob = p`; Shelves `freq_knob = p`, `gain_knob = 2p − 1` (±18 dB, `Shelves.cpp:130-133`), `q_knob = p` (Q 0.5..40, `Shelves.cpp:135-136`); Warps `modulation_algorithm = p` (`Warps.cpp:80`), `modulation_parameter = p` (`:91`), `channel_drive[i] = p` (`:78-79`), easter-egg `frequency_shift_pot = p` and `phase_shift = algorithm` (`:93-95`). Classic Filters and the SVF have no VCV port: their Cutoff uses the same 20 Hz·1000^p curve as the Analog Filter so one modulation amount means the same sweep in every filter; the SVF's Q is 0.5·80^p (Shelves' Q range, `shelves.hpp:31-32`).
- **Defaults** (cited in each engine): VCV defaults except where noted. Analog Filter fully open (`Ripples.cpp:36`), resonance 0. Classic Filters and SVF Cutoff fully open too, so choosing a filter does not jump the level. SVF Resonance defaults to Q = 0.707 (flat passband). Ring Mod / Vocoder Algorithm defaults to the analog ring modulator (2/8) rather than VCV's crossfade (`Warps.cpp:43`): the engine is named Ring Mod. Carrier = Internal Osc with Modulator = Voice (owner, 2026-10-20, provisional: the common hardware patch, a cable into MODULATOR with the internal oscillator on). These defaults live only in the engine's `EngineInfo` parameter defaults; changing one later is a one-line edit that affects new patches only. Once M7's factory presets exist, such a change would need those presets re-saved, not a state migration. Carrier Level and Mod Level default to 0.47 rather than VCV's 1.0 (`Warps.cpp:46-47`), owner 2026-10-20: measured during planning, 0.47 is where Warps' `SaturatingAmplifier` (`warps/dsp/modulator.h:53-115`) passes a full-scale EG13 voice (a 1 kHz sine at ±1.0, i.e. ±10240 counts) at unity gain (+0.03 dB, interpolated between +0.22 dB at 0.48 and −0.17 dB at 0.46) with 0.06 % THD; at 1.0 it adds +12 dB and 38 % THD. The ranges are unchanged, so turning the levels up still gives the saturated character. Task 8 pins the measurement in a test.
- **Plugin defaults:** the instrument starts with Filter Off and Shaper Off. EG13 FX's Part A starts with Filter = Analog Filter (spec §6's FX init patch; M2 deferred its filter to M3). Modulation of Filter P1 and the instrument init patch arrive with the matrix in M4. Documents older than schema 4 load with Filter and Shaper Off, so M2 projects keep their sound.
- **Stereo when needed (§5.1), exactly:** each `ProcessorSlot` checks its input every call (≤ 16 samples). It is stereo from the first block in which any L sample differs from its R sample, and mono again after 50 ms (`kStereoHoldSeconds`) in which L == R exactly (silence counts as mono). The engine's right-channel instance runs only while stereo. When stereo starts, an engine whose state can be copied (Analog Filter, SVF, EQ) gives the right instance the left instance's state, so the right channel continues seamlessly; Warps (self-referencing buffers, cannot be copied) resets its right instance and fades its output in from the left output over 10 ms (`kStereoFadeSeconds`). When stereo stops, the right output fades back to the left output over 10 ms, then the right instance stops. While mono, the right output is the left output. Classic Filters runs L and R as two lanes of one SIMD register: the right lane costs nothing extra and, fed identical samples while mono, holds exactly the left lane's state, so it needs no transition.
- **Ring Mod / Vocoder routing, as on the module (owner, 2026-10-20, provisional; to be revisited during patching).** Warps has two inputs, CARRIER (channel 1) and MODULATOR (channel 2); `Parameters::carrier_shape` is the internal-oscillator button: 0 takes the carrier from the CARRIER jack, 1..3 replace it with the internal oscillator (`modulator.cc:209-286`), whose waveform Warps picks per algorithm region from the mode: cross-modulation sine / triangle / saw, vocoder saw / pulse / noise (`modulator.cc:233-237`), at `Parameters::note` (`modulator.cc:246,256,267,273`; the module derives it from the LEVEL 1 pot and V/OCT, `cv_scaler.cc:149-173`). With the oscillator on, Warps skips the carrier channel's amplifier, so LEVEL 1 has no effect (`modulator.cc:215`), and reads the CARRIER jack as the oscillator's FM input (`modulator.cc:228-231`). EG13 replaces the cables with two selectors:
  - **Carrier:** *Internal Osc* sets `carrier_shape = 1 + Osc Shape` with `note` = the voice's pitch + Osc Pitch (±24 st; at 96 kHz, Warps' own rate, no correction, where VCV adds one at other rates, `Warps.cpp:97`) and leaves CARRIER silent (it would only be the oscillator's FM; v1 has no audio-rate FM, spec §6). *Voice*, *Source AUX*, *Input* and *Other part* set `carrier_shape = 0` and feed that signal into CARRIER. Osc Pitch and Osc Shape only matter with Internal Osc (documented in the manual).
  - **Modulator** (MODULATOR): *Voice*, *Source AUX*, *Input*, *Other part*. Voice stays the default whatever the Carrier; Carrier = Voice with Modulator = Voice is self-modulation, allowed and harmless.
  - The signals: *Voice* is what reaches the Shaper from this voice's Source → Filter, per channel (each stereo instance gets its own channel). *Source AUX* is Plaits' AUX or Rings' even output (other sources their main output), *Input* the part's Input as its Input source routes it (§2.3), *Other part* the other part's post-FX output (A→B without delay, B→A through the 64-sample feed, soft-limited as in M2); these three are mono (L+R)/2 and reach the engine as side inputs `in[2]` (CARRIER) and `in[3]` (MODULATOR), through the wrapper's input conversion, from per-voice buffers, without allocation. An absent Input or Other part reads as silence.
  - Both selectors are append-only option lists read in 8 fixed steps, and a test pins their order, so options can be added later without changing what saved projects mean.
  - **Mode = Frequency shifter** is the easter egg (`modulator.cc:69-186`), which uses the same two channels and `carrier_shape`: with Internal Osc, its quadrature oscillator shifts channel 2 (+ channel 1, silent here) by the frequency Algorithm sets (`modulator.cc:78-98,140-145`): the classic frequency shifter of the Modulator signal (the voice by default); with any other Carrier, the CARRIER signal is split into I/Q with Algorithm as the phase shift and single-sideband multiplied with the Modulator (`modulator.cc:99-119`). Timbre is the up/down sideband mix, Carrier Level the feedback and Mod Level the dry/wet, as on the module.
  - Warps' output is read back with the input's scaling (±10240 counts = EG13 ±1.0), so a unity path through Warps is unity in EG13.
- **Algorithm is continuous**, as Warps' big knob (adjacent algorithms crossfade, `modulator.cc:292-311`); its text names the algorithm or the pair it is between. The spec's "mode menu" becomes the card's detent list in M6.
- **Analog Filter polarity:** Ripples' output amplifiers invert (`ripples.hpp:95-97`); the adapter restores the input's polarity so Dry and processed signals add rather than cancel.
- **Engine change while sounding:** the slot keeps the old engine running with its last parameters, holds it alone for the slot latency (the new engine's first output arrives then) and crossfades linearly to the new engine over 10 ms (`kSlotFadeSeconds`). Changing Classic Filters' Type or a mode/output selector inside one engine is immediate, as on the hardware.
- **Voice freeing:** spec §5.2's silence rule is measured at the Shaper input (plus the Shaper slot latency to flush it), because Warps' internal oscillator keeps the Shaper output alive after the note has died (crossfade/fold algorithms pass the modulator). M4's Amp will gate the output.
- **Latency:** the Shaper slot's latency is the Warps wrapper's input path (≈ 270 samples, 5.6 ms at 48 kHz, measured during planning with r8brain 7.6) plus Warps' 60-sample block FIFO (30 samples at 48 kHz); Filter slot latency is 0. So the instrument reports ≈ 521 samples (10.9 ms) and EG13 FX ≈ 791 samples (16.5 ms) at 48 kHz (M2: 4.6 / 10.2 ms). Planning also confirmed M2's wrapper reports its input-path latency to within one sample of the measured delay for a 96 kHz, 60-sample engine. Owner decision (2026-10-20): this constant slot latency is accepted for v1 and documented in the README, the manual and spec §3.2. Task 15 measures and documents the real figures.
- **CPU (measured during planning on an M1, -O3, per instance):** Ripples model 1.31 % of a core at 48 kHz, Shelves 1.07 %, Warps cross-modulation 0.45 % and vocoder 1.09 % at 96 kHz, sst Vintage Ladder stereo 1.37 %. A 2 × 8 stereo chain of Modal Synth → Analog Filter → Vocoder therefore costs about 90 % of a core, above the spec's 50 % budget. Owner decision (2026-10-20): the 50 % budget applies to chains whose Source is mono; the stereo worst case is measured and documented, not held to the budget. The VCV models are built with Rack's flags (−30 % measured, see Global Constraints). Task 15 measures the full matrix in both cases.

## Owner decisions (2026-10-20)

Taken on the first draft of this plan; Task 1 records them in the spec and the manual.

1. **CPU budget:** 50 % of one M1 core for 2 × 8 voices of the heaviest chain *with a mono Source*. The stereo worst case (≈ 90 % estimated before the flag change) is measured, printed and documented, not asserted. The Ripples and Shelves models are compiled with Rack's `-O3 -funsafe-math-optimizations`; their fidelity tests use a 1e-5 tolerance.
2. **Latency:** the constant slot latency is accepted for v1: about 10.9 ms (instrument) and 16.5 ms (EG13 FX) at 48 kHz.
3. **Warps routing (provisional; to be revisited during patching):** a Carrier selector (Internal Osc / Voice / Source AUX / Input / Other part) and a Modulator selector (Voice / Source AUX / Input / Other part); defaults Internal Osc and Voice. Both option lists are append-only and pinned by a test; the defaults live only in the engine's parameter defaults. Spec §4's Ring Mod / Vocoder row is rewritten accordingly in Task 1.
4. **Warps levels:** both default to 0.47, the measured unity-gain, unsaturated point of Warps' amplifier; ranges unchanged.

## Review Focus

1. **Changing Filter or Shaper type while notes sound or while the selector is automated:** no click, no timing shift, P values swap to the new engine's last settings and the host shows them. Pinned in Task 10 ("switching engines while sounding crossfades without a step") and Task 13 ("switching Filter type keeps the output aligned in time").
2. **Sources that go stereo and back (Rings Spread, Modal Synth, stereo Audio Input) through every Filter and Shaper:** the stereo image passes through and the switch between one and two instances makes no jump. Pinned in Task 3 ("StereoLink start and stop are continuous"), Task 5 ("stereo starts without a jump"), Task 10 ("a real filter goes stereo and back without a jump").
3. **Ring Mod / Vocoder modulator sources with nothing on them** (Input with no sidechain, Other part silent or in Single mode, Source AUX on an engine without AUX), for either Warps input: silence or the documented fallback, never garbage or NaN. Pinned in Task 11 ("absent modulator sources give silence").
4. **Old projects:** M2 (schema 3) and older instrument and FX projects open with Filter and Shaper Off and the same sound. Pinned in Task 14 ("Schema 3 projects load with Filter and Shaper Off and pass audio as before").
5. **Host rates other than 48 kHz and odd host block sizes**, especially 96 kHz where Warps runs bypassed through its block FIFO: in tune, finite, aligned. Pinned in Task 8 ("through the wrapper at 44.1, 48 and 96 kHz with odd host blocks").

## File Structure

```
.gitmodules                               + AudibleInstruments, sst-filters, sst-basic-blocks, simde
third_party/AudibleInstruments            submodule @ a1cd335 (Ripples, Shelves models; unmodified)
third_party/sst-filters                   submodule @ 74ef5bd (tag last_cpp_17)
third_party/sst-basic-blocks              submodule @ 1f2ce90 (tag last_cpp_17)
third_party/simde                         submodule @ 71fd833 (v0.8.2)
cmake/eurorack.cmake                      + Warps sources
cmake/vcv.cmake                           eg13_vcv: AudibleInstruments/src + rack shim + simde
cmake/sst.cmake                           eg13_sst: sst-filters + sst-basic-blocks + simde
CMakeLists.txt                            include the two new cmake files
core/CMakeLists.txt                       new sources; link eg13_vcv, eg13_sst
core/engine/rack_shim/rack.hpp            minimal Rack SDK stand-in for the two VCV models
core/engine/voice_engine.h                EngineInfo hasAux/sideInputs/sideSource; SideSource; setStereo,
                                          setAuxEnabled, nativeLatency
core/engine/native_rate_wrapper.h|.cpp    up to 3 channels (side in, AUX out); engine latency
core/engine/pot_smoother.h                + ParamRamp
core/engine/stereo_link.h                 StereoLink, StereoDetector (§5.1)
core/engine/param_format.h                shared value formatters for the new engines
core/engine/plaits_engine.h|.cpp          AUX output
core/engine/rings_engine.h|.cpp           even output as AUX
core/engine/ripples_engine.h|.cpp         Analog Filter
core/engine/sst_filter_engine.h|.cpp      Classic Filters
core/engine/svf_engine.h|.cpp             State Variable Filter
core/engine/warps_engine.h|.cpp           Ring Mod / Vocoder
core/engine/shelves_engine.h|.cpp         EQ
core/engine/bypass_engine.h|.cpp          Off
core/engine/engine_registry.h|.cpp        Filter and Shaper ids, names, infos
core/voice/processor_factory.h|.cpp       makeAllFilters(), makeAllShapers()
core/voice/processor_slot.h|.cpp          one Filter or Shaper slot of a voice
core/voice/voice.h|.cpp                   Source -> Filter -> Shaper, side signal, latency
core/part/part.h|.cpp                     slot engines, VoiceInputs, SlotParams
core/synth/param_table.h|.cpp             v4 table: Filter/Shaper selectors and P's, SlotKind helpers
core/synth/synth.h|.cpp                   three-slot stash and switching, other-part feed for voices
core/state/state.h|.cpp                   schema 4: filterSettings, shaperSettings; migration
tests/alloc_counter.h|.cpp                aligned new; bytes()
tests/test_engines.h                      side/AUX/latency/stereo/gain test engines
tests/data/classic_filter_types.txt       frozen Classic Filters type order
tests/test_*.cpp                          per task
docs/superpowers/specs/2026-10-20-eg13-design.md   §1.2, §2.1, §7.1 amendments
README.md                                 engines, latency, CPU, credits
docs/manual/index.md                      the user manual: contents
docs/manual/engines.md                    one section per engine
docs/manual/parts-and-routing.md          parts, voices, slots, input source, engine switching, defaults
docs/manual/latency.md                    latency figures and why
docs/manual/smoothing.md                  how each engine smooths its controls
docs/manual/limitations.md                known limitations
```

---
### Task 1: Third-party DSP, spec amendments and the user manual

**Files:**
- Modify: `.gitmodules` (via `git submodule add`), `CMakeLists.txt`, `cmake/eurorack.cmake`, `core/CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/superpowers/specs/2026-10-20-eg13-design.md`, `README.md`
- Create: `third_party/AudibleInstruments`, `third_party/sst-filters`, `third_party/sst-basic-blocks`, `third_party/simde` (submodules), `cmake/vcv.cmake`, `cmake/sst.cmake`, `core/engine/rack_shim/rack.hpp`, `docs/manual/index.md`, `docs/manual/engines.md`, `docs/manual/parts-and-routing.md`, `docs/manual/latency.md`, `docs/manual/smoothing.md`, `docs/manual/limitations.md`
- Test: `tests/test_third_party_smoke.cpp`

**Interfaces:**
- Consumes: M2 build (`eg13_eurorack`, `eg13_core`, `eg13_tests`).
- Produces:
  - CMake INTERFACE targets `eg13_vcv` (SYSTEM includes `third_party/AudibleInstruments/src` and `third_party/simde`; normal include `core/engine/rack_shim`) and `eg13_sst` (SYSTEM includes `third_party/sst-filters/include`, `third_party/sst-basic-blocks/include`, `third_party/simde`). Both are linked PRIVATE to `eg13_core` and to `eg13_tests`.
  - `eg13_eurorack` also compiles Warps (`warps/dsp/modulator.cc`, `oscillator.cc`, `vocoder.cc`, `filter_bank.cc`, `warps/resources.cc`).
  - `core/engine/rack_shim/rack.hpp`: namespace `rack` with `math::clamp`, `math::rescale`, `simd::float_4` (+ arithmetic operators, `fmin`, `fmax`, `clamp`, `rescale`, `pow(float, float_4)`), `dsp::TRCFilter<T>`, `dsp::TSlewLimiter<T>` (+ `RCFilter`, `SlewLimiter` typedefs), `random::uniform()`, `random::seed(uint64_t)`.
  - `eg13_tests` gets `EG13_TEST_DATA_DIR` (path to `tests/data`).

- [ ] **Step 1: Add the four submodules at their pinned commits**

```bash
git submodule add https://github.com/VCVRack/AudibleInstruments.git third_party/AudibleInstruments
git -C third_party/AudibleInstruments checkout a1cd335ec6ac44e8be55891854015bf0763f8552
git submodule add https://github.com/surge-synthesizer/sst-filters.git third_party/sst-filters
git -C third_party/sst-filters checkout 74ef5bd29dfd9311f59a67518eec22f55d225b24   # tag last_cpp_17
git submodule add https://github.com/surge-synthesizer/sst-basic-blocks.git third_party/sst-basic-blocks
git -C third_party/sst-basic-blocks checkout 1f2ce9059e3d694223a67f65e3dff6a2bf1bf3c9   # tag last_cpp_17
git submodule add https://github.com/simd-everywhere/simde.git third_party/simde
git -C third_party/simde checkout 71fd833d9666141edcd1d3c109a80e228303d8d7   # v0.8.2
git add .gitmodules third_party/AudibleInstruments third_party/sst-filters third_party/sst-basic-blocks third_party/simde
```

Do not initialise AudibleInstruments' own nested `eurorack` submodule locally: EG13 includes only `AudibleInstruments/src/Ripples` and `src/Shelves`, which do not use it (CI's recursive checkout fetches it harmlessly). sst-filters moved to C++20 after `last_cpp_17`; EG13 is C++17, so these two tags are the newest usable versions.

Verify the licences you are about to rely on and record nothing new if they match: `head -5 third_party/AudibleInstruments/LICENSE.md` (GPL-3.0-or-later for `src/`), `head -2 third_party/sst-filters/LICENSE` and `third_party/sst-basic-blocks/LICENSE` (GNU GPL v3), `head -3 third_party/simde/COPYING` (MIT). If any differs, stop and report BLOCKED.

- [ ] **Step 2: Amend the spec (licences, layout, Shaper slot size, latency, Ring Mod / Vocoder routing, CPU budget)**

In `docs/superpowers/specs/2026-10-20-eg13-design.md`:

§1.2, after the `surge-synthesizer/sst-filters` row, add:

```markdown
| surge-synthesizer/sst-basic-blocks | GPL-3.0 | SIMD setup required by sst-filters |
| simd-everywhere/simde | MIT | SSE intrinsics on arm64 for sst-filters and the Ripples/Shelves models |
```

§2.1, replace the line `/third_party/sst-filters, clap, clap-wrapper, r8brain-free-src, Catch2   submodules` with:

```
/third_party/sst-filters, sst-basic-blocks, simde, clap, clap-wrapper, r8brain-free-src, Catch2   submodules
```

§7.1, replace `- Shaper: type, P1–P6` with:

```markdown
- Shaper: type, P1–P12 (raised from 6, M3 2026-10-20: the EQ needs its ten knobs, its −6 dB input pad and the choice among its six filter-tap outputs; Ring Mod / Vocoder uses nine)
```

§3.2, append to the paragraph that ends "so switching engines never shifts timing.":

```markdown
Owner decision (2026-10-20): this constant latency is accepted for v1. With the Ring Mod / Vocoder's 96 kHz engine in every Shaper slot it is about 10.9 ms (instrument) and 16.5 ms (EG13 FX) at 48 kHz.
```

§4, replace the Ring Mod / Vocoder row's Notes cell (from "Mode menu shows all Warps algorithms." to the end of the cell) with:

```markdown
All Warps algorithms (a continuous Algorithm knob, as on the module) plus its frequency shifter. Owner decision 2026-10-20 (provisional; to be revisited during patching): Warps' two inputs are chosen by two selectors. **Carrier**: Internal Osc (the module's internal oscillator, tracking the voice's pitch plus an offset, with its three oscillator modes) / Voice / Source AUX / Input / other part. **Modulator**: Voice / Source AUX / Input / other part. Defaults: Carrier Internal Osc, Modulator Voice. Option lists are append-only.
```

and replace the list under "Ring Mod / Vocoder modulator sources:" with (the heading line becomes "Ring Mod / Vocoder input sources (Carrier and Modulator):"):

```markdown
- **Voice:** the signal reaching the Shaper from this voice's Source → Filter (Carrier = Voice with Modulator = Voice is self-modulation, allowed).
- **Internal Osc** (Carrier only): Warps' internal oscillator.
- **Source AUX:** the Source's secondary output (Macro Oscillator AUX, Resonator even partials); other sources fall back to their main output.
- **Input:** §2.3.
- **Other part:** the other part's output, A→B with no delay, B→A through the fixed 64-sample feed, soft-limited (§2.3).
```

§10, replace the **CPU benchmark** bullet's budget sentence with:

```markdown
Budget (owner, 2026-10-20): 2 × 8 voices of the heaviest chain *with a mono Source* run below 50% of one M1 performance core at 48 kHz, 128-sample blocks; the stereo worst case is measured and documented, not held to the budget.
```

- [ ] **Step 3: Write the failing smoke test (`tests/test_third_party_smoke.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include "Ripples/ripples.hpp"
#include "Shelves/shelves.hpp"
#include "alloc_counter.h"
#include "sst/filters++.h"
#include "test_helpers.h"
#include "warps/dsp/modulator.h"

namespace sfpp = sst::filtersplusplus;

TEST_CASE("The Rack stand-in's RC filter is Rack's bilinear one-pole") {
  rack::dsp::TRCFilter<float> f;
  f.setCutoffFreq(0.01f);
  // Rack include/dsp/filter.hpp:30-49, written out.
  const float c = 2.f / (2.f * float(M_PI) * 0.01f);
  float x0 = 0.f, y0 = 0.f;
  for (int i = 0; i < 100; ++i) {
    const float x = i == 0 ? 1.f : 0.25f;
    f.process(x);
    const float y = (x + x0 - y0 * (1 - c)) / (1 + c);
    x0 = x;
    y0 = y;
    REQUIRE(f.lowpass() == y);
    REQUIRE(f.highpass() == x - y);
  }
}

TEST_CASE("The Rack stand-in's float_4 behaves per lane") {
  rack::simd::float_4 a(1.f, -2.f, 3.f, -4.f);
  const rack::simd::float_4 b = a * 2.f + 1.f;
  REQUIRE(b[0] == 3.f);
  REQUIRE(b[1] == -3.f);
  REQUIRE(b[3] == -7.f);
  const rack::simd::float_4 c = rack::simd::clamp(a, -1.f, 2.f);  // Rack simd/functions.hpp:251-253
  REQUIRE(c[0] == 1.f);
  REQUIRE(c[1] == -1.f);
  REQUIRE(c[2] == 2.f);
  const rack::simd::float_4 p = rack::simd::pow(2.f, rack::simd::float_4(0.f, 1.f, 2.f, -1.f));
  REQUIRE(std::fabs(p[2] - 4.f) < 1e-5f);
  REQUIRE(std::fabs(p[3] - 0.5f) < 1e-6f);
  a[2] = 9.f;
  REQUIRE(a[2] == 9.f);
  REQUIRE(rack::math::clamp(5.f, 0.f, 1.f) == 1.f);
  REQUIRE(rack::math::rescale(0.5f, 0.f, 1.f, 10.f, 20.f) == 15.f);
}

TEST_CASE("The Rack stand-in's random numbers are in [0, 1) and repeat per seed") {
  rack::random::seed(7);
  std::vector<float> first;
  for (int i = 0; i < 1000; ++i) {
    const float u = rack::random::uniform();
    REQUIRE(u >= 0.f);
    REQUIRE(u < 1.f);
    first.push_back(u);
  }
  rack::random::seed(7);
  for (int i = 0; i < 1000; ++i) REQUIRE(rack::random::uniform() == first[size_t(i)]);
}

TEST_CASE("The VCV Ripples and Shelves models run unmodified") {
  ripples::RipplesEngine ripples;
  ripples.setSampleRate(48000.f);
  ripples::RipplesEngine::Frame frame = {};
  frame.freq_knob = 0.5f;
  frame.res_knob = 0.3f;
  std::vector<float> out(48000);
  for (size_t i = 0; i < out.size(); ++i) {
    frame.input = (i % 100) < 50 ? 5.f : -5.f;
    ripples.process(frame);
    out[i] = frame.lp4;
    REQUIRE(std::isfinite(frame.lp4));
  }
  REQUIRE(eg13::test::rms(out, 4800, 48000) > 0.5);

  shelves::ShelvesEngine eq;
  eq.setSampleRate(48000.f);
  shelves::ShelvesEngine::Frame f = {};
  f.hs_freq_knob = f.p1_freq_knob = f.p2_freq_knob = f.ls_freq_knob = 0.5f;
  f.p1_q_knob = f.p2_q_knob = 0.5f;  // gains 0: flat
  std::vector<float> in(48000), flat(48000);
  for (size_t i = 0; i < in.size(); ++i) {
    in[i] = 5.f * float(std::sin(6.283185307179586 * 1000.0 * double(i) / 48000.0));
    f.main_in = in[i];
    eq.process(f);
    flat[i] = f.main_out;
  }
  const double gainDb = 20.0 * std::log10(eg13::test::rms(flat, 4800, 48000) / eg13::test::rms(in, 4800, 48000));
  INFO("flat EQ gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 0.5);
}

TEST_CASE("sst-filters offers 205 filter types and every one prepares") {
  static float lines[4][8192];
  int count = 0;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) {
      sfpp::Filter filter;
      filter.setSampleRateAndBlockSize(48000.0, 16);
      for (int lane = 0; lane < 4; ++lane) filter.provideDelayLine(lane, lines[lane]);
      filter.setFilterModel(model);
      filter.setModelConfiguration(config);
      INFO(sfpp::details::FilterPayload::displayName(model, config));
      REQUIRE(filter.prepareInstance());
      ++count;
    }
  }
  REQUIRE(count == 205);
}

TEST_CASE("Switching sst-filters types does not allocate once its tables exist") {
  static float lines[4][8192];
  std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> all;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) all.push_back({model, config});
  }
  sfpp::Filter filter;
  filter.setSampleRateAndBlockSize(48000.0, 16);
  for (int lane = 0; lane < 4; ++lane) filter.provideDelayLine(lane, lines[lane]);
  size_t allocations = 0;
  {
    eg13::test::ScopedAllocationCounter counter;
    for (const auto& [model, config] : all) {
      filter.setFilterModel(model);
      filter.setModelConfiguration(config);
      (void)filter.prepareInstance();
      filter.setStereo();
      filter.makeCoefficients(0, 0.f, 0.5f, 0.5f);
      filter.copyCoefficientsFromVoiceToVoice(0, 1);
      filter.prepareBlock();
      float l = 0.f, r = 0.f;
      for (int i = 0; i < 16; ++i) filter.processStereoSample(0.1f, 0.1f, l, r);
      filter.concludeBlock();
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("Warps' modulator runs from the unmodified upstream sources") {
  auto modulator = std::make_unique<warps::Modulator>();
  std::memset(static_cast<void*>(modulator.get()), 0, sizeof(warps::Modulator));  // Warps.cpp:61
  modulator->Init(96000.0f);
  warps::Parameters* p = modulator->mutable_parameters();
  p->channel_drive[0] = 0.8f;
  p->channel_drive[1] = 0.8f;
  p->modulation_algorithm = 3.0f / 8.0f;  // digital ring modulation
  p->modulation_parameter = 0.5f;
  warps::ShortFrame in[60], out[60];
  double energy = 0.0;
  for (int block = 0; block < 200; ++block) {
    for (int i = 0; i < 60; ++i) {
      const double t = double(block * 60 + i) / 96000.0;
      in[i].l = short(10000.0 * std::sin(6.283185307179586 * 440.0 * t));
      in[i].r = short(10000.0 * std::sin(6.283185307179586 * 110.0 * t));
    }
    modulator->Process(in, out, 60);
    for (const auto& frame : out) energy += double(frame.l) * double(frame.l);
  }
  REQUIRE(energy > 0.0);
}
```

- [ ] **Step 4: Run it to verify it fails**

Run: `cmake --preset mac && cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`Ripples/ripples.hpp`, `sst/filters++.h`, `rack.hpp` not found; Warps symbols undefined).

- [ ] **Step 5: Write the Rack stand-in (`core/engine/rack_shim/rack.hpp`)**

```cpp
#pragma once

// The parts of the VCV Rack SDK that AudibleInstruments' Ripples and Shelves models use
// (src/Ripples/ripples.hpp, src/Shelves/shelves.hpp, their aafilter.hpp and sos.hpp), so those
// models compile unmodified without Rack. Each definition follows Rack v2 (VCVRack/Rack
// 061ccf6, GPL-3.0-or-later): file:line cited per item. Only Rack's exp/log approximations
// (sse_mathfun) are replaced by <cmath>; the models only use them for CV levels.

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#else
#ifndef SIMDE_ENABLE_NATIVE_ALIASES
#define SIMDE_ENABLE_NATIVE_ALIASES  // _mm_* names on arm64, as Rack builds with simde there
#endif
#include "simde/x86/sse.h"
#endif

namespace rack {

namespace math {

// include/math.hpp:106-108
inline float clamp(float x, float a = 0.f, float b = 1.f) { return std::fmax(std::fmin(x, b), a); }

// include/math.hpp:151-153
inline float rescale(float x, float xMin, float xMax, float yMin, float yMax) {
  return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

}  // namespace math

namespace simd {

// include/simd/Vector.hpp:34-94: four floats in one SSE register.
struct float_4 {
  union {
    __m128 v;
    float s[4];
  };
  float_4() = default;  // trivial, so float_4 can sit in the models' unions
  float_4(__m128 x) : v(x) {}
  float_4(float x) { v = _mm_set1_ps(x); }
  float_4(float x1, float x2, float x3, float x4) { v = _mm_setr_ps(x1, x2, x3, x4); }
  float& operator[](int i) { return s[i]; }
  const float& operator[](int i) const { return s[i]; }
};

// include/simd/Vector.hpp operators (non-member so a float converts on either side).
inline float_4 operator+(float_4 a, float_4 b) { return _mm_add_ps(a.v, b.v); }
inline float_4 operator-(float_4 a, float_4 b) { return _mm_sub_ps(a.v, b.v); }
inline float_4 operator*(float_4 a, float_4 b) { return _mm_mul_ps(a.v, b.v); }
inline float_4 operator/(float_4 a, float_4 b) { return _mm_div_ps(a.v, b.v); }
inline float_4 operator-(float_4 a) { return _mm_sub_ps(_mm_setzero_ps(), a.v); }
inline float_4& operator+=(float_4& a, float_4 b) { return a = a + b; }
inline float_4& operator-=(float_4& a, float_4 b) { return a = a - b; }
inline float_4& operator*=(float_4& a, float_4 b) { return a = a * b; }
inline float_4& operator/=(float_4& a, float_4 b) { return a = a / b; }

inline float_4 fmin(float_4 a, float_4 b) { return _mm_min_ps(a.v, b.v); }
inline float_4 fmax(float_4 a, float_4 b) { return _mm_max_ps(a.v, b.v); }

// include/simd/functions.hpp:249-259
using math::clamp;
inline float_4 clamp(float_4 x, float_4 a = 0.f, float_4 b = 1.f) { return fmin(fmax(x, a), b); }
using math::rescale;
inline float_4 rescale(float_4 x, float_4 xMin, float_4 xMax, float_4 yMin, float_4 yMax) {
  return yMin + (x - xMin) / (xMax - xMin) * (yMax - yMin);
}

// include/simd/functions.hpp:231-233: exp(b * log(a)), per lane with <cmath>.
inline float_4 pow(float a, float_4 b) {
  const float l = std::log(a);
  return float_4(std::exp(b[0] * l), std::exp(b[1] * l), std::exp(b[2] * l), std::exp(b[3] * l));
}

}  // namespace simd

namespace dsp {

// include/dsp/filter.hpp:13-52
template <typename T = float>
struct TRCFilter {
  T c = 0.f;
  T xstate[1];
  T ystate[1];
  TRCFilter() { reset(); }
  void reset() {
    xstate[0] = 0.f;
    ystate[0] = 0.f;
  }
  void setCutoff(T r) { c = 2.f / r; }
  void setCutoffFreq(T f) { setCutoff(2.f * float(M_PI) * f); }
  void process(T x) {
    T y = (x + xstate[0] - ystate[0] * (1 - c)) / (1 + c);
    xstate[0] = x;
    ystate[0] = y;
  }
  T lowpass() { return ystate[0]; }
  T highpass() { return xstate[0] - ystate[0]; }
};
typedef TRCFilter<> RCFilter;

// include/dsp/filter.hpp:132-155
template <typename T = float>
struct TSlewLimiter {
  T out = 0.f;
  T rise = 0.f;
  T fall = 0.f;
  void reset() { out = 0.f; }
  void setRiseFall(T r, T f) {
    rise = r;
    fall = f;
  }
  T process(T deltaTime, T in) {
    out = simd::clamp(in, out - fall * deltaTime, out + rise * deltaTime);
    return out;
  }
};
typedef TSlewLimiter<> SlewLimiter;

}  // namespace dsp

namespace random {

// Rack's random::uniform() (include/random.hpp:125-126) draws from a per-thread xoroshiro128+.
// Ripples only uses it for 1e-6 V of noise to start self-oscillation (ripples.hpp:193-194), so
// a per-thread xorshift64 serves; seed() makes renders repeatable in tests.
inline thread_local uint64_t tState = 0x9E3779B97F4A7C15ull;
inline void seed(uint64_t s) { tState = s != 0 ? s : 0x9E3779B97F4A7C15ull; }
inline float uniform() {
  tState ^= tState << 13;
  tState ^= tState >> 7;
  tState ^= tState << 17;
  return float(tState >> 40) * (1.0f / 16777216.0f);
}

}  // namespace random

}  // namespace rack
```

- [ ] **Step 6: Wire the build**

`cmake/vcv.cmake`:

```cmake
# VCVRack/AudibleInstruments (GPL-3.0-or-later): the Ripples and Shelves models, header-only,
# compiled unmodified through EG13's stand-in for the parts of the Rack SDK they use
# (core/engine/rack_shim/rack.hpp). simde (MIT) supplies the SSE intrinsics on arm64.
add_library(eg13_vcv INTERFACE)
target_include_directories(eg13_vcv SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party/AudibleInstruments/src
  ${CMAKE_SOURCE_DIR}/third_party/simde)
target_include_directories(eg13_vcv INTERFACE ${CMAKE_SOURCE_DIR}/core/engine/rack_shim)
```

`cmake/sst.cmake`:

```cmake
# surge-synthesizer/sst-filters and sst-basic-blocks (GPL-3.0-or-later), header-only, at their
# last_cpp_17 tags. sst-basic-blocks' simd/setup.h includes simde (MIT) on every platform.
add_library(eg13_sst INTERFACE)
target_include_directories(eg13_sst SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/third_party/sst-filters/include
  ${CMAKE_SOURCE_DIR}/third_party/sst-basic-blocks/include
  ${CMAKE_SOURCE_DIR}/third_party/simde)
```

In `CMakeLists.txt`, after `include(cmake/r8brain.cmake)`:

```cmake
include(cmake/vcv.cmake)
include(cmake/sst.cmake)
```

In `cmake/eurorack.cmake`, add to the `add_library(eg13_eurorack STATIC ...)` list, before `${EURORACK_DIR}/stmlib/dsp/units.cc`:

```cmake
  ${EURORACK_DIR}/warps/dsp/modulator.cc
  ${EURORACK_DIR}/warps/dsp/oscillator.cc
  ${EURORACK_DIR}/warps/dsp/vocoder.cc
  ${EURORACK_DIR}/warps/dsp/filter_bank.cc
  ${EURORACK_DIR}/warps/resources.cc
```

In `core/CMakeLists.txt`, change the link line to:

```cmake
target_link_libraries(eg13_core PRIVATE eg13_eurorack eg13_nlohmann eg13_r8brain eg13_vcv eg13_sst)
```

In `tests/CMakeLists.txt`, change the link line and add the data path:

```cmake
target_link_libraries(eg13_tests PRIVATE eg13_core eg13_eurorack eg13_r8brain eg13_vcv eg13_sst Catch2::Catch2WithMain Threads::Threads)
target_compile_definitions(eg13_tests PRIVATE EG13_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
```

- [ ] **Step 7: Run the tests (macOS and Linux)**

Run: `cmake --preset mac && cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all tests pass, including the seven new ones.

Run the Docker command from Global Constraints.
Expected: GCC 13 builds `eg13_core` with `-Werror` and every test passes. (Planning compiled these headers with GCC 13 on x86_64 and aarch64 with `-Wall -Wextra -Werror` as SYSTEM includes.)

- [ ] **Step 8: Credit the new code in `README.md`**

In "Credits and licences", after the Eurorack DSP line, add:

```markdown
- Analog Filter and EQ: the Ripples and Shelves models from VCV's AudibleInstruments (© 2020 Tyler Coy and contributors), GPL-3.0-or-later.
- Classic Filters: sst-filters and sst-basic-blocks by the Surge Synth Team, GPL-3.0-or-later.
- simde (SIMD Everywhere): MIT licence.
```

- [ ] **Step 8a: Start the user manual (`docs/manual/`)**

The manual documents every user-facing behaviour choice in plain, musician-facing words (Global Constraints). This step backfills M2's choices (from the spec, the M2 plan and its ledger `.superpowers/sdd/2026-10-20-m2-sources/progress.md`); each later task adds its own. Create these six files with exactly this content.

`docs/manual/index.md`:

````markdown
# EG13 manual

EG13 is a two-part synthesizer (and, as EG13 FX, an audio effect) built from the open-source Eurorack firmware by Émilie Gillet. Each part plays up to eight voices. Each voice runs a **Source**, then a **Filter**, then a **Shaper**.

This manual describes how EG13 behaves, including the choices it makes where the original modules leave things open.

- [Engines](engines.md): every Source, Filter and Shaper engine and its controls.
- [Parts and routing](parts-and-routing.md): parts, voices, the engine slots, the Input source, engine switching and the default patches.
- [Latency](latency.md): how much delay EG13 reports and why.
- [Smoothing](smoothing.md): how each engine reacts when you move a control.
- [Known limitations](limitations.md).
````

`docs/manual/engines.md`:

````markdown
# Engines

Every engine shows its controls as P1, P2, … in the host. A slot has more parameters than most engines use: the unused ones are named "(unused)" and show "-". The Source slot has 16 parameters.

## Sources

### Macro Oscillator (based on Plaits)

Controls: **Model** (24 models, from Virtual analog to Hi-hat), **Harmonics**, **Timbre**, **Morph**, **Frequency** (±12 semitones), **Decay**, **LPG Colour** and **Aux Mix**.

- Each note triggers the model and opens its low-pass gate with the note's velocity.
- Decay and LPG Colour are the settings that Plaits keeps behind its Morph and Timbre knobs.
- **Aux Mix** blends the module's AUX output into the main output (0 % = OUT only). The AUX output can also feed the Ring Mod / Vocoder's carrier or modulator (*Source AUX*), whatever Aux Mix is set to.
- The output is mono.

### Digital Oscillator (based on Braids)

Controls: **Shape** (47 shapes), **Timbre**, **Color**, **Frequency** (±12 semitones), **Signature**, **Bit Depth** (2 to 16 bits) and **Sample Rate** (4 kHz to 96 kHz).

- Braids' own envelope and quantizer are not exposed; EG13's envelopes replace them.
- A level follows each note's gate and velocity, before the Signature waveshaper as on the module, so velocity also changes how hard the waveshaper is driven.
- The output is mono.

### Modal Synth (based on Elements)

All 16 panel controls: **Model** (Modal, String, Strings), **Contour**, **Bow**, **Blow**, **Strike**, **Flow**, **Mallet**, **Bow Timbre**, **Blow Timbre**, **Strike Timbre**, **Geometry**, **Brightness**, **Damping**, **Position**, **Space** and **Ext In**.

- **Ext In** routes the part's Input like the module's two EXT IN jacks: *Off*, *Blow*, *Strike* or *Both* (default *Strike*).
  - *Blow* feeds the Input into the blow exciter. That exciter is gated by the exciter envelope, so with *Blow* the Input is silent until you hold a note, as on the hardware.
  - *Strike* excites the resonator directly, with or without a note.
- The output is stereo; Space sets its width.
- Controls are smoothed as in the firmware, so some move slowly: Space and Position take about half a second to settle after a jump ([Smoothing](smoothing.md)).
- In the instrument, sound excited by the Input arrives up to about 6 ms after the Dry signal ([Latency](latency.md)).

### Resonator (based on Rings)

Controls: **Model** (Modal, Sympathetic, String, FM Voice, Sympathetic Q, String + Reverb), **Structure**, **Brightness**, **Damping**, **Position**, **Exciter**, **Frequency** (±12 semitones), **Spread** and **Polyphony**.

- **Exciter:** *Internal* (each note strums the resonator), *Input* (the part's Input excites it; notes only set the pitch), or *Both*.
- **Polyphony** 1, 2 or 4: with more than one voice, earlier strums keep ringing at their own pitch while new ones start.
- **Spread** 0 folds the module's odd and even outputs to mono, the way the module does when only OUT is patched. Spread 1 puts the odd output on the left and the even output on the right.
- The played note reaches the Resonator as the module's V/OCT input against a tonic of C4 plus **Frequency**.
  - With the Sympathetic String model at Structure below about 1/7, the strings drone at that tonic, as on the hardware with V/OCT patched.
  - Small pitch bends pass through Rings' own note filter, so they are smoothed.
  - Moving **Frequency** glides briefly rather than jumping.
- The Resonator responds to the Input even before the first note (useful in EG13 FX).
- Its even output can also feed the Ring Mod / Vocoder's carrier or modulator (*Source AUX*).

### Audio Input

Plays the part's Input through each voice: notes open a gate with **Attack** (1 ms to 2 s) and **Release** (5 ms to 5 s); **Gain** sets the level (−24 to +12 dB). The Input is stereo when the host sends stereo.
````

`docs/manual/parts-and-routing.md`:

````markdown
# Parts and routing

## Parts and voices

EG13 has two parts, A and B. Each plays up to eight voices. Each voice runs Source → Filter → Shaper. Every engine of every slot is ready in every voice, so changing an engine never loads anything.

The master **Mode** decides which notes reach which part: *Single* (only A plays), *Layer* (both play every note), *Split* (A below the split note, B from it up) or *Multi* (each part on its own MIDI channel). In Single mode Part B is silent but still runs, so it can feed Part A (see below).

## Engine switching

Each engine remembers its own settings in each part. Switching Source from Macro Oscillator to Digital Oscillator and back brings back the Macro Oscillator exactly as you left it. The host's parameters change to the new engine's values; this change is not recorded as automation.

Switching the **Source** engine is a hard cut: the old engine stops at once, and a held note restarts on the new engine after the plugin's latency.

## Input and Input source

The plugin's audio input (the instrument's sidechain, or EG13 FX's main input) is the **Input**. When nothing is connected, the Input is silent. Each part has an **Input source** setting:

- *External*: the plugin's Input.
- *Other part*: the other part's output.
- *Both*: the two summed.

Part A runs before Part B. B therefore hears A with no extra delay, while A hears B 64 samples later (or one host block, if the host's blocks are shorter). Every signal fed from one part to the other passes through a soft limiter: it is untouched up to full scale and never exceeds twice full scale, so a loop between the parts cannot run away.

Each part's **Output** goes to *Main*, to *Bus 2* (the instrument's second output) or to *Other part only*. With *Other part only* the part is not heard; it only feeds the other part.

**Dry** mixes the Input into the main output, delayed by the plugin's latency so it lines up with the processed sound.

## Default patches

- **EG13:** Part A with the Macro Oscillator.
- **EG13 FX:**
  - Part A plays the Input through **Audio Input**, with Trigger set to *Always on*, so it processes audio with no MIDI.
  - Dry is 0 and the master level is 100 %.
````

`docs/manual/latency.md`:

````markdown
# Latency

Each engine runs at its module's own sample rate and is converted to and from the host's rate. Conversion takes time, and EG13 reports that time to the host so the host can compensate.

EG13 reports **one fixed latency**, and every engine is delayed to match the slowest one. That way changing engines never moves your sound in time. The cost is that a patch pays for the slowest engine even when that engine isn't in use.

| Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| EG13 (note onsets aligned) | 152 samples | 221 samples (4.6 ms) | 441 samples |
| EG13 FX (input aligned with Dry) | 428 samples | 491 samples (10.2 ms) | 981 samples |

The Modal Synth's 32 kHz engine sets these figures.

- **EG13** aligns note onsets. Sound that the Input excites through the Modal Synth or Resonator therefore arrives up to about 6 ms after the Dry signal.
- **EG13 FX** aligns the processed Input exactly with Dry.
````

`docs/manual/smoothing.md`:

````markdown
# Smoothing

When a control jumps (an automation step, or a modulation change), each engine follows it the way its original module would: some smooth it, some don't.

- **Macro Oscillator:** Harmonics, Timbre, Morph, Frequency, Decay and LPG Colour follow Plaits' knob filter: Timbre, Morph, Decay and LPG Colour settle in about 25 ms (time constant), Harmonics and Frequency in about 50 ms. Model switches at once. Aux Mix is EG13's own control; a 5 ms fade removes clicks.
- **Digital Oscillator:** no smoothing, as on the hardware (Braids reads its knobs directly). Fast automation of Timbre or Color can therefore zipper.
- **Modal Synth:** Elements' firmware filters. Most controls have a time constant of about 50 ms; Flow, Mallet and Geometry are a little slower, and Position and Space take about half a second to settle. Model and Ext In switch at once.
- **Resonator:** Rings' knob filter (time constant about 50 ms) on Structure, Brightness, Damping, Position and Frequency (so Frequency glides briefly). Model, Exciter and Polyphony switch at once. Spread is EG13's own control; a 5 ms fade removes clicks.
- **Audio Input:** Gain has a 1 ms fade. Attack and Release are times, so they need none.

After a reset, or when a voice starts from silence, every control starts at its current value with no glide.
````

`docs/manual/limitations.md`:

````markdown
# Known limitations

- **Levels and headroom:** EG13 does not yet manage gain between voices and parts. Sixteen loud voices can peak well above full scale (5 to 13 times, measured on chords). Keep the master level down until gain staging lands with the modulation milestone (M4).
- **Resonator velocity:** a new note's velocity rescales the whole Resonator output, including voices still ringing, which can click (fixed in M4).
- **Modal Synth input:** the firmware's noise gate on the external input is not ported.
- **Part B in Single mode** still runs and uses CPU, so it can feed Part A.
````

- [ ] **Step 9: Commit**

```bash
git add .gitmodules third_party/AudibleInstruments third_party/sst-filters third_party/sst-basic-blocks third_party/simde \
  CMakeLists.txt cmake/vcv.cmake cmake/sst.cmake cmake/eurorack.cmake core/CMakeLists.txt core/engine/rack_shim \
  tests/CMakeLists.txt tests/test_third_party_smoke.cpp docs/superpowers/specs/2026-10-20-eg13-design.md README.md docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Third-party DSP for M3; spec amendments; user manual with M2's behaviour"
```

---
### Task 2: Engine plumbing: side inputs, AUX output, engine latency, stereo hook

**Files:**
- Modify: `core/engine/voice_engine.h`, `core/engine/native_rate_wrapper.h`, `core/engine/native_rate_wrapper.cpp`, `tests/alloc_counter.h`, `tests/alloc_counter.cpp`, `tests/test_engines.h`
- Test: `tests/test_native_rate_wrapper.cpp`, `tests/test_alloc_counter.cpp`

**Interfaces:**
- Consumes: M2 `VoiceEngine`, `NativeRateWrapper`.
- Produces:
  - `EngineInfo` gains three members with defaults (existing initialisers stay valid): `bool hasAux = false;` (Source: `process()` also writes `out[2]` once `setAuxEnabled(true)`), `int sideInputs = 0;` (Filter/Shaper: `process()` also reads `in[2] .. in[1 + sideInputs]`, mono side signals that may each be `nullptr`; at most 2), `SideSourceFn sideSource = nullptr;` (which signal each side input takes, from the engine's parameters).
  - `enum class SideSource : int { Voice = 0, SourceAux = 1, Input = 2, OtherPart = 3, None = 4 };`, `using SideSourceFn = SideSource (*)(int input, const ParamValues& p);` and `SideSource sideSourceOf(float normalized)` (stepped quarters Voice / Source AUX / Input / Other part, NaN → Voice). `Voice` means the engine uses its own input (the voice) and `None` that it makes the signal itself; the Voice passes `nullptr` for both.
  - `VoiceEngine` gains `virtual void setStereo(bool) {}` (Filter/Shaper: run the right instance only while true), `virtual void setAuxEnabled(bool) {}` (main thread, before `prepare`), `virtual int nativeLatency() const { return 0; }` (engine-internal latency in native samples).
  - `NativeRateWrapper::kMaxChannels = 4`. It converts `in[0..1]` plus `in[2 .. 1 + sideInputs]`, and `out[0..1]` plus `out[2]` when the engine `hasAux` and aux is enabled. Its latencies include `nativeLatency()` converted to host samples. `setStereo` and `setAuxEnabled` are forwarded to the engine.
  - `test::ScopedAllocationCounter::bytes()` (bytes requested); aligned `operator new` is counted too.
  - Test engines in `tests/test_engines.h`: `SideEchoEngine` (two side inputs, sources picked by P1 and P2 as `sideSourceOf` quarters; out L = side 1, out R = side 2), `AuxImpulseEngine` (impulse into AUX at note-on), `FixedLatencyEngine` (delays in L by 60 native samples and reports `nativeLatency() == 60`), `StereoProbeEngine` (records the last `setStereo`).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_alloc_counter.cpp`:

```cpp
#include <new>

TEST_CASE("ScopedAllocationCounter counts bytes and aligned allocations") {
  eg13::test::ScopedAllocationCounter counter;
  void* a = ::operator new(100);
  void* b = ::operator new(64, std::align_val_t(64));
  ::operator delete(a);
  ::operator delete(b, std::align_val_t(64));
  REQUIRE(counter.count() == 2);
  REQUIRE(counter.bytes() == 164);
}
```

Append to `tests/test_engines.h`, before the closing `}  // namespace eg13::test`:

```cpp
// Uses input and two side inputs: out L = side 1, out R = side 2 (zeros when absent). P1 and P2
// pick each side input's source (SideSource quarters).
class SideEchoEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in && in[2] ? in[2][i] : 0.0f;
      out[1][i] = in && in[3] ? in[3][i] : 0.0f;
    }
  }
  static SideSource source(int input, const ParamValues& p) { return sideSourceOf(p.p[input]); }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"sideecho", "Side echo", 0, nullptr, true, false, 2, source};
    return kInfo;
  }
};

// A source with an AUX output: one 1.0 sample into AUX (when enabled) after each note-on; L/R silent.
class AuxImpulseEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override { pending_ = false; }
  void noteOn(float, float) override { pending_ = true; }
  void noteOff() override {}
  void setNote(float) override {}
  void setAuxEnabled(bool on) override { aux_ = on; }
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = 0.0f;
      out[1][i] = 0.0f;
      if (aux_) out[2][i] = pending_ ? 1.0f : 0.0f;
      pending_ = false;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"auximpulse", "AUX impulse", 0, nullptr, false, true, 0, nullptr};
    return kInfo;
  }

 private:
  bool aux_ = false;
  bool pending_ = false;
};

// Delays input L by 60 native samples on both outputs and says so through nativeLatency().
class FixedLatencyEngine final : public VoiceEngine {
 public:
  static constexpr int kDelay = 60;
  void prepare(double, int) override { reset(); }
  void reset() override {
    for (float& s : line_) s = 0.0f;
    pos_ = 0;
  }
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  int nativeLatency() const override { return kDelay; }
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      const float delayed = line_[pos_];
      line_[pos_] = in ? in[0][i] : 0.0f;
      pos_ = (pos_ + 1) % kDelay;
      out[0][i] = delayed;
      out[1][i] = delayed;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"fixedlatency", "Fixed latency", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }

 private:
  float line_[kDelay] = {};
  int pos_ = 0;
};

// Passes input through and records the last setStereo() it was given.
class StereoProbeEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override {
    stereo_ = stereo;
    ++calls_;
  }
  void process(const float* const* in, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] : 0.0f;
      out[1][i] = in ? in[1][i] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"stereoprobe", "Stereo probe", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
  bool stereo() const { return stereo_; }
  int calls() const { return calls_; }

 private:
  bool stereo_ = false;
  int calls_ = 0;
};
```

Append to `tests/test_native_rate_wrapper.cpp`:

```cpp
namespace {

struct Channels {
  std::vector<float> c[4];
  explicit Channels(size_t n) {
    for (auto& v : c) v.assign(n, 0.0f);
  }
};

// Runs a wrapper over four-channel input (L, R, side 1, side 2; the sides nullable) and
// three-channel output (L, R, AUX).
Channels runAll(NativeRateWrapper& w, const Channels& in, bool sides, int chunk) {
  const size_t n = in.c[0].size();
  Channels out(n);
  const ParamValues p;
  for (size_t done = 0; done < n;) {
    const int count = int(std::min<size_t>(size_t(chunk), n - done));
    const float* i4[4] = {in.c[0].data() + done, in.c[1].data() + done, sides ? in.c[2].data() + done : nullptr,
                          sides ? in.c[3].data() + done : nullptr};
    float* o3[3] = {out.c[0].data() + done, out.c[1].data() + done, out.c[2].data() + done};
    w.process(i4, o3, p, count);
    done += size_t(count);
  }
  return out;
}

int peakAt(const std::vector<float>& x) {
  return int(std::max_element(x.begin(), x.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); }) -
             x.begin());
}

}  // namespace

TEST_CASE("sideSourceOf steps in quarters") {
  REQUIRE(sideSourceOf(0.0f) == SideSource::Voice);
  REQUIRE(sideSourceOf(0.26f) == SideSource::SourceAux);
  REQUIRE(sideSourceOf(0.5f) == SideSource::Input);
  REQUIRE(sideSourceOf(1.0f) == SideSource::OtherPart);
  REQUIRE(sideSourceOf(std::nanf("")) == SideSource::Voice);
}

TEST_CASE("Side inputs are converted with the input path's latency") {
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
    w.prepare(rate, 256);
    Channels in(8000);
    in.c[2][3000] = 1.0f;  // side 1 -> out L
    in.c[3][3000] = 1.0f;  // side 2 -> out R
    const auto out = runAll(w, in, true, 256);
    INFO("rate " << rate << " input latency " << w.inputLatencySamples());
    REQUIRE(peakAt(out.c[0]) == peakAt(out.c[1]));
    REQUIRE(std::abs(peakAt(out.c[0]) - 3000 - w.inputLatencySamples()) <= 4);
    REQUIRE(w.inputUnderruns() == 0);
  }
}

TEST_CASE("Missing side inputs read as silence") {
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.prepare(48000.0, 256);
  Channels in(4000);
  for (auto& s : in.c[2]) s = 0.5f;
  for (auto& s : in.c[3]) s = 0.5f;
  const auto out = runAll(w, in, false, 256);
  for (float s : out.c[0]) REQUIRE(s == 0.0f);
  for (float s : out.c[1]) REQUIRE(s == 0.0f);
}

TEST_CASE("AUX is converted like the main outputs") {
  for (double rate : {44100.0, 48000.0}) {
    NativeRateWrapper aux(std::make_unique<test::AuxImpulseEngine>(), 47872.34, 12);
    aux.setAuxEnabled(true);
    aux.prepare(rate, 256);
    NativeRateWrapper main(std::make_unique<test::ImpulseEngine>(), 47872.34, 12);
    main.prepare(rate, 256);
    REQUIRE(aux.latencySamples() == main.latencySamples());
    aux.noteOn(60.0f, 1.0f);
    main.noteOn(60.0f, 1.0f);
    Channels silent(2000);
    const auto a = runAll(aux, silent, false, 100);
    const auto m = runAll(main, silent, false, 100);
    REQUIRE(peakAt(a.c[2]) == peakAt(m.c[0]));
    for (float s : a.c[0]) REQUIRE(s == 0.0f);
  }
}

TEST_CASE("An engine's own latency is added to the wrapper's") {
  NativeRateWrapper bypassed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  bypassed.prepare(96000.0, 256);
  REQUIRE(bypassed.inputLatencySamples() == 60);
  REQUIRE(bypassed.latencySamples() == 60);
  Channels in(2000);
  in.c[0][500] = 1.0f;
  REQUIRE(peakAt(runAll(bypassed, in, false, 37).c[0]) == 560);

  NativeRateWrapper fixed(std::make_unique<test::FixedLatencyEngine>(), 96000.0, 60);
  NativeRateWrapper echo(std::make_unique<test::EchoEngine>(), 96000.0, 60);
  fixed.prepare(48000.0, 256);
  echo.prepare(48000.0, 256);
  REQUIRE(fixed.inputLatencySamples() - echo.inputLatencySamples() == 30);
}

TEST_CASE("setStereo reaches the wrapped engine") {
  auto probe = std::make_unique<test::StereoProbeEngine>();
  test::StereoProbeEngine* raw = probe.get();
  NativeRateWrapper w(std::move(probe), 0.0, 16);
  w.prepare(48000.0, 256);
  w.setStereo(true);
  REQUIRE(raw->stereo());
  w.setStereo(false);
  REQUIRE_FALSE(raw->stereo());
}

TEST_CASE("Four-channel conversion does not allocate") {
  NativeRateWrapper w(std::make_unique<test::SideEchoEngine>(), 96000.0, 60);
  w.prepare(44100.0, 256);
  w.setExtraDelay(11);
  std::vector<float> a(256, 0.25f), l(256), r(256), x(256);
  const float* in[4] = {a.data(), a.data(), a.data(), a.data()};
  float* out[3] = {l.data(), r.data(), x.data()};
  const ParamValues p;
  w.process(in, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 100; ++block) {
      in[2] = block % 3 ? a.data() : nullptr;
      in[3] = block % 5 ? a.data() : nullptr;
      w.process(in, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`bytes`, `sideSourceOf`, `setAuxEnabled`, `nativeLatency`, the 8-field `EngineInfo` initialisers).

- [ ] **Step 3: Replace `core/engine/voice_engine.h`**

```cpp
#pragma once

#include <cstddef>

namespace eg13 {

constexpr int kMaxSlotParams = 16;  // engines with fewer ignore the extra slots

// Post-modulation slot parameter values, normalized 0..1.
struct ParamValues {
  float p[kMaxSlotParams] = {};
};

using ParamFormatter = void (*)(float normalized, char* buf, size_t size);

// Where a side input of a Filter/Shaper engine (the Ring Mod / Vocoder's CARRIER and MODULATOR)
// takes its signal from. Voice: the engine uses its own input, the voice. None: the engine makes
// the signal itself (Warps' internal oscillator). The Voice passes nullptr for both.
enum class SideSource : int { Voice = 0, SourceAux = 1, Input = 2, OtherPart = 3, None = 4 };

// For side input `input` (0-based), the source the engine's parameters select.
using SideSourceFn = SideSource (*)(int input, const ParamValues& p);

struct EngineParamInfo {
  const char* name;
  float defaultValue;
  ParamFormatter format;
};

struct EngineInfo {
  const char* id;           // stable, saved in state
  const char* displayName;  // shown in the UI
  int paramCount;
  const EngineParamInfo* params;
  bool usesInput;           // the engine reads in[0], in[1] of process()
  bool hasAux = false;      // Source: also writes out[2], the module's AUX output, once enabled
  int sideInputs = 0;       // Filter/Shaper: also reads in[2] .. in[1 + sideInputs] (each nullable), <= 2
  SideSourceFn sideSource = nullptr;  // which signal each side input takes
};

// Per-voice sound generator or processor (Source, Filter, Shaper slots).
class VoiceEngine {
 public:
  virtual ~VoiceEngine() = default;
  // Main thread, never concurrently with process(). The only place that may allocate.
  virtual void prepare(double hostRate, int maxBlock) = 0;
  virtual void reset() = 0;
  virtual void noteOn(float note, float velocity) = 0;  // note: float MIDI note
  virtual void noteOff() = 0;
  virtual void setNote(float note) = 0;  // pitch change without a new note (glide, bend)
  // out[0], out[1]: n samples each (and out[2] for an enabled AUX), n <= the maxBlock given to
  // prepare(). in may be nullptr; in[2] .. in[1 + sideInputs] exist for engines with side inputs
  // and may each be nullptr.
  virtual void process(const float* const* in, float* const* out, const ParamValues& p,
                       int n) = 0;
  virtual const EngineInfo& info() const = 0;
  // Filter/Shaper (spec §5.1): run the right-channel instance only while the input is stereo.
  // Called before process(); engines without a second instance ignore it.
  virtual void setStereo(bool) {}
  // Sources with info().hasAux: write out[2] from now on. Main thread, before prepare(); off by
  // default so two-channel callers stay valid.
  virtual void setAuxEnabled(bool) {}
  // Latency the engine itself adds (e.g. a fixed-block FIFO), in samples at its native rate.
  virtual int nativeLatency() const { return 0; }
};

inline float clamp01(float x) {
  if (!(x > 0.0f)) return 0.0f;  // also maps NaN to 0
  return x > 1.0f ? 1.0f : x;
}

inline SideSource sideSourceOf(float normalized) {
  const int i = int(clamp01(normalized) * 4.0f);
  return SideSource(i < 4 ? i : 3);
}

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/engine/native_rate_wrapper.h`**

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "engine/voice_engine.h"

namespace r8b {
class CDSPResampler24;
}

namespace eg13 {

// Runs an engine at its native sample rate and block size, converting its
// output (and, for engines that read input, the host input) with r8brain.
// At the native rate it is a bit-exact pass-through. Notes and parameters
// reach the engine at native block boundaries (the hardware's control rate).
// nativeRate <= 0 runs the engine at the host rate.
// Channels: in[0], in[1] (+ in[2], in[3], the engine's side inputs); out[0], out[1]
// (+ out[2], AUX, for hasAux engines with AUX enabled). Every channel is converted alike, so
// they stay sample-aligned.
class NativeRateWrapper final : public VoiceEngine {
 public:
  // ~2.3 ms for Plaits-rate engines; up to ~4.6 ms for 32 kHz engines (r8brain 7.6, measured).
  static constexpr double kTransitionBandPercent = 30.0;
  static constexpr int kMaxChannels = 4;  // inputs L, R and two side inputs; outputs L, R, AUX

  NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate, int nativeBlock);
  ~NativeRateWrapper() override;

  // Main thread. Resets the extra delay: call setExtraDelay after every prepare.
  void prepare(double hostRate, int maxBlock) override;
  void reset() override;
  void noteOn(float note, float velocity) override;
  void noteOff() override;
  void setNote(float note) override;
  // in: host-rate input (nullable = silence); converted for engines that use it.
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;
  void setStereo(bool stereo) override;
  void setAuxEnabled(bool on) override;  // main thread, before prepare

  void setExtraDelay(int samples);  // main thread, after prepare (allocates)

  // All latencies include the engine's own nativeLatency(), in host samples.
  int outputLatencySamples() const { return latency_; }
  int inputLatencySamples() const { return inputLatency_; }
  int extraDelaySamples() const { return extra_; }
  int latencySamples() const { return latency_ + extra_; }
  int inputPathLatencySamples() const { return inputLatency_ + extra_; }
  size_t inputUnderruns() const { return underruns_; }
  size_t inputOverruns() const { return overruns_; }

 private:
  void pushInput(const float* const* in, int n);
  void renderNativeBlock(const ParamValues& p);
  void applyExtraDelay(float* const* out, int n);

  std::unique_ptr<VoiceEngine> inner_;
  double nativeRate_;
  int nativeBlock_;
  bool bypass_ = true;
  bool usesInput_ = false;
  bool auxEnabled_ = false;
  int inChannels_ = 0;   // 0, or 2 + the engine's side inputs
  int outChannels_ = 2;  // 2, or 3 with AUX
  int latency_ = 0;
  int inputLatency_ = 0;
  int engineLatency_ = 0;  // the engine's nativeLatency() in host samples (part of both above)

  // Output path: native -> host.
  std::unique_ptr<r8b::CDSPResampler24> resampler_[kMaxChannels];
  std::vector<float> native_[kMaxChannels];
  std::vector<double> scratch_;
  std::vector<float> fifo_[kMaxChannels];
  size_t fifoRead_ = 0;
  size_t fifoSize_ = 0;
  size_t zeroPad_ = 0;  // host samples of silence owed after reset()

  // Input path: host -> native.
  std::unique_ptr<r8b::CDSPResampler24> inputResampler_[kMaxChannels];
  std::vector<double> inputScratch_;
  std::vector<float> inputFifo_[kMaxChannels];  // ring buffers
  size_t inputRead_ = 0;
  size_t inputSize_ = 0;
  size_t inputPrime_ = 0;  // native samples of silence queued at reset()
  std::vector<float> nativeIn_[kMaxChannels];
  size_t underruns_ = 0;
  size_t overruns_ = 0;

  // Alignment delay.
  int extra_ = 0;
  std::vector<float> delay_[kMaxChannels];
  size_t delayPos_ = 0;
};

}  // namespace eg13
```

- [ ] **Step 5: Replace `core/engine/native_rate_wrapper.cpp`**

```cpp
#include "engine/native_rate_wrapper.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "CDSPResampler.h"

namespace eg13 {

NativeRateWrapper::NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate,
                                     int nativeBlock)
    : inner_(std::move(inner)), nativeRate_(nativeRate), nativeBlock_(nativeBlock) {}

NativeRateWrapper::~NativeRateWrapper() = default;

void NativeRateWrapper::setAuxEnabled(bool on) {
  auxEnabled_ = on;
  inner_->setAuxEnabled(on);
}

void NativeRateWrapper::setStereo(bool stereo) { inner_->setStereo(stereo); }

void NativeRateWrapper::prepare(double hostRate, int maxBlock) {
  const double native = nativeRate_ > 0.0 ? nativeRate_ : hostRate;
  bypass_ = std::fabs(hostRate - native) < 1e-6;
  inner_->prepare(native, bypass_ ? std::max(maxBlock, nativeBlock_) : nativeBlock_);
  const EngineInfo& info = inner_->info();
  usesInput_ = info.usesInput;
  inChannels_ = usesInput_ ? 2 + std::clamp(info.sideInputs, 0, 2) : 0;
  outChannels_ = info.hasAux && auxEnabled_ ? 3 : 2;
  latency_ = 0;
  inputLatency_ = 0;
  inputPrime_ = 0;
  scratch_.assign(size_t(nativeBlock_), 0.0);
  inputScratch_.assign(size_t(maxBlock), 0.0);
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    resampler_[ch].reset();
    inputResampler_[ch].reset();
    native_[ch].assign(size_t(nativeBlock_), 0.0f);
    nativeIn_[ch].assign(size_t(nativeBlock_), 0.0f);
    fifo_[ch].clear();
    inputFifo_[ch].clear();
  }
  if (!bypass_) {
    for (int ch = 0; ch < outChannels_; ++ch) {
      resampler_[ch] = std::make_unique<r8b::CDSPResampler24>(native, hostRate, nativeBlock_,
                                                              kTransitionBandPercent);
      fifo_[ch].assign(size_t(resampler_[ch]->getMaxOutLen(nativeBlock_)), 0.0f);
    }
    latency_ = int(std::lround(double(resampler_[0]->getInLenBeforeOutPos(0)) * hostRate / native));
    if (usesInput_) {
      for (int ch = 0; ch < inChannels_; ++ch) {
        inputResampler_[ch] = std::make_unique<r8b::CDSPResampler24>(hostRate, native, maxBlock,
                                                                     kTransitionBandPercent);
      }
      const int withheld = inputResampler_[0]->getInLenBeforeOutPos(0);
      inputPrime_ = size_t(2 * nativeBlock_ + int(std::ceil(double(withheld) * native / hostRate)) + 1);
      const size_t capacity = inputPrime_ + size_t(inputResampler_[0]->getMaxOutLen(maxBlock)) +
                              size_t(std::ceil(double(latency_) * native / hostRate)) +
                              size_t(2 * nativeBlock_);
      for (int ch = 0; ch < inChannels_; ++ch) inputFifo_[ch].assign(capacity, 0.0f);
      inputLatency_ = latency_ + int(std::lround(double(inputPrime_) * hostRate / native));
    }
  }
  // The engine's own latency (a fixed-block FIFO) delays both paths.
  engineLatency_ = int(std::lround(double(inner_->nativeLatency()) * hostRate / native));
  latency_ += engineLatency_;
  if (usesInput_) inputLatency_ += engineLatency_;
  extra_ = 0;
  for (auto& line : delay_) line.clear();
  reset();
}

void NativeRateWrapper::setExtraDelay(int samples) {
  extra_ = std::max(0, samples);
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    delay_[ch].assign(ch < outChannels_ ? size_t(extra_) : 0, 0.0f);
  }
  delayPos_ = 0;
}

void NativeRateWrapper::reset() {
  inner_->reset();
  for (auto& resampler : resampler_) {
    if (resampler) resampler->clear();
  }
  for (auto& resampler : inputResampler_) {
    if (resampler) resampler->clear();
  }
  fifoRead_ = 0;
  fifoSize_ = 0;
  // r8brain skips its own latency after clear(); pad so the onset lands at
  // latencySamples() like it does in steady state. The engine's own latency is
  // real delay inside the engine, so only the converter's part is padded.
  zeroPad_ = bypass_ ? 0 : size_t(latency_ - engineLatency_);
  inputRead_ = 0;
  inputSize_ = inputPrime_;
  for (auto& ring : inputFifo_) std::fill(ring.begin(), ring.end(), 0.0f);
  underruns_ = 0;
  overruns_ = 0;
  for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
  delayPos_ = 0;
}

void NativeRateWrapper::noteOn(float note, float velocity) { inner_->noteOn(note, velocity); }

void NativeRateWrapper::noteOff() { inner_->noteOff(); }

void NativeRateWrapper::setNote(float note) { inner_->setNote(note); }

const EngineInfo& NativeRateWrapper::info() const { return inner_->info(); }

void NativeRateWrapper::process(const float* const* in, float* const* out, const ParamValues& p,
                                int n) {
  if (bypass_) {
    inner_->process(usesInput_ ? in : nullptr, out, p, n);
  } else {
    if (usesInput_) pushInput(in, n);
    for (int i = 0; i < n; ++i) {
      if (zeroPad_ > 0) {
        for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = 0.0f;
        --zeroPad_;
        continue;
      }
      // The converter withholds its first outputs, so a block can yield nothing.
      while (fifoSize_ == 0) renderNativeBlock(p);
      for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = fifo_[ch][fifoRead_];
      ++fifoRead_;
      --fifoSize_;
    }
  }
  if (extra_ > 0) applyExtraDelay(out, n);
}

void NativeRateWrapper::pushInput(const float* const* in, int n) {
  const size_t capacity = inputFifo_[0].size();
  int produced = 0;
  double* converted[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  for (int ch = 0; ch < inChannels_; ++ch) {
    const float* src = in != nullptr ? in[ch] : nullptr;  // a side input may be absent
    for (int i = 0; i < n; ++i) inputScratch_[size_t(i)] = src != nullptr ? double(src[i]) : 0.0;
    produced = inputResampler_[ch]->process(inputScratch_.data(), n, converted[ch]);
  }
  size_t count = size_t(produced);
  size_t skip = 0;
  if (count > capacity) {  // cannot happen with the capacity set in prepare()
    skip = count - capacity;
    count = capacity;
    ++overruns_;
  }
  if (inputSize_ + count > capacity) {  // drop the oldest samples to fit
    const size_t drop = inputSize_ + count - capacity;
    inputRead_ = (inputRead_ + drop) % capacity;
    inputSize_ -= drop;
    ++overruns_;
  }
  for (int ch = 0; ch < inChannels_; ++ch) {
    for (size_t j = 0; j < count; ++j) {
      inputFifo_[ch][(inputRead_ + inputSize_ + j) % capacity] = float(converted[ch][skip + j]);
    }
  }
  inputSize_ += count;
}

void NativeRateWrapper::renderNativeBlock(const ParamValues& p) {
  const float* nativeIn[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  if (usesInput_) {
    const size_t capacity = inputFifo_[0].size();
    const size_t take = std::min(inputSize_, size_t(nativeBlock_));
    if (take < size_t(nativeBlock_)) ++underruns_;
    for (int ch = 0; ch < inChannels_; ++ch) {
      for (size_t j = 0; j < size_t(nativeBlock_); ++j) {
        nativeIn_[ch][j] = j < take ? inputFifo_[ch][(inputRead_ + j) % capacity] : 0.0f;
      }
      nativeIn[ch] = nativeIn_[ch].data();
    }
    inputRead_ = (inputRead_ + take) % capacity;
    inputSize_ -= take;
  }
  float* native[kMaxChannels] = {native_[0].data(), native_[1].data(), native_[2].data(), native_[3].data()};
  inner_->process(usesInput_ ? nativeIn : nullptr, native, p, nativeBlock_);
  int produced = 0;
  for (int ch = 0; ch < outChannels_; ++ch) {
    for (int j = 0; j < nativeBlock_; ++j) scratch_[size_t(j)] = double(native_[ch][size_t(j)]);
    double* converted = nullptr;
    // Every channel sees identical block lengths, so they produce the same count.
    produced = resampler_[ch]->process(scratch_.data(), nativeBlock_, converted);
    for (int j = 0; j < produced; ++j) fifo_[ch][size_t(j)] = float(converted[j]);
  }
  fifoRead_ = 0;
  fifoSize_ = size_t(produced);
}

void NativeRateWrapper::applyExtraDelay(float* const* out, int n) {
  for (int i = 0; i < n; ++i) {
    for (int ch = 0; ch < outChannels_; ++ch) {
      const float delayed = delay_[ch][delayPos_];
      delay_[ch][delayPos_] = out[ch][i];
      out[ch][i] = delayed;
    }
    delayPos_ = (delayPos_ + 1) % size_t(extra_);
  }
}

}  // namespace eg13
```

- [ ] **Step 6: Count bytes and aligned allocations (`tests/alloc_counter.h`, `tests/alloc_counter.cpp`)**

In `alloc_counter.h`, add to the class after `size_t count() const;`:

```cpp
  size_t bytes() const;  // bytes requested by those allocations
```

Replace `tests/alloc_counter.cpp` with:

```cpp
#include "alloc_counter.h"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace {
thread_local bool tCounting = false;
thread_local size_t tCount = 0;
thread_local size_t tBytes = 0;

void note(std::size_t size) {
  if (tCounting) {
    ++tCount;
    tBytes += size;
  }
}

void* alignedAlloc(std::size_t size, std::align_val_t al) {
  void* p = nullptr;
  const std::size_t alignment = std::max(sizeof(void*), std::size_t(al));
  if (posix_memalign(&p, alignment, size ? size : 1) != 0) throw std::bad_alloc();
  return p;
}
}  // namespace

namespace eg13::test {

ScopedAllocationCounter::ScopedAllocationCounter() {
  tCount = 0;
  tBytes = 0;
  tCounting = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() { tCounting = false; }

size_t ScopedAllocationCounter::count() const { return tCount; }

size_t ScopedAllocationCounter::bytes() const { return tBytes; }

}  // namespace eg13::test

void* operator new(std::size_t size) {
  note(size);
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }

void* operator new(std::size_t size, std::align_val_t al) {
  note(size);
  return alignedAlloc(size, al);
}

void* operator new[](std::size_t size, std::align_val_t al) { return operator new(size, al); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
```

- [ ] **Step 7: Run the tests**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all tests pass (every M2 test unchanged: two-channel engines take the same path as before). If "Side inputs are converted with the input path's latency" is off by more than 4 samples, report the measured peak positions and `inputLatencySamples()` as BLOCKED rather than widening the tolerance.

- [ ] **Step 8: Commit**

```bash
git add core/engine/voice_engine.h core/engine/native_rate_wrapper.h core/engine/native_rate_wrapper.cpp \
  tests/alloc_counter.h tests/alloc_counter.cpp tests/test_engines.h tests/test_native_rate_wrapper.cpp tests/test_alloc_counter.cpp
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "NativeRateWrapper: side inputs, AUX output and engine latency; stereo hook"
```

---
### Task 3: Smoothing and stereo helpers: ParamRamp, StereoLink, StereoDetector, formatters

**Files:**
- Modify: `core/engine/pot_smoother.h`
- Create: `core/engine/stereo_link.h`, `core/engine/param_format.h`
- Test: `tests/test_param_ramp.cpp`, `tests/test_stereo_link.cpp`

**Interfaces:**
- Consumes: `clamp01` (Task 2).
- Produces:
  - `class ParamRamp { void reset(); void begin(float target, int n); float at(int i) const; float value() const; }`: per-sample linear ramp across one `process()` call from where the previous call ended to `target`; `at(n - 1)` is exactly `target`; the first `begin()` after `reset()` holds the target.
  - `constexpr double kStereoFadeSeconds = 0.01; constexpr double kStereoHoldSeconds = 0.05;`
  - `class StereoLink { enum class Start { None, CopyLeft, ResetRight }; void prepare(double rate); void reset(); Start request(bool stereo, bool copyable); bool rightRuns() const; void mix(const float* left, const float* right, float* out, int n); float weight() const; }`. `out` may alias `right`.
  - `class StereoDetector { void prepare(double rate); void reset(); bool update(const float* l, const float* r, int n); bool stereo() const; }`
  - `param_format.h`: `float knobHz(float normalized)` (20 Hz·1000^p), `void formatPercent01(float, char*, size_t)`, `void formatHz(float hz, char*, size_t)`, `void formatSemitones(float st, char*, size_t)`, `void formatDb(float db, char*, size_t)`, `int stepIndex(float normalized, int steps)`.

- [ ] **Step 1: Write the failing tests**

`tests/test_param_ramp.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "engine/param_format.h"
#include "engine/pot_smoother.h"

using namespace eg13;

TEST_CASE("ParamRamp holds its first target after reset") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.8f, 16);
  for (int i = 0; i < 16; ++i) REQUIRE(ramp.at(i) == 0.8f);
  REQUIRE(ramp.value() == 0.8f);
}

TEST_CASE("ParamRamp steps gradually and ends each call exactly on its target") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.2f, 16);
  ramp.begin(0.8f, 16);
  const float step = (0.8f - 0.2f) / 16.0f;
  float previous = 0.2f;
  for (int i = 0; i < 15; ++i) {
    REQUIRE(ramp.at(i) == 0.2f + step * float(i + 1));
    REQUIRE(ramp.at(i) > previous);
    previous = ramp.at(i);
  }
  REQUIRE(ramp.at(15) == 0.8f);
  ramp.begin(0.8f, 7);  // converged: holds
  for (int i = 0; i < 7; ++i) REQUIRE(ramp.at(i) == 0.8f);
}

TEST_CASE("ParamRamp snaps again after reset") {
  ParamRamp ramp;
  ramp.reset();
  ramp.begin(0.1f, 16);
  ramp.reset();
  ramp.begin(0.9f, 16);
  REQUIRE(ramp.at(0) == 0.9f);
}

TEST_CASE("Shared formatters and knob curve") {
  REQUIRE(knobHz(0.0f) == 20.0f);
  REQUIRE(std::fabs(knobHz(1.0f) - 20000.0f) < 0.5f);
  REQUIRE(std::fabs(knobHz(0.5f) - 632.456f) < 0.01f);
  REQUIRE(knobHz(std::nanf("")) == 20.0f);
  char buf[32];
  formatHz(440.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "440 Hz");
  formatHz(1500.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1.50 kHz");
  formatDb(-6.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-6.0 dB");
  formatSemitones(7.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+7.0 st");
  formatPercent01(0.25f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "25%");
  REQUIRE(stepIndex(0.0f, 3) == 0);
  REQUIRE(stepIndex(0.34f, 3) == 1);
  REQUIRE(stepIndex(1.0f, 3) == 2);
  REQUIRE(stepIndex(std::nanf(""), 3) == 0);
}
```

Add `#include <string>` at the top of that file.

`tests/test_stereo_link.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/stereo_link.h"

using namespace eg13;

namespace {
constexpr double kRate = 48000.0;
constexpr int kFade = 480;  // 10 ms at 48 kHz
}  // namespace

TEST_CASE("StereoDetector turns stereo at the first differing sample and mono after the hold") {
  StereoDetector d;
  d.prepare(kRate);
  d.reset();
  std::vector<float> l(16, 0.5f), r(16, 0.5f);
  REQUIRE_FALSE(d.update(l.data(), r.data(), 16));
  r[9] = 0.4f;
  REQUIRE(d.update(l.data(), r.data(), 16));
  r[9] = 0.5f;
  int samples = 0;
  while (d.update(l.data(), r.data(), 16)) samples += 16;
  REQUIRE(samples + 16 >= int(kRate * kStereoHoldSeconds));
  REQUIRE(samples <= int(kRate * kStereoHoldSeconds));
  d.reset();
  REQUIRE_FALSE(d.stereo());
}

TEST_CASE("StereoLink while mono: the right output is the left output") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(false, true) == StereoLink::Start::None);
  REQUIRE_FALSE(link.rightRuns());
  std::vector<float> left = {0.1f, 0.2f, 0.3f}, right = {9.f, 9.f, 9.f}, out(3);
  link.mix(left.data(), right.data(), out.data(), 3);
  REQUIRE(out == left);
}

TEST_CASE("StereoLink start with a copyable engine is immediate") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(true, true) == StereoLink::Start::CopyLeft);
  REQUIRE(link.rightRuns());
  std::vector<float> left(16, 0.1f), right(16, 0.7f), out(16);
  link.mix(left.data(), right.data(), out.data(), 16);
  for (float s : out) REQUIRE(s == 0.7f);
  REQUIRE(link.request(true, true) == StereoLink::Start::None);
}

TEST_CASE("StereoLink start and stop are continuous") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  REQUIRE(link.request(true, false) == StereoLink::Start::ResetRight);
  std::vector<float> left(16, 0.0f), right(16, 1.0f), out(16);
  float previous = 0.0f;
  int samples = 0;
  for (int block = 0; block < 40; ++block) {  // fade in
    link.request(true, false);
    link.mix(left.data(), right.data(), out.data(), 16);
    for (float s : out) {
      REQUIRE(s >= previous);
      REQUIRE(s - previous <= 1.0f / float(kFade) + 1e-6f);
      previous = s;
      ++samples;
    }
  }
  REQUIRE(previous == 1.0f);
  for (int block = 0; block < 40; ++block) {  // fade out
    link.request(false, false);
    link.mix(left.data(), right.data(), out.data(), 16);
    for (float s : out) {
      REQUIRE(s <= previous);
      REQUIRE(previous - s <= 1.0f / float(kFade) + 1e-6f);
      previous = s;
    }
  }
  REQUIRE(previous == 0.0f);
  REQUIRE_FALSE(link.rightRuns());
}

TEST_CASE("StereoLink restarting during a fade-out continues from the current weight") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  link.request(true, true);
  std::vector<float> left(16, 0.0f), right(16, 1.0f), out(16);
  link.mix(left.data(), right.data(), out.data(), 16);
  link.request(false, true);
  link.mix(left.data(), right.data(), out.data(), 16);
  const float w = link.weight();
  REQUIRE(w < 1.0f);
  REQUIRE(link.request(true, true) == StereoLink::Start::None);  // still running: no copy
  link.mix(left.data(), right.data(), out.data(), 16);
  REQUIRE(std::fabs(out[0] - (w + 1.0f / float(kFade))) < 1e-5f);
}

TEST_CASE("StereoLink can mix in place over the right buffer") {
  StereoLink link;
  link.prepare(kRate);
  link.reset();
  link.request(true, true);
  std::vector<float> left(8, 0.25f), right(8, 0.5f);
  link.mix(left.data(), right.data(), right.data(), 8);
  for (float s : right) REQUIRE(s == 0.5f);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`ParamRamp`, `engine/stereo_link.h`, `engine/param_format.h` missing).

- [ ] **Step 3: Add `ParamRamp` to `core/engine/pot_smoother.h`**

Append before the closing `}  // namespace eg13`:

```cpp
// Per-sample linear ramp across one process() call, from the value the previous call ended on
// to this call's target: stmlib's ParameterInterpolator (stmlib/dsp/parameter_interpolator.h:
// 38-51), which Plaits, Rings and Warps run on their DSP parameters within each block. Used by
// engines with no firmware pot filter (the VCV Ripples and Shelves models read their knobs every
// sample, Ripples.cpp:74-76, Shelves.cpp:125-136; the SVF follows Plaits' noise engine,
// plaits/dsp/engine/noise_engine.cc:76-78). The first call after reset() holds its target.
class ParamRamp {
 public:
  void reset() { snap_ = true; }

  void begin(float target, int n) {
    start_ = snap_ ? target : end_;
    end_ = target;
    n_ = n;
    step_ = n > 0 ? (end_ - start_) / float(n) : 0.0f;
    snap_ = false;
  }

  // Sample i (0..n-1) of the current call; the last one is exactly the target.
  float at(int i) const { return i + 1 >= n_ ? end_ : start_ + step_ * float(i + 1); }

  float value() const { return end_; }  // where the current call ends

 private:
  float start_ = 0.0f;
  float end_ = 0.0f;
  float step_ = 0.0f;
  int n_ = 0;
  bool snap_ = true;
};
```

Note: with a held target `start_ == end_`, so `at(i)` returns `end_ + 0 * k == end_` exactly.

- [ ] **Step 4: Write `core/engine/stereo_link.h`**

```cpp
#pragma once

#include <algorithm>

namespace eg13 {

// Spec §5.1: a Filter or Shaper runs a second (right-channel) instance only while its input is
// stereo.
constexpr double kStereoFadeSeconds = 0.01;  // right output fades between instances
constexpr double kStereoHoldSeconds = 0.05;  // L == R this long before mono again

// Decides, per engine, when its right instance runs and what the right output is.
// Starting: a copyable engine gives the right instance the left one's state (CopyLeft) and the
// right output switches to it at once, seamlessly; any other engine resets its right instance
// (ResetRight) and the right output fades from the left output to it. Stopping: the right
// output fades back to the left output, then the right instance stops. A restart during a fade
// continues from the current weight with the instance still running.
class StereoLink {
 public:
  enum class Start { None, CopyLeft, ResetRight };

  void prepare(double rate) { step_ = float(1.0 / std::max(1.0, rate * kStereoFadeSeconds)); }

  void reset() {
    running_ = false;
    weight_ = 0.0f;
    target_ = 0.0f;
  }

  // Once per block, before processing. The caller copies or resets its right instance as told.
  Start request(bool stereo, bool copyable) {
    target_ = stereo ? 1.0f : 0.0f;
    if (!stereo || running_) return Start::None;
    running_ = true;
    if (copyable) {
      weight_ = 1.0f;
      return Start::CopyLeft;
    }
    weight_ = 0.0f;
    return Start::ResetRight;
  }

  bool rightRuns() const { return running_; }
  float weight() const { return weight_; }

  // out = left + weight * (right - left), per sample. right is not read while not running.
  // out may alias right.
  void mix(const float* left, const float* right, float* out, int n) {
    if (!running_) {
      std::copy(left, left + n, out);
      return;
    }
    for (int i = 0; i < n; ++i) {
      weight_ += std::clamp(target_ - weight_, -step_, step_);
      out[i] = left[i] + weight_ * (right[i] - left[i]);
    }
    if (target_ == 0.0f && weight_ <= 0.0f) {
      weight_ = 0.0f;
      running_ = false;
    }
  }

 private:
  bool running_ = false;
  float weight_ = 0.0f;
  float target_ = 0.0f;
  float step_ = 1.0f;
};

// Whether a slot's input is stereo: from the first block with any L != R sample; mono again
// once L == R exactly (silence included) has held for kStereoHoldSeconds.
class StereoDetector {
 public:
  void prepare(double rate) { hold_ = int(rate * kStereoHoldSeconds); }

  void reset() {
    stereo_ = false;
    same_ = 0;
  }

  bool update(const float* l, const float* r, int n) {
    bool differs = false;
    for (int i = 0; i < n; ++i) differs = differs || l[i] != r[i];
    if (differs) {
      stereo_ = true;
      same_ = 0;
    } else if (stereo_) {
      same_ += n;
      if (same_ >= hold_) stereo_ = false;
    }
    return stereo_;
  }

  bool stereo() const { return stereo_; }

 private:
  bool stereo_ = false;
  int same_ = 0;
  int hold_ = 2400;
};

}  // namespace eg13
```

- [ ] **Step 5: Write `core/engine/param_format.h`**

```cpp
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>

#include "engine/voice_engine.h"

namespace eg13 {

// 20 Hz .. 20 kHz, exponential: the VCV Ripples and Shelves frequency knobs (ripples.hpp:29-32,
// Ripples.cpp:36,75; shelves.hpp:28-29, Shelves.cpp:5-7,125-128). The Classic Filters and SVF
// cutoffs use the same curve so a modulation amount means the same sweep in every filter.
inline float knobHz(float normalized) { return 20.0f * std::pow(1000.0f, clamp01(normalized)); }

// Index of a stepped parameter with `steps` equal ranges over 0..1 (NaN -> 0).
inline int stepIndex(float normalized, int steps) {
  const int i = int(clamp01(normalized) * float(steps));
  return i < steps ? i : steps - 1;
}

inline void formatPercent01(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d%%", int(std::lround(clamp01(v) * 100.0f)));
}

inline void formatHz(float hz, char* buf, size_t size) {
  if (hz < 999.5f) {
    std::snprintf(buf, size, "%d Hz", int(std::lround(hz)));
  } else {
    std::snprintf(buf, size, "%.2f kHz", double(hz) / 1000.0);
  }
}

inline void formatSemitones(float st, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(st));
}

inline void formatDb(float db, char* buf, size_t size) { std::snprintf(buf, size, "%+.1f dB", double(db)); }

}  // namespace eg13
```

`formatDb(-6.0f)` prints `-6.0 dB` (the `+` flag only adds a sign to non-negative values).

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add core/engine/pot_smoother.h core/engine/stereo_link.h core/engine/param_format.h tests/test_param_ramp.cpp tests/test_stereo_link.cpp
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "ParamRamp, StereoLink and StereoDetector helpers; shared value formatters"
```

---
### Task 4: Source AUX outputs (Macro Oscillator AUX, Resonator even output)

**Files:**
- Modify: `core/engine/plaits_engine.h`, `core/engine/plaits_engine.cpp`, `core/engine/rings_engine.h`, `core/engine/rings_engine.cpp`
- Test: `tests/test_plaits_engine.cpp`, `tests/test_rings_engine.cpp`

**Interfaces:**
- Consumes: `EngineInfo::hasAux`, `VoiceEngine::setAuxEnabled` (Task 2).
- Produces: `PlaitsEngine` and `RingsEngine` report `hasAux = true`; after `setAuxEnabled(true)` their `process()` writes `out[2]`: Plaits' AUX frame (`f.aux / 32768`), Rings' even (AUX jack) output times velocity. Two-channel callers (aux off, the default) are unchanged.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_plaits_engine.cpp`:

```cpp
TEST_CASE("Macro Oscillator AUX is upstream Plaits' AUX output") {
  PlaitsEngine engine;
  REQUIRE(PlaitsEngine::staticInfo().hasAux);
  engine.setAuxEnabled(true);
  engine.prepare(PlaitsEngine::kNativeRate, 512);
  engine.noteOn(69.0f, 0.8f);
  const int blocks = 200;
  std::vector<float> l(size_t(blocks * 12)), r(l.size()), aux(l.size());
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  for (int b = 0; b < blocks; ++b) {
    float* out[3] = {l.data() + b * 12, r.data() + b * 12, aux.data() + b * 12};
    engine.process(nullptr, out, p, 12);
  }

  static char ram[16384];
  stmlib::BufferAllocator allocator(ram, sizeof(ram));
  auto reference = std::make_unique<plaits::Voice>();
  reference->Init(&allocator);
  plaits::Patch patch = {};
  patch.note = 69.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.engine = 8;
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;
  plaits::Modulations mods = {};
  mods.trigger_patched = true;
  mods.level_patched = true;
  mods.level = 0.8f;
  plaits::Voice::Frame frames[plaits::kBlockSize];
  for (int b = 0; b < blocks; ++b) {
    mods.trigger = b == 0 ? 0.0f : 1.0f;
    reference->Render(patch, mods, frames, plaits::kBlockSize);
    for (size_t j = 0; j < plaits::kBlockSize; ++j) {
      REQUIRE(aux[size_t(b) * 12 + j] == float(frames[j].aux) / 32768.0f);
      REQUIRE(l[size_t(b) * 12 + j] == float(frames[j].out) / 32768.0f);  // Aux Mix 0: OUT only
    }
  }
}

TEST_CASE("Macro Oscillator AUX does not change the main outputs") {
  PlaitsEngine a, b;
  b.setAuxEnabled(true);
  for (auto* e : {&a, &b}) {
    e->prepare(PlaitsEngine::kNativeRate, 512);
    e->noteOn(60.0f, 1.0f);
  }
  const auto p = defaultParams(PlaitsEngine::staticInfo());
  std::vector<float> la(480), ra(480), lb(480), rb(480), xb(480);
  float* outA[2] = {la.data(), ra.data()};
  float* outB[3] = {lb.data(), rb.data(), xb.data()};
  a.process(nullptr, outA, p, 480);
  b.process(nullptr, outB, p, 480);
  REQUIRE(la == lb);
  REQUIRE(ra == rb);
}
```

Append to `tests/test_rings_engine.cpp`:

```cpp
TEST_CASE("Resonator AUX is upstream Rings' even output") {
  REQUIRE(RingsEngine::staticInfo().hasAux);
  constexpr int kBlocks = 200;
  constexpr int kN = RingsEngine::kNativeBlock;
  std::vector<float> odd, even;
  renderUpstream(kBlocks, odd, even);
  ParamValues p = defaults();
  p.p[0] = 3.5f / 6.0f;  // FM Voice, as renderUpstream
  RingsEngine engine;
  engine.setAuxEnabled(true);
  engine.prepare(RingsEngine::kNativeRate, 256);
  engine.noteOn(60.0f, 1.0f);
  std::vector<float> l(size_t(kBlocks * kN)), r(l.size()), aux(l.size());
  for (int b = 0; b < kBlocks; ++b) {
    float* out[3] = {l.data() + b * kN, r.data() + b * kN, aux.data() + b * kN};
    engine.process(nullptr, out, p, kN);
  }
  REQUIRE(test::rms(even, 0, even.size()) > 0.001);
  for (size_t i = 0; i < aux.size(); ++i) REQUIRE(aux[i] == even[i]);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "AUX"`
Expected: FAIL (`hasAux` is false; AUX buffers stay zero).

- [ ] **Step 3: Macro Oscillator AUX**

In `core/engine/plaits_engine.h`, add to the public section after `setNote`:

```cpp
  void setAuxEnabled(bool on) override { aux_ = on; }
```

and to the private members after `bool retrigger_ = false;`:

```cpp
  bool aux_ = false;  // write out[2]: Plaits' AUX output, for the Ring Mod modulator
```

In `core/engine/plaits_engine.cpp`, change the info line to:

```cpp
const EngineInfo kInfo = {"plaits", "Macro Oscillator", 8, kParams, false, true, 0, nullptr};
```

and in `PlaitsEngine::process`, after `out[1][i] = s;`, add:

```cpp
    if (aux_) out[2][i] = float(f.aux) / 32768.0f;  // the AUX jack (plaits/dsp/voice.h Frame)
```

- [ ] **Step 4: Resonator AUX**

In `core/engine/rings_engine.h`, add to the public section after `setNote`:

```cpp
  void setAuxEnabled(bool on) override { aux_ = on; }
```

and to the private members after `int polyphony_ = 1;`:

```cpp
  bool aux_ = false;  // write out[2]: Rings' even (AUX jack) output, for the Ring Mod modulator
```

In `core/engine/rings_engine.cpp`, change the info line to:

```cpp
const EngineInfo kInfo = {"rings", "Resonator", 9, kParams, true, true, 0, nullptr};
```

and in `RingsEngine::renderBlock`, inside the final loop after the `out[1][offset + i] = ...;` line, add:

```cpp
    if (aux_) out[2][offset + i] = s.even[i] * velocity_;  // the AUX jack: even partials (part.cc:530-535)
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/engine/plaits_engine.h core/engine/plaits_engine.cpp core/engine/rings_engine.h core/engine/rings_engine.cpp \
  tests/test_plaits_engine.cpp tests/test_rings_engine.cpp
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Sources: Macro Oscillator AUX and Resonator even output on a third channel"
```

---
### Task 5: Analog Filter (VCV Ripples model)

**Files:**
- Manual: `docs/manual/engines.md`, `docs/manual/smoothing.md` (same commit)
- Create: `core/engine/ripples_engine.h`, `core/engine/ripples_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/ripples_engine.cpp`)
- Test: `tests/test_ripples_engine.cpp`

**Interfaces:**
- Consumes: `ParamRamp` (Task 3), `StereoLink` (Task 3), `knobHz`, `stepIndex`, `formatHz`, `formatPercent01` (Task 3), `eg13_vcv` (Task 1).
- Produces: `class RipplesEngine final : public VoiceEngine` ("Analog Filter", id `"ripples"`, host rate, `usesInput`): `static constexpr float kVolts = 5.0f; static const EngineInfo& staticInfo(); static int modeIndex(float normalized);` (0 LP4, 1 LP2, 2 BP2), `float smoothedParam(int index) const;` (P1/P2 at the end of the last block), `bool rightRuns() const;`. P1 Frequency (default 1.0), P2 Resonance (0), P3 Mode (LP4).

- [ ] **Step 1: Write the failing tests (`tests/test_ripples_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Ripples/ripples.hpp"
#include "alloc_counter.h"
#include "engine/ripples_engine.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(RipplesEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

std::vector<float> saw(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) {
    const double phase = std::fmod(hz * double(i) / rate, 1.0);
    x[i] = amp * float(2.0 * phase - 1.0);
  }
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(RipplesEngine& e, const std::vector<float>& inL, const std::vector<float>& inR,
              const ParamValues& p, int chunk = 16) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size();) {
    const int count = int(std::min<size_t>(size_t(chunk), inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
    done += size_t(count);
  }
  return s;
}

// The model's output as the adapter delivers it: polarity restored, volts to EG13 units.
float delivered(const ripples::RipplesEngine::Frame& f, int mode) {
  const float v = mode == 0 ? f.lp4 : mode == 1 ? f.lp2 : f.bp2;
  return -v / RipplesEngine::kVolts;
}

// The adapter is built with Rack's own -funsafe-math-optimizations (Rack compile.mk:17), this
// test without it, so they agree to 1e-5 rather than bit for bit (owner, 2026-10-20). Planning
// measured at most 3.9e-6 over a second at Resonance 0.7.
bool nearModel(float actual, float expected) {
  return std::fabs(actual - expected) <= 1e-5f * std::max(1.0f, std::fabs(expected));
}

}  // namespace

TEST_CASE("Analog Filter describes itself") {
  const auto& info = RipplesEngine::staticInfo();
  REQUIRE(std::string(info.id) == "ripples");
  REQUIRE(std::string(info.displayName) == "Analog Filter");
  REQUIRE(info.paramCount == 3);
  REQUIRE(info.usesInput);
  REQUIRE(info.sideInputs == 0);
  const char* const names[3] = {"Frequency", "Resonance", "Mode"};
  for (int i = 0; i < 3; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);  // Ripples.cpp:36: fully open
  REQUIRE(info.params[1].defaultValue == 0.0f);  // Ripples.cpp:35
  char buf[32];
  info.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20.00 kHz");
  info.params[0].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "20 Hz");
  const char* const modes[3] = {"LP4", "LP2", "BP2"};
  for (int m = 0; m < 3; ++m) {
    info.params[2].format((float(m) + 0.5f) / 3.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
    REQUIRE(RipplesEngine::modeIndex((float(m) + 0.5f) / 3.0f) == m);
  }
  REQUIRE(RipplesEngine::modeIndex(info.params[2].defaultValue) == 0);
  REQUIRE(RipplesEngine::modeIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
}

TEST_CASE("Analog Filter matches the VCV Ripples model to 1e-5") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    for (int mode = 0; mode < 3; ++mode) {
      RipplesEngine engine;
      engine.prepare(rate, 256);
      ParamValues p = defaults();
      p.p[0] = 0.55f;
      p.p[1] = 0.7f;
      p.p[2] = (float(mode) + 0.5f) / 3.0f;
      const auto in = saw(4800, 110.0, rate);
      rack::random::seed(11);
      const Stereo out = render(engine, in, in, p);
      rack::random::seed(11);
      ripples::RipplesEngine ref;
      ref.setSampleRate(float(rate));
      ripples::RipplesEngine::Frame f = {};
      f.freq_knob = 0.55f;
      f.res_knob = 0.7f;
      INFO("rate " << rate << " mode " << mode);
      for (size_t i = 0; i < in.size(); ++i) {
        f.input = in[i] * RipplesEngine::kVolts;
        ref.process(f);
        REQUIRE(nearModel(out.l[i], delivered(f, mode)));
        REQUIRE(out.r[i] == out.l[i]);
      }
    }
  }
}

TEST_CASE("Analog Filter ramps Frequency and Resonance across each block") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = saw(64, 220.0, 48000.0);
  ParamValues a = defaults();
  a.p[0] = 0.2f;
  a.p[1] = 0.1f;
  ParamValues b = a;
  b.p[0] = 0.8f;
  b.p[1] = 0.6f;
  rack::random::seed(3);
  std::vector<float> out(64), right(64);
  for (int block = 0; block < 4; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  REQUIRE(engine.smoothedParam(0) == 0.8f);  // converged by the end of the step's block
  rack::random::seed(3);
  ripples::RipplesEngine ref;
  ref.setSampleRate(48000.f);
  ripples::RipplesEngine::Frame f = {};
  const float df = (0.8f - 0.2f) / 16.0f;
  const float dr = (0.6f - 0.1f) / 16.0f;
  for (int i = 0; i < 64; ++i) {
    if (i < 16) {  // first block after reset: snapped
      f.freq_knob = 0.2f;
      f.res_knob = 0.1f;
    } else if (i < 32) {  // the step, gradually
      const int k = i - 16;
      f.freq_knob = k == 15 ? 0.8f : 0.2f + df * float(k + 1);
      f.res_knob = k == 15 ? 0.6f : 0.1f + dr * float(k + 1);
    } else {
      f.freq_knob = 0.8f;
      f.res_knob = 0.6f;
    }
    f.input = in[size_t(i)] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], delivered(f, 0)));
  }
}

TEST_CASE("Analog Filter snaps to its parameters after reset") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues a = defaults();
  a.p[0] = 0.9f;
  const auto in = saw(32, 220.0, 48000.0);
  render(engine, in, in, a);
  engine.reset();
  ParamValues b = defaults();
  b.p[0] = 0.3f;
  rack::random::seed(5);
  const Stereo out = render(engine, in, in, b);
  rack::random::seed(5);
  ripples::RipplesEngine ref;
  ref.setSampleRate(48000.f);
  ripples::RipplesEngine::Frame f = {};
  f.freq_knob = 0.3f;
  for (size_t i = 0; i < in.size(); ++i) {
    f.input = in[i] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out.l[i], delivered(f, 0)));
  }
}

TEST_CASE("Analog Filter Mode switches at once (stepped, not smoothed)") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues lp4 = defaults();
  lp4.p[0] = 0.5f;
  ParamValues bp2 = lp4;
  bp2.p[2] = 2.5f / 3.0f;
  const auto in = saw(32, 220.0, 48000.0);
  rack::random::seed(9);
  std::vector<float> out(32), right(32);
  const float* a[2] = {in.data(), in.data()};
  float* o[2] = {out.data(), right.data()};
  engine.process(a, o, lp4, 16);
  const float* b[2] = {in.data() + 16, in.data() + 16};
  float* ob[2] = {out.data() + 16, right.data() + 16};
  engine.process(b, ob, bp2, 16);
  rack::random::seed(9);
  ripples::RipplesEngine ref;
  ref.setSampleRate(48000.f);
  ripples::RipplesEngine::Frame f = {};
  f.freq_knob = 0.5f;
  for (int i = 0; i < 32; ++i) {
    f.input = in[size_t(i)] * RipplesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], delivered(f, i < 16 ? 0 : 2)));
  }
}

TEST_CASE("Analog Filter open keeps the input's level and polarity") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(48000, 100.0, 48000.0);
  const Stereo out = render(engine, in, in, defaults());
  double dot = 0.0;
  for (size_t i = 4800; i < in.size(); ++i) dot += double(in[i]) * double(out.l[i]);
  REQUIRE(dot > 0.0);
  const double gainDb = 20.0 * std::log10(test::rms(out.l, 4800, 48000) / test::rms(in, 4800, 48000));
  INFO("open LP4 gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 1.0);
}

TEST_CASE("Analog Filter Frequency sets the cutoff") {
  ParamValues p = defaults();
  p.p[0] = 0.3f;  // ~160 Hz
  auto level = [&](double hz) {
    RipplesEngine engine;
    engine.prepare(48000.0, 256);
    const auto in = sine(24000, hz, 48000.0);
    const Stereo out = render(engine, in, in, p);
    return test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000);
  };
  REQUIRE(level(40.0) > 0.7);
  REQUIRE(level(5000.0) < 0.02);  // measured during planning: 0.006 (-44 dB)
}

TEST_CASE("Analog Filter self-oscillates at full Resonance and stays bounded") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 1.0f;
  std::vector<float> kick(48000, 0.0f);
  kick[0] = 0.5f;  // a start, so the test does not wait on the model's 1e-6 V bootstrap noise
  const Stereo out = render(engine, kick, kick, p);
  for (float s : out.l) {
    REQUIRE(std::isfinite(s));
    REQUIRE(std::fabs(s) < 7.0f);  // opamps clamp at 10.6 V (ripples.hpp:112) times the LP4 gain
  }
  REQUIRE(test::rms(out.l, 24000, 48000) > 0.01);
}

TEST_CASE("Analog Filter runs its right instance only while stereo") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.6f;
  const auto l = saw(960, 220.0, 48000.0);
  const auto r = saw(960, 330.0, 48000.0);
  engine.setStereo(false);
  const Stereo mono = render(engine, l, r, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo stereo = render(engine, l, r, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(stereo.l != stereo.r);
}

TEST_CASE("Analog Filter: stereo starts without a jump and stops with a fade") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 0.6f;
  const auto x = saw(4800, 110.0, 48000.0);
  render(engine, x, x, p);  // mono history
  engine.setStereo(true);   // same input on both sides: the right copy continues the left
  const Stereo start = render(engine, x, x, p);
  for (size_t i = 0; i < 64; ++i) REQUIRE(std::fabs(start.r[i] - start.l[i]) < 1e-4f);
  const auto y = saw(4800, 165.0, 48000.0);
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(test::rms(apart.r, 2400, 4800) > 0.01);
  engine.setStereo(false);
  const Stereo stop = render(engine, x, y, p);
  float previousGap = std::fabs(stop.r[0] - stop.l[0]);
  for (size_t i = 0; i < 480; ++i) {
    const float gap = std::fabs(stop.r[i] - stop.l[i]);
    REQUIRE(std::fabs(gap - previousGap) < 0.1f);  // no step in R (a cut would jump by the whole gap)
    previousGap = gap;
  }
  for (size_t i = 496; i < stop.l.size(); ++i) REQUIRE(stop.r[i] == stop.l[i]);
  REQUIRE_FALSE(engine.rightRuns());
}

TEST_CASE("Analog Filter survives NaN parameters") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = saw(4800, 110.0, 48000.0);
  const Stereo out = render(engine, x, x, bad);
  for (float s : out.l) REQUIRE(std::isfinite(s));
}

TEST_CASE("Analog Filter processing does not allocate") {
  RipplesEngine engine;
  engine.prepare(48000.0, 256);
  std::vector<float> l(16, 0.1f), r(16, -0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p = defaults();
  engine.process(in, out, p, 16);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 300; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[0] = float(block % 10) / 10.0f;
      p.p[2] = float(block % 3) / 3.0f;
      engine.process(in, out, p, 16);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/ripples_engine.h` not found).

- [ ] **Step 3: Write `core/engine/ripples_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the VCV Ripples model (AudibleInstruments src/Ripples, GPL-3.0-or-later), shown as
// "Analog Filter". Runs at the host rate (the model oversamples internally, aafilter.hpp).
class RipplesEngine final : public VoiceEngine {
 public:
  static constexpr float kVolts = 5.0f;  // EG13 ±1.0 = ±5 V, VCV's audio level

  RipplesEngine();
  ~RipplesEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override { stereo_ = stereo; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int modeIndex(float normalized);  // P3 thirds: 0 LP4, 1 LP2, 2 BP2
  float smoothedParam(int index) const;    // test hook: P1/P2 where the last block ended
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp freq_;
  ParamRamp res_;
  StereoLink link_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/ripples_engine.cpp`**

```cpp
#include "engine/ripples_engine.h"

#include <cstdio>
#include <vector>

#include "Ripples/ripples.hpp"
#include "engine/param_format.h"

namespace eg13 {

namespace {

const char* const kModeNames[3] = {"LP4", "LP2", "BP2"};

void formatFrequency(float v, char* buf, size_t size) { formatHz(knobHz(v), buf, size); }
void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[RipplesEngine::modeIndex(v)]);
}

// Frequency: the VCV knob, 20 Hz .. 20 kHz exponential (Ripples.cpp:36,75; ripples.hpp:29-32),
// default fully open (Ripples.cpp:36). Resonance: 0..1, default 0 (Ripples.cpp:35). Mode picks
// one of the module's outputs, in the spec's order (LP4 / LP2 / BP2).
const EngineParamInfo kParams[3] = {
    {"Frequency", 1.0f, formatFrequency},
    {"Resonance", 0.0f, formatPercent01},
    {"Mode", 0.5f / 3.0f, formatMode},
};

const EngineInfo kInfo = {"ripples", "Analog Filter", 3, kParams, true, false, 0, nullptr};

// Ripples' output amplifiers invert (ripples.hpp:95-97: kLP2Gain, kLP4Gain, kBP2Gain < 0). EG13
// restores the input's polarity so Dry and processed signals add rather than cancel.
float delivered(const ripples::RipplesEngine::Frame& f, int mode) {
  const float v = mode == 0 ? f.lp4 : mode == 1 ? f.lp2 : f.bp2;
  return -v / RipplesEngine::kVolts;
}

}  // namespace

struct RipplesEngine::Impl {
  ripples::RipplesEngine left;
  ripples::RipplesEngine right;
  std::vector<float> rightOut;
};

RipplesEngine::RipplesEngine() : impl_(std::make_unique<Impl>()) {}

RipplesEngine::~RipplesEngine() = default;

const EngineInfo& RipplesEngine::staticInfo() { return kInfo; }

const EngineInfo& RipplesEngine::info() const { return kInfo; }

int RipplesEngine::modeIndex(float normalized) { return stepIndex(normalized, 3); }

float RipplesEngine::smoothedParam(int index) const {
  return index == 0 ? freq_.value() : index == 1 ? res_.value() : 0.0f;
}

void RipplesEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  impl_->rightOut.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void RipplesEngine::reset() {
  // A fresh model clears every state, including the CV RC filters that setSampleRate() keeps
  // (ripples.hpp:145-165). VCV's onReset only calls setSampleRate (Ripples.cpp:58-67).
  impl_->left = ripples::RipplesEngine();
  impl_->left.setSampleRate(float(rate_));
  impl_->right = impl_->left;
  freq_.reset();
  res_.reset();
  link_.reset();
}

void RipplesEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                            int n) {
  Impl& s = *impl_;
  // The VCV module reads its knobs every sample (Ripples.cpp:74-76) and the model low-passes
  // them like the hardware's CV amplifiers (ripples.hpp:155-161). EG13's parameters change at
  // block boundaries, so they ramp across each block instead of stepping.
  freq_.begin(clamp01(p.p[0]), n);
  res_.begin(clamp01(p.p[1]), n);
  const int mode = modeIndex(p.p[2]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) s.right = s.left;
  const bool right = link_.rightRuns();
  ripples::RipplesEngine::Frame frame = {};  // CV inputs unpatched, FM knob 0
  for (int i = 0; i < n; ++i) {
    frame.freq_knob = freq_.at(i);
    frame.res_knob = res_.at(i);
    frame.input = (in != nullptr ? in[0][i] : 0.0f) * kVolts;
    s.left.process(frame);
    out[0][i] = delivered(frame, mode);
    if (right) {
      frame.input = (in != nullptr ? in[1][i] : 0.0f) * kVolts;
      s.right.process(frame);
      s.rightOut[size_t(i)] = delivered(frame, mode);
    }
  }
  link_.mix(out[0], s.rightOut.data(), out[1], n);
}

}  // namespace eg13
```

Add `engine/ripples_engine.cpp` to the `add_library(eg13_core ...)` list in `core/CMakeLists.txt`, and after the `target_compile_options` line add:

```cmake
# The VCV models are built as Rack builds its plugins (VCVRack/Rack compile.mk:17:
# "FLAGS += -O3 -funsafe-math-optimizations -fno-omit-frame-pointer"; Release already has -O3):
# about 30 % faster, measured during planning. Owner decision 2026-10-20.
set_source_files_properties(engine/ripples_engine.cpp PROPERTIES COMPILE_OPTIONS "-funsafe-math-optimizations")
```

- [ ] **Step 4a: Document the Analog Filter in the manual**

Append to `docs/manual/engines.md`:

````markdown
## Filters

Every voice has a Filter slot after its Source. **Off** passes the sound through unchanged. All filters' first control is the cutoff, on the same scale (20 Hz to 20 kHz), so one modulation amount sweeps every filter alike. Every filter starts fully open, so choosing one does not change the level.

### Analog Filter (the VCV model of Ripples)

Controls: **Frequency** (20 Hz to 20 kHz, default fully open), **Resonance** (default 0) and **Mode**: *LP4* (24 dB/octave low-pass), *LP2* (12 dB low-pass) or *BP2* (12 dB band-pass).

- A circuit model of the analog filter, including its gentle saturation; at full Resonance it self-oscillates.
- The module's output amplifiers invert the signal; EG13 turns it back, so the filtered sound adds to the Dry signal instead of cancelling it.
- Ripples' FM knob and its CV and Gain inputs are not controls here: modulation comes from EG13's modulation matrix (M4), and the LP4 VCA output is LP4 with a fixed level.
- Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state (no jump).
````

Append to `docs/manual/smoothing.md`:

````markdown
- **Analog Filter:** the VCV module reads its knobs every sample; EG13 moves Frequency and Resonance in a straight line across each 16-sample control block, so a step glides over a third of a millisecond. Mode switches at once.
````


- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Analog Filter"`
Expected: all pass. Then `ctest --preset mac` (all) and the Docker command: all pass. If the 1e-5 comparison fails on any platform, report the first differing index and both values as BLOCKED (do not loosen it).

- [ ] **Step 6: Commit**

```bash
git add core/engine/ripples_engine.h core/engine/ripples_engine.cpp core/CMakeLists.txt tests/test_ripples_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Analog Filter: VCV Ripples model, ramped knobs, stereo when needed"
```

---
### Task 6: Classic Filters (sst-filters)

**Files:**
- Manual: `docs/manual/engines.md`, `docs/manual/smoothing.md` (same commit)
- Create: `core/engine/sst_filter_engine.h`, `core/engine/sst_filter_engine.cpp`, `tests/data/classic_filter_types.txt`
- Modify: `core/CMakeLists.txt` (add `engine/sst_filter_engine.cpp`)
- Test: `tests/test_sst_filter_engine.cpp`

**Interfaces:**
- Consumes: `knobHz`, `formatHz`, `formatPercent01` (Task 3), `eg13_sst` (Task 1), `EG13_TEST_DATA_DIR` (Task 1).
- Produces: `class SstFilterEngine final : public VoiceEngine` ("Classic Filters", id `"sst"`, host rate, `usesInput`): `static constexpr int kTypeSteps = 256; static const EngineInfo& staticInfo(); static int typeCount(); static int typeIndex(float normalized); static float typeValue(int index); static const char* typeName(int index); static bool typeHasCharacter(int index); static float cutoffNote(float normalized);` (semitones from A440, sst's unit). P1 Cutoff (default 1.0), P2 Resonance (0), P3 Type (type 0: Vember Classic Lowpass 12 dB Standard), P4 Character (0.5).

- [ ] **Step 1: Freeze the type order (`tests/data/classic_filter_types.txt`)**

Create the file with exactly these 205 lines (upstream display names at the pinned commit, typos included; generated during planning by enumerating `Filter::availableModels()` and each model's `availableModelConfigurations(model, true)`):

```text
Vember Classic Lowpass 12 dB Standard
Vember Classic Lowpass 12 dB Clean
Vember Classic Lowpass 12 dB Driven
Vember Classic Lowpass 24 dB Standard
Vember Classic Lowpass 24 dB Clean
Vember Classic Lowpass 24 dB Driven
Vember Classic Highpass 12 dB Standard
Vember Classic Highpass 12 dB Clean
Vember Classic Highpass 12 dB Driven
Vember Classic Highpass 24 dB Standard
Vember Classic Highpass 24 dB Clean
Vember Classic Highpass 24 dB Driven
Vember Classic Bandpass 12 dB Standard
Vember Classic Bandpass 12 dB Clean
Vember Classic Bandpass 12 dB Driven
Vember Classic Bandpass 24 dB Standard
Vember Classic Bandpass 24 dB Clean
Vember Classic Bandpass 24 dB Driven
Vember Classic Notch 12 dB Standard
Vember Classic Notch 12 dB Mild
Vember Classic Notch 24 dB Standard
Vember Classic Notch 24 dB Mild
Vember Classic Allpass
Vember Ladder Lowpass 6 dB
Vember Ladder Lowpass 12 dB
Vember Ladder Lowpass 18 dB
Vember Ladder Lowpass 24 dB
OB-Xd 2-pole Lowpass Standard
OB-Xd 2-pole Lowpass Pushed
OB-Xd 2-pole Highpass Standard
OB-Xd 2-pole Highpass Pushed
OB-Xd 2-pole Bandpass Standard
OB-Xd 2-pole Bandpass Pushed
OB-Xd 2-pole Notch Standard
OB-Xd 2-pole Notch Pushed
OB-Xd 4-pole Lowpass 6 dB
OB-Xd 4-pole Lowpass 12 dB
OB-Xd 4-pole Lowpass 18 dB
OB-Xd 4-pole Lowpass 24 dB
OB-Xd 4-pole Lowpass 24 dB 24 dB (Legacy)
OB-Xd 4-pole Lowpass Morph
OB-Xd Xpander Lowpass 6 dB
OB-Xd Xpander Lowpass 12 dB
OB-Xd Xpander Lowpass 18 dB
OB-Xd Xpander Lowpass 24 dB
OB-Xd Xpander Highpass 6 dB
OB-Xd Xpander Highpass 12 dB
OB-Xd Xpander Highpass 18 dB
OB-Xd Xpander Bandpass 12 dB
OB-Xd Xpander Bandpass 12 dB/6 dB
OB-Xd Xpander Bandpass 18 dB/6 dB
OB-Xd Xpander Bandpass 24 dB
OB-Xd Xpander Notch 12 dB
OB-Xd Xpander Phaser 18 dB
OB-Xd Xpander Notch+LP 12 dB/6 dB
OB-Xd Xpander Phaser+LP 18 dB/6 dB
K35 Lowpass None
K35 Lowpass Mild
K35 Lowpass Moderate
K35 Lowpass Heavy
K35 Lowpass Extreme
K35 Lowpass Continuous
K35 Highpass None
K35 Highpass Mild
K35 Highpass Moderate
K35 Highpass Heavy
K35 Highpass Extreme
K35 Highpass Continuous
Diode Ladder Lowpass 6 dB
Diode Ladder Lowpass 12 dB
Diode Ladder Lowpass 18 dB
Diode Ladder Lowpass 24 dB
Vintage Ladder Lowpass Runge-Kutta
Vintage Ladder Lowpass Runge-Kutta Compensated
Vintage Ladder Lowpass Huovilainen
Vintage Ladder Lowpass Huovilainen Compensated
Cutoff Warp Lowpass 1 Stage tanh
Cutoff Warp Lowpass 1 Stage Soft Clip
Cutoff Warp Lowpass 1 Stage OJD
Cutoff Warp Lowpass 2 Stages tanh
Cutoff Warp Lowpass 2 Stages Soft Clip
Cutoff Warp Lowpass 2 Stages OJD
Cutoff Warp Lowpass 3 Stages tanh
Cutoff Warp Lowpass 3 Stages Soft Clip
Cutoff Warp Lowpass 3 Stages OJD
Cutoff Warp Lowpass 4 Stages tanh
Cutoff Warp Lowpass 4 Stages Soft Clip
Cutoff Warp Lowpass 4 Stages OJD
Cutoff Warp Highpass 1 Stage tanh
Cutoff Warp Highpass 1 Stage Soft Clip
Cutoff Warp Highpass 1 Stage OJD
Cutoff Warp Highpass 2 Stages tanh
Cutoff Warp Highpass 2 Stages Soft Clip
Cutoff Warp Highpass 2 Stages OJD
Cutoff Warp Highpass 3 Stages tanh
Cutoff Warp Highpass 3 Stages Soft Clip
Cutoff Warp Highpass 3 Stages OJD
Cutoff Warp Highpass 4 Stages tanh
Cutoff Warp Highpass 4 Stages Soft Clip
Cutoff Warp Highpass 4 Stages OJD
Cutoff Warp Bandpass 1 Stage tanh
Cutoff Warp Bandpass 1 Stage Soft Clip
Cutoff Warp Bandpass 1 Stage OJD
Cutoff Warp Bandpass 2 Stages tanh
Cutoff Warp Bandpass 2 Stages Soft Clip
Cutoff Warp Bandpass 2 Stages OJD
Cutoff Warp Bandpass 3 Stages tanh
Cutoff Warp Bandpass 3 Stages Soft Clip
Cutoff Warp Bandpass 3 Stages OJD
Cutoff Warp Bandpass 4 Stages tanh
Cutoff Warp Bandpass 4 Stages Soft Clip
Cutoff Warp Bandpass 4 Stages OJD
Cutoff Warp Notch 1 Stage tanh
Cutoff Warp Notch 1 Stage Soft Clip
Cutoff Warp Notch 1 Stage OJD
Cutoff Warp Notch 2 Stages tanh
Cutoff Warp Notch 2 Stages Soft Clip
Cutoff Warp Notch 2 Stages OJD
Cutoff Warp Notch 3 Stages tanh
Cutoff Warp Notch 3 Stages Soft Clip
Cutoff Warp Notch 3 Stages OJD
Cutoff Warp Notch 4 Stages tanh
Cutoff Warp Notch 4 Stages Soft Clip
Cutoff Warp Notch 4 Stages OJD
Cutoff Warp Allpass 1 Stage tanh
Cutoff Warp Allpass 1 Stage Soft Clip
Cutoff Warp Allpass 1 Stage OJD
Cutoff Warp Allpass 2 Stages tanh
Cutoff Warp Allpass 2 Stages Soft Clip
Cutoff Warp Allpass 2 Stages OJD
Cutoff Warp Allpass 3 Stages tanh
Cutoff Warp Allpass 3 Stages Soft Clip
Cutoff Warp Allpass 3 Stages OJD
Cutoff Warp Allpass 4 Stages tanh
Cutoff Warp Allpass 4 Stages Soft Clip
Cutoff Warp Allpass 4 Stages OJD
Resonance Warp Lowpass 1 Stage tanh
Resonance Warp Lowpass 1 Stage Soft Clip
Resonance Warp Lowpass 2 Stages tanh
Resonance Warp Lowpass 2 Stages Soft Clip
Resonance Warp Lowpass 3 Stages tanh
Resonance Warp Lowpass 3 Stages Soft Clip
Resonance Warp Lowpass 4 Stages tanh
Resonance Warp Lowpass 4 Stages Soft Clip
Resonance Warp Highpass 1 Stage tanh
Resonance Warp Highpass 1 Stage Soft Clip
Resonance Warp Highpass 2 Stages tanh
Resonance Warp Highpass 2 Stages Soft Clip
Resonance Warp Highpass 3 Stages tanh
Resonance Warp Highpass 3 Stages Soft Clip
Resonance Warp Highpass 4 Stages tanh
Resonance Warp Highpass 4 Stages Soft Clip
Resonance Warp Bandpass 1 Stage tanh
Resonance Warp Bandpass 1 Stage Soft Clip
Resonance Warp Bandpass 2 Stages tanh
Resonance Warp Bandpass 2 Stages Soft Clip
Resonance Warp Bandpass 3 Stages tanh
Resonance Warp Bandpass 3 Stages Soft Clip
Resonance Warp Bandpass 4 Stages tanh
Resonance Warp Bandpass 4 Stages Soft Clip
Resonance Warp Notch 1 Stage tanh
Resonance Warp Notch 1 Stage Soft Clip
Resonance Warp Notch 2 Stages tanh
Resonance Warp Notch 2 Stages Soft Clip
Resonance Warp Notch 3 Stages tanh
Resonance Warp Notch 3 Stages Soft Clip
Resonance Warp Notch 4 Stages tanh
Resonance Warp Notch 4 Stages Soft Clip
Resonance Warp Allpass 1 Stage tanh
Resonance Warp Allpass 1 Stage Soft Clip
Resonance Warp Allpass 2 Stages tanh
Resonance Warp Allpass 2 Stages Soft Clip
Resonance Warp Allpass 3 Stages tanh
Resonance Warp Allpass 3 Stages Soft Clip
Resonance Warp Allpass 4 Stages tanh
Resonance Warp Allpass 4 Stages Soft Clip
Fast SVF Lowpass
Fast SVF Highpass
Fast SVF Bandpass
Fast SVF Notch
Fast SVF Peak
Fast SVF Allpass
Fast SVF Low Shelf
Fast SVF Bell
Fast SVF High Shelf
Tri-pole 1 Stage Low/Low/Low
Tri-pole 1 Stage Low/High/Low
Tri-pole 1 Stage High/Low/High
Tri-pole 1 Stage High/High/High
Tri-pole 2 Stages Low/Low/Low
Tri-pole 2 Stages Low/High/Low
Tri-pole 2 Stages High/Low/High
Tri-pole 2 Stages High/High/High
Tri-pole 3 Stages Low/Low/Low
Tri-pole 3 Stages Low/High/Low
Tri-pole 3 Stages High/Low/High
Tri-pole 3 Stages High/High/High
Comb Negative Feedback, Variable Mix
Comb Negative Feedback, Mix 100%
Comb Negative Feedback, Mix 50%
Comb Postive Feedback, Mix 50%
Comb Positive Feedback, Mix 100%
Comb Positive Feedback, Variable Mix
Comb Bipolar, Variable Mix
Sample & Hold
```

- [ ] **Step 2: Write the failing tests (`tests/test_sst_filter_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "alloc_counter.h"
#include "engine/sst_filter_engine.h"
#include "sst/filters++.h"
#include "test_helpers.h"

using namespace eg13;
namespace sfpp = sst::filtersplusplus;

namespace {

ParamValues defaults() { return test::defaultParams(SstFilterEngine::staticInfo()); }

constexpr size_t kLine = size_t(sst::filters::utilities::MAX_FB_COMB) +
                         size_t(sst::filters::utilities::SincTable::FIRipol_N);

std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> upstreamTypes() {
  std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> all;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) all.push_back({model, config});
  }
  return all;
}

int typeNamed(const std::string& name) {
  for (int i = 0; i < SstFilterEngine::typeCount(); ++i) {
    if (name == SstFilterEngine::typeName(i)) return i;
  }
  FAIL("no Classic Filters type named " << name);
  return -1;
}

// sst-filters driven directly, the way the adapter drives it.
struct Reference {
  sfpp::Filter f;
  std::vector<float> delay;
  Reference(int type, double rate) : delay(4 * kLine, 0.0f) {
    const auto all = upstreamTypes();
    f.setSampleRateAndBlockSize(rate, 16);
    for (int lane = 0; lane < 4; ++lane) f.provideDelayLine(lane, delay.data() + size_t(lane) * kLine);
    f.setFilterModel(all[size_t(type)].first);
    f.setModelConfiguration(all[size_t(type)].second);
    REQUIRE(f.prepareInstance());
    f.setStereo();
  }
  void block(const float* in, float* outL, float* outR, float cutoff, float reso, float extra, bool constant) {
    if (constant) {
      f.makeConstantCoefficients(0, cutoff, reso, extra);
    } else {
      f.makeCoefficients(0, cutoff, reso, extra);
    }
    f.copyCoefficientsFromVoiceToVoice(0, 1);
    f.prepareBlock();
    for (int i = 0; i < 16; ++i) f.processStereoSample(in[i], in[i], outL[i], outR[i]);
    f.concludeBlock();
  }
};

std::vector<float> saw(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(hz * double(i) / rate, 1.0) - 1.0);
  return x;
}

void runBlock(SstFilterEngine& e, const float* l, const float* r, float* ol, float* orr, const ParamValues& p) {
  const float* in[2] = {l, r};
  float* out[2] = {ol, orr};
  e.process(in, out, p, 16);
}

}  // namespace

TEST_CASE("Classic Filters describes itself") {
  const auto& info = SstFilterEngine::staticInfo();
  REQUIRE(std::string(info.id) == "sst");
  REQUIRE(std::string(info.displayName) == "Classic Filters");
  REQUIRE(info.paramCount == 4);
  REQUIRE(info.usesInput);
  const char* const names[4] = {"Cutoff", "Resonance", "Type", "Character"};
  for (int i = 0; i < 4; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);
  REQUIRE(SstFilterEngine::typeIndex(info.params[2].defaultValue) == 0);
  char buf[64];
  info.params[2].format(info.params[2].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Vember Classic Lowpass 12 dB Standard");
  REQUIRE(std::fabs(SstFilterEngine::cutoffNote(1.0f) - 12.0f * std::log2(20000.0f / 440.0f)) < 1e-3f);
  REQUIRE(std::fabs(SstFilterEngine::cutoffNote(0.0f) - 12.0f * std::log2(20.0f / 440.0f)) < 1e-3f);
}

TEST_CASE("Classic Filters keep their 205 types in a permanent order") {
  std::ifstream file(std::string(EG13_TEST_DATA_DIR) + "/classic_filter_types.txt");
  REQUIRE(file.good());
  std::vector<std::string> frozen;
  for (std::string line; std::getline(file, line);) {
    if (!line.empty()) frozen.push_back(line);
  }
  REQUIRE(frozen.size() == 205);
  REQUIRE(SstFilterEngine::typeCount() == 205);
  for (int i = 0; i < 205; ++i) {
    INFO("type " << i);
    REQUIRE(std::string(SstFilterEngine::typeName(i)) == frozen[size_t(i)]);
  }
}

TEST_CASE("Classic Filters Type steps map to each type") {
  for (int i = 0; i < SstFilterEngine::typeCount(); ++i) {
    REQUIRE(SstFilterEngine::typeIndex(SstFilterEngine::typeValue(i)) == i);
  }
  REQUIRE(SstFilterEngine::typeIndex(1.0f) == SstFilterEngine::typeCount() - 1);
  REQUIRE(SstFilterEngine::typeIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
  REQUIRE(SstFilterEngine::typeHasCharacter(typeNamed("K35 Lowpass Continuous")));
  REQUIRE_FALSE(SstFilterEngine::typeHasCharacter(0));
}

TEST_CASE("Classic Filters match sst-filters sample for sample") {
  const char* const named[] = {"Vember Classic Lowpass 12 dB Standard",
                               "Vintage Ladder Lowpass Runge-Kutta",
                               "K35 Lowpass Continuous",
                               "Fast SVF Bell",
                               "OB-Xd 4-pole Lowpass Morph",
                               "Comb Positive Feedback, Variable Mix",
                               "Diode Ladder Lowpass 24 dB"};
  const auto in = saw(1600, 110.0, 48000.0);
  for (const char* name : named) {
    const int type = typeNamed(name);
    INFO(name);
    SstFilterEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.6f;
    p.p[1] = 0.4f;
    p.p[2] = SstFilterEngine::typeValue(type);
    p.p[3] = 0.7f;
    Reference ref(type, 48000.0);
    const auto all = upstreamTypes();
    const bool bipolar = sfpp::Filter::coefficientsExtraIsBipolar(all[size_t(type)].first, all[size_t(type)].second, 0);
    const float extra = bipolar ? 2.0f * 0.7f - 1.0f : 0.7f;
    float l[16], r[16], el[16], er[16];
    for (size_t b = 0; b < in.size() / 16; ++b) {
      runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, p);
      ref.block(in.data() + b * 16, el, er, SstFilterEngine::cutoffNote(0.6f), 0.4f, extra, b == 0);
      for (int i = 0; i < 16; ++i) {
        REQUIRE(l[i] == el[i]);
        REQUIRE(r[i] == er[i]);
      }
    }
  }
}

TEST_CASE("Classic Filters interpolate a cutoff step across one block and snap after a reset") {
  const auto in = saw(64, 220.0, 48000.0);
  SstFilterEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues a = defaults();
  a.p[0] = 0.3f;
  ParamValues b = a;
  b.p[0] = 0.8f;
  Reference interpolated(0, 48000.0), stepped(0, 48000.0);
  float l[16], r[16], il[16], ir[16], sl[16], sr[16];
  runBlock(engine, in.data(), in.data(), l, r, a);  // first block: constant coefficients
  interpolated.block(in.data(), il, ir, SstFilterEngine::cutoffNote(0.3f), 0.0f, 0.5f, true);
  stepped.block(in.data(), sl, sr, SstFilterEngine::cutoffNote(0.3f), 0.0f, 0.5f, true);
  for (int i = 0; i < 16; ++i) REQUIRE(l[i] == il[i]);
  runBlock(engine, in.data() + 16, in.data() + 16, l, r, b);  // the step
  interpolated.block(in.data() + 16, il, ir, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, false);
  stepped.block(in.data() + 16, sl, sr, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, true);
  bool differsFromStep = false;
  for (int i = 0; i < 16; ++i) {
    REQUIRE(l[i] == il[i]);
    differsFromStep = differsFromStep || l[i] != sl[i];
  }
  REQUIRE(differsFromStep);
  engine.reset();
  Reference fresh(0, 48000.0);
  runBlock(engine, in.data() + 32, in.data() + 32, l, r, b);
  fresh.block(in.data() + 32, il, ir, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, true);
  for (int i = 0; i < 16; ++i) REQUIRE(l[i] == il[i]);
}

TEST_CASE("Classic Filters keep L and R identical for a mono input, for every type") {
  const auto in = saw(320, 110.0, 48000.0);
  for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
    SstFilterEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.5f;
    p.p[1] = 0.5f;
    p.p[2] = SstFilterEngine::typeValue(type);
    float l[16], r[16];
    for (size_t b = 0; b < in.size() / 16; ++b) {
      runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, p);
      for (int i = 0; i < 16; ++i) {
        INFO(SstFilterEngine::typeName(type));
        REQUIRE(l[i] == r[i]);
        REQUIRE(std::isfinite(l[i]));
      }
    }
  }
}

TEST_CASE("Classic Filters survive NaN and full resonance in every type") {
  const auto in = saw(1600, 110.0, 48000.0);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
    for (bool nan : {false, true}) {
      SstFilterEngine engine;
      engine.prepare(48000.0, 256);
      ParamValues p = defaults();
      p.p[0] = 0.5f;
      p.p[1] = 1.0f;
      p.p[2] = SstFilterEngine::typeValue(type);
      float l[16], r[16];
      for (size_t b = 0; b < in.size() / 16; ++b) {
        runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, nan ? bad : p);
        for (int i = 0; i < 16; ++i) {
          INFO(SstFilterEngine::typeName(type) << (nan ? " (NaN)" : ""));
          REQUIRE(std::isfinite(l[i]));
        }
      }
    }
  }
}

TEST_CASE("Classic Filters type changes and processing do not allocate") {
  SstFilterEngine engine;
  engine.prepare(48000.0, 256);
  float in[16] = {}, l[16], r[16];
  for (int i = 0; i < 16; ++i) in[i] = 0.1f * float(i % 4);
  ParamValues p = defaults();
  runBlock(engine, in, in, l, r, p);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
      p.p[2] = SstFilterEngine::typeValue(type);
      p.p[0] = float(type % 10) / 10.0f;
      runBlock(engine, in, in, l, r, p);
      runBlock(engine, in, in, l, r, p);
    }
    const float* i2[2] = {in, in};
    float* o2[2] = {l, r};
    engine.process(i2, o2, p, 7);  // a short block re-times the coefficient ramp
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/sst_filter_engine.h` not found).

- [ ] **Step 4: Write `core/engine/sst_filter_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for surge-synthesizer/sst-filters (GPL-3.0-or-later), shown as "Classic Filters":
// one of 205 filter types chosen on the card. Runs at the host rate.
// L and R are lanes 0 and 1 of one SIMD filter: the right lane costs nothing extra and, fed the
// same samples while the input is mono, holds exactly the left lane's state. So it always runs
// and setStereo() needs no action: spec §5.1's second instance is that lane.
class SstFilterEngine final : public VoiceEngine {
 public:
  static constexpr int kTypeSteps = 256;  // P3 resolution: type = floor(P3 * 256)

  SstFilterEngine();
  ~SstFilterEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int typeCount();
  static int typeIndex(float normalized);  // P3 -> 0..typeCount()-1 (NaN -> 0)
  static float typeValue(int index);       // the centre of a type's P3 step
  static const char* typeName(int index);
  static bool typeHasCharacter(int index);  // the type uses sst's extra coefficient (P4)
  static float cutoffNote(float normalized);  // P1 -> semitones from A440, sst's cutoff unit

 private:
  struct Impl;
  void selectType(int type);

  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  int type_ = -1;
  int block_ = 0;
  bool snap_ = true;
};

}  // namespace eg13
```

- [ ] **Step 5: Write `core/engine/sst_filter_engine.cpp`**

```cpp
#include "engine/sst_filter_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/param_format.h"
#include "sst/filters++.h"

namespace eg13 {

namespace {

namespace sfpp = sst::filtersplusplus;

struct ClassicType {
  sfpp::FilterModel model;
  sfpp::ModelConfig config;
  std::string name;
  bool character;  // uses sst's one extra coefficient (filter_impl.h:55-82)
  bool bipolar;    // that coefficient is bipolar (filter_impl.h:84-105)
};

// Every sst-filters model and configuration in upstream order (Filter::availableModels(), each
// model's availableModelConfigurations sorted): 205 at the pinned commit. This order is EG13's
// permanent type index, frozen by tests/data/classic_filter_types.txt. Built on first use (the
// engine's constructor, main thread), which also builds sst's own configuration tables, so
// later type changes do not allocate.
const std::vector<ClassicType>& types() {
  static const std::vector<ClassicType> table = [] {
    std::vector<ClassicType> t;
    for (auto model : sfpp::Filter::availableModels()) {
      for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) {
        t.push_back({model, config, sfpp::details::FilterPayload::displayName(model, config),
                     sfpp::Filter::coefficientsExtraCount(model, config) > 0,
                     sfpp::Filter::coefficientsExtraIsBipolar(model, config, 0)});
      }
    }
    return t;
  }();
  return table;
}

// Comb filters need an external delay line per SIMD lane (filter_impl.h:188-193).
constexpr size_t kDelayLine = size_t(sst::filters::utilities::MAX_FB_COMB) +
                              size_t(sst::filters::utilities::SincTable::FIRipol_N);

void formatCutoff(float v, char* buf, size_t size) { formatHz(knobHz(v), buf, size); }
void formatType(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", SstFilterEngine::typeName(SstFilterEngine::typeIndex(v)));
}

// Cutoff: the Analog Filter's 20 Hz .. 20 kHz curve, default fully open. Resonance 0..1,
// default 0. Type 0 is Surge's classic 12 dB low-pass. Character: sst's extra coefficient
// (OB-Xd morph, K35 saturation, Fast SVF shelf/bell gain, comb mix), centred by default.
const EngineParamInfo kParams[4] = {
    {"Cutoff", 1.0f, formatCutoff},
    {"Resonance", 0.0f, formatPercent01},
    {"Type", 0.5f / float(SstFilterEngine::kTypeSteps), formatType},
    {"Character", 0.5f, formatPercent01},
};

const EngineInfo kInfo = {"sst", "Classic Filters", 4, kParams, true, false, 0, nullptr};

}  // namespace

struct SstFilterEngine::Impl {
  sfpp::Filter filter;
  std::vector<float> delay;  // comb delay lines, kDelayLine per lane
};

SstFilterEngine::SstFilterEngine() : impl_(std::make_unique<Impl>()) {
  types();
  impl_->delay.assign(4 * kDelayLine, 0.0f);
  for (int lane = 0; lane < 4; ++lane) {
    impl_->filter.provideDelayLine(lane, impl_->delay.data() + size_t(lane) * kDelayLine);
  }
}

SstFilterEngine::~SstFilterEngine() = default;

const EngineInfo& SstFilterEngine::staticInfo() { return kInfo; }

const EngineInfo& SstFilterEngine::info() const { return kInfo; }

int SstFilterEngine::typeCount() { return int(types().size()); }

int SstFilterEngine::typeIndex(float normalized) {
  const int i = int(clamp01(normalized) * float(kTypeSteps));
  return std::min(i, typeCount() - 1);
}

float SstFilterEngine::typeValue(int index) { return (float(index) + 0.5f) / float(kTypeSteps); }

const char* SstFilterEngine::typeName(int index) {
  return types()[size_t(std::clamp(index, 0, typeCount() - 1))].name.c_str();
}

bool SstFilterEngine::typeHasCharacter(int index) {
  return types()[size_t(std::clamp(index, 0, typeCount() - 1))].character;
}

float SstFilterEngine::cutoffNote(float normalized) {
  return 12.0f * std::log2(knobHz(normalized) / 440.0f);
}

void SstFilterEngine::prepare(double hostRate, int) {
  rate_ = hostRate;
  block_ = 16;
  impl_->filter.setSampleRateAndBlockSize(hostRate, size_t(block_));
  type_ = -1;  // selected again (prepareInstance) on the next process()
  reset();
}

void SstFilterEngine::reset() {
  impl_->filter.reset();
  std::fill(impl_->delay.begin(), impl_->delay.end(), 0.0f);
  snap_ = true;
}

void SstFilterEngine::selectType(int type) {
  const ClassicType& t = types()[size_t(type)];
  sfpp::Filter& f = impl_->filter;
  f.setFilterModel(t.model);
  f.setModelConfiguration(t.config);
  (void)f.prepareInstance();  // every listed type prepares (test_third_party_smoke.cpp); it resets
  f.setStereo();              // lanes 0 (L) and 1 (R) active
  type_ = type;
  snap_ = true;
}

void SstFilterEngine::process(const float* const* in, float* const* out, const ParamValues& p,
                              int n) {
  sfpp::Filter& f = impl_->filter;
  const int type = typeIndex(p.p[2]);
  if (type != type_) selectType(type);  // like the hardware's type switch: immediate
  if (n != block_) {
    f.setSampleRateAndBlockSize(rate_, size_t(n));
    block_ = n;
  }
  const ClassicType& t = types()[size_t(type_)];
  const float cutoff = cutoffNote(p.p[0]);
  const float reso = clamp01(p.p[1]);
  const float extra = t.bipolar ? 2.0f * clamp01(p.p[3]) - 1.0f : clamp01(p.p[3]);
  // sst-filters interpolates its coefficients across each block, as in Surge (dC = (tC - C) /
  // blockSize, FilterCoefficientMaker_Impl.h:848): that is this engine's smoothing. The first
  // block after a reset or a type change starts on its target.
  if (snap_) {
    f.makeConstantCoefficients(0, cutoff, reso, extra);
    snap_ = false;
  } else {
    f.makeCoefficients(0, cutoff, reso, extra);
  }
  f.copyCoefficientsFromVoiceToVoice(0, 1);
  f.prepareBlock();
  for (int i = 0; i < n; ++i) {
    const float l = in != nullptr ? in[0][i] : 0.0f;
    const float r = in != nullptr ? in[1][i] : 0.0f;
    f.processStereoSample(l, r, out[0][i], out[1][i]);
  }
  f.concludeBlock();
}

}  // namespace eg13
```

Add `engine/sst_filter_engine.cpp` to the `add_library(eg13_core ...)` list in `core/CMakeLists.txt`.

- [ ] **Step 5a: Document Classic Filters in the manual**

Append to `docs/manual/engines.md`:

````markdown
### Classic Filters (sst-filters, the filters of the Surge synthesizer)

Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance**, **Type** and **Character**.

- **Type** chooses one of 205 filters: low-, high-, band-pass and notch filters of 12 and 24 dB, ladder models (Legacy, Vintage, Diode), OB-Xd and K35 models, Cutoff Warp and Resonance Warp, the Xpander's pole mixes, Tri-pole, the Fast SVF's shelves and bell, combs, and Sample & Hold. The default is *Vember Classic Lowpass 12 dB Standard*. The list keeps its order in every future version, so a saved type always comes back.
- **Character** is the extra control some types have: OB-Xd 4-pole *Morph*, K35 *Continuous* saturation, the Fast SVF shelves' and bell's gain, and the comb filters' variable mix. Other types ignore it.
- Changing Type takes effect at once and restarts the filter, as switching a filter type on hardware does; it can click on a sounding note.
- Stereo: left and right always run together inside one filter, at no extra cost.
````

Append to `docs/manual/smoothing.md`:

````markdown
- **Classic Filters:** as in Surge, the filter's coefficients move smoothly across each 16-sample control block. Type switches at once.
````

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Classic Filters"`
Expected: all pass. Then the full suite and the Docker command: all pass.

- [ ] **Step 7: Commit**

```bash
git add core/engine/sst_filter_engine.h core/engine/sst_filter_engine.cpp core/CMakeLists.txt \
  tests/test_sst_filter_engine.cpp tests/data/classic_filter_types.txt docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Classic Filters: sst-filters' 205 types with a frozen type order"
```

---
### Task 7: State Variable Filter (stmlib Svf)

**Files:**
- Manual: `docs/manual/engines.md`, `docs/manual/smoothing.md` (same commit)
- Create: `core/engine/svf_engine.h`, `core/engine/svf_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/svf_engine.cpp`)
- Test: `tests/test_svf_engine.cpp`

**Interfaces:**
- Consumes: `ParamRamp`, `StereoLink`, `knobHz`, `stepIndex`, formatters (Task 3).
- Produces: `class SvfEngine final : public VoiceEngine` ("State Variable Filter", id `"svf"`, host rate, `usesInput`): `static const EngineInfo& staticInfo(); static float resonanceQ(float normalized);` (0.5·80^p), `static int modeIndex(float normalized);` (0 LP, 1 BP, 2 HP), `static constexpr float kMaxF = 0.497f; float smoothedParam(int index) const; bool rightRuns() const;`. P1 Cutoff (1.0), P2 Resonance (Q 0.707), P3 Mode (LP).

- [ ] **Step 1: Write the failing tests (`tests/test_svf_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/param_format.h"
#include "engine/svf_engine.h"
#include "stmlib/dsp/filter.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(SvfEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

std::vector<float> saw(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(hz * double(i) / rate, 1.0) - 1.0);
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(SvfEngine& e, const std::vector<float>& inL, const std::vector<float>& inR, const ParamValues& p) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size(); done += 16) {
    const int count = int(std::min<size_t>(16, inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
  }
  return s;
}

float runMode(stmlib::Svf& f, int mode, float x) {
  if (mode == 0) return f.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
  if (mode == 1) return f.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);
  return f.Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
}

double gainAt(double hz, const ParamValues& p) {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(24000, hz, 48000.0);
  const Stereo out = render(engine, in, in, p);
  return test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000);
}

}  // namespace

TEST_CASE("State Variable Filter describes itself") {
  const auto& info = SvfEngine::staticInfo();
  REQUIRE(std::string(info.id) == "svf");
  REQUIRE(std::string(info.displayName) == "State Variable Filter");
  REQUIRE(info.paramCount == 3);
  REQUIRE(info.usesInput);
  const char* const names[3] = {"Cutoff", "Resonance", "Mode"};
  for (int i = 0; i < 3; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);
  REQUIRE(std::fabs(SvfEngine::resonanceQ(info.params[1].defaultValue) - 0.70710678f) < 1e-4f);
  REQUIRE(SvfEngine::resonanceQ(0.0f) == 0.5f);
  REQUIRE(std::fabs(SvfEngine::resonanceQ(1.0f) - 40.0f) < 1e-3f);
  char buf[32];
  const char* const modes[3] = {"LP", "BP", "HP"};
  for (int m = 0; m < 3; ++m) {
    info.params[2].format((float(m) + 0.5f) / 3.0f, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
  }
}

TEST_CASE("State Variable Filter matches stmlib's Svf sample for sample") {
  for (int mode = 0; mode < 3; ++mode) {
    SvfEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.55f;
    p.p[1] = 0.6f;
    p.p[2] = (float(mode) + 0.5f) / 3.0f;
    const auto in = saw(4800, 110.0, 48000.0);
    const Stereo out = render(engine, in, in, p);
    stmlib::Svf ref;
    ref.Init();
    const float f = std::min(knobHz(0.55f) / 48000.0f, SvfEngine::kMaxF);
    const float q = SvfEngine::resonanceQ(0.6f);
    for (size_t i = 0; i < in.size(); ++i) {
      ref.set_f_q<stmlib::FREQUENCY_ACCURATE>(f, q);
      INFO("mode " << mode << " sample " << i);
      REQUIRE(out.l[i] == runMode(ref, mode, in[i]));
      REQUIRE(out.r[i] == out.l[i]);
    }
  }
}

TEST_CASE("State Variable Filter ramps cutoff and Q across a block and snaps after reset") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = saw(48, 220.0, 48000.0);
  ParamValues a = defaults();
  a.p[0] = 0.3f;
  ParamValues b = a;
  b.p[0] = 0.7f;
  b.p[1] = 0.5f;
  std::vector<float> out(48), right(48);
  for (int block = 0; block < 3; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  const float fa = knobHz(0.3f) / 48000.0f, fb = knobHz(0.7f) / 48000.0f;
  const float qa = SvfEngine::resonanceQ(a.p[1]), qb = SvfEngine::resonanceQ(0.5f);
  const float df = (fb - fa) / 16.0f, dq = (qb - qa) / 16.0f;
  stmlib::Svf ref;
  ref.Init();
  for (int i = 0; i < 48; ++i) {
    float f = fb, q = qb;
    if (i < 16) {
      f = fa;
      q = qa;
    } else if (i < 31) {
      f = fa + df * float(i - 15);
      q = qa + dq * float(i - 15);
    }
    ref.set_f_q<stmlib::FREQUENCY_ACCURATE>(f, q);
    REQUIRE(out[size_t(i)] == ref.Process<stmlib::FILTER_MODE_LOW_PASS>(in[size_t(i)]));
  }
  REQUIRE(engine.smoothedParam(0) == fb);
  engine.reset();
  const float* i2[2] = {in.data(), in.data()};
  float* o2[2] = {out.data(), right.data()};
  engine.process(i2, o2, a, 16);
  stmlib::Svf fresh;
  fresh.Init();
  for (int i = 0; i < 16; ++i) {
    fresh.set_f_q<stmlib::FREQUENCY_ACCURATE>(fa, qa);
    REQUIRE(out[size_t(i)] == fresh.Process<stmlib::FILTER_MODE_LOW_PASS>(in[size_t(i)]));
  }
}

TEST_CASE("State Variable Filter modes pass and stop the right bands") {
  ParamValues p = defaults();
  p.p[0] = 0.5f;  // ~632 Hz
  REQUIRE(gainAt(60.0, p) > 0.9);
  REQUIRE(gainAt(8000.0, p) < 0.02);
  p.p[2] = 2.5f / 3.0f;  // HP
  REQUIRE(gainAt(60.0, p) < 0.02);
  REQUIRE(gainAt(8000.0, p) > 0.9);
  p.p[2] = 1.5f / 3.0f;  // BP, normalized: unity at the cutoff
  REQUIRE(std::fabs(gainAt(knobHz(0.5f), p) - 1.0) < 0.05);
  REQUIRE(gainAt(60.0, p) < 0.2);
}

TEST_CASE("State Variable Filter at its default resonance is -3 dB at the cutoff") {
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  REQUIRE(std::fabs(20.0 * std::log10(gainAt(knobHz(0.5f), p)) + 3.0) < 0.3);
}

TEST_CASE("State Variable Filter runs its right instance only while stereo, starting seamlessly") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[0] = 0.5f;
  p.p[1] = 0.8f;
  const auto x = saw(4800, 110.0, 48000.0);
  const auto y = saw(4800, 165.0, 48000.0);
  const Stereo mono = render(engine, x, y, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo same = render(engine, x, x, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(same.l == same.r);  // a copied state with the same input stays identical
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(apart.l != apart.r);
}

TEST_CASE("State Variable Filter survives NaN and processing does not allocate") {
  SvfEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = saw(480, 110.0, 48000.0);
  const Stereo out = render(engine, x, x, bad);
  for (float s : out.l) REQUIRE(std::isfinite(s));
  ParamValues p = defaults();
  std::vector<float> l(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), l.data()};
  float* o[2] = {ol.data(), orr.data()};
  size_t engineAllocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 200; ++block) {
      engine.setStereo(block % 50 < 25);
      p.p[0] = float(block % 10) / 10.0f;
      p.p[2] = float(block % 3) / 3.0f;
      engine.process(in, o, p, 16);
    }
    engine.reset();
    engineAllocations = counter.count();
  }
  REQUIRE(engineAllocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/svf_engine.h` not found).

- [ ] **Step 3: Write `core/engine/svf_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// stmlib's state-variable filter (stmlib/dsp/filter.h:177-260), shown as "State Variable
// Filter". No module exposes it on its own, so it runs at the host rate and smooths like the
// Plaits engine that uses it (plaits/dsp/engine/noise_engine.cc:76-91).
class SvfEngine final : public VoiceEngine {
 public:
  static constexpr float kMaxF = 0.497f;  // f / rate clip, as stmlib's exact tangent (filter.h:110)

  SvfEngine();
  ~SvfEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override { stereo_ = stereo; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static float resonanceQ(float normalized);  // 0.5 .. 40, exponential
  static int modeIndex(float normalized);     // P3 thirds: 0 LP, 1 BP, 2 HP
  float smoothedParam(int index) const;       // test hook: f (index 0) or Q (1) at block end
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp f_;
  ParamRamp q_;
  StereoLink link_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/svf_engine.cpp`**

```cpp
#include "engine/svf_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/param_format.h"
#include "stmlib/dsp/filter.h"

namespace eg13 {

namespace {

const char* const kModeNames[3] = {"LP", "BP", "HP"};

void formatCutoff(float v, char* buf, size_t size) { formatHz(knobHz(v), buf, size); }
void formatQ(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "Q %.2f", double(SvfEngine::resonanceQ(v)));
}
void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[SvfEngine::modeIndex(v)]);
}

// Resonance default: Q = 1/sqrt(2), a flat (Butterworth) passband: 0.5 * 80^p = 0.7071.
const float kDefaultResonance = float(std::log(2.0 * 0.70710678118654752) / std::log(80.0));

// Cutoff: the Analog Filter's curve, fully open by default. Resonance: Q 0.5..40, Shelves'
// Q knob range (AudibleInstruments shelves.hpp:31-32). Mode: LP / BP / HP (spec §4); BP is
// stmlib's normalized band-pass, unity at the cutoff (filter.h:243-244).
const EngineParamInfo kParams[3] = {
    {"Cutoff", 1.0f, formatCutoff},
    {"Resonance", kDefaultResonance, formatQ},
    {"Mode", 0.5f / 3.0f, formatMode},
};

const EngineInfo kInfo = {"svf", "State Variable Filter", 3, kParams, true, false, 0, nullptr};

float run(stmlib::Svf& f, int mode, float x) {
  if (mode == 0) return f.Process<stmlib::FILTER_MODE_LOW_PASS>(x);
  if (mode == 1) return f.Process<stmlib::FILTER_MODE_BAND_PASS_NORMALIZED>(x);
  return f.Process<stmlib::FILTER_MODE_HIGH_PASS>(x);
}

}  // namespace

struct SvfEngine::Impl {
  stmlib::Svf left;
  stmlib::Svf right;
  std::vector<float> rightOut;
};

SvfEngine::SvfEngine() : impl_(std::make_unique<Impl>()) {}

SvfEngine::~SvfEngine() = default;

const EngineInfo& SvfEngine::staticInfo() { return kInfo; }

const EngineInfo& SvfEngine::info() const { return kInfo; }

float SvfEngine::resonanceQ(float normalized) { return 0.5f * std::pow(80.0f, clamp01(normalized)); }

int SvfEngine::modeIndex(float normalized) { return stepIndex(normalized, 3); }

float SvfEngine::smoothedParam(int index) const { return index == 0 ? f_.value() : q_.value(); }

void SvfEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  impl_->rightOut.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void SvfEngine::reset() {
  impl_->left.Init();
  impl_->right.Init();
  f_.reset();
  q_.reset();
  link_.reset();
}

void SvfEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  // Frequency and Q ramp linearly across the block and set the filter every sample, as Plaits'
  // noise engine does (noise_engine.cc:76-91).
  f_.begin(std::min(knobHz(p.p[0]) / float(rate_), kMaxF), n);
  q_.begin(resonanceQ(p.p[1]), n);
  const int mode = modeIndex(p.p[2]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) s.right = s.left;
  const bool right = link_.rightRuns();
  for (int i = 0; i < n; ++i) {
    s.left.set_f_q<stmlib::FREQUENCY_ACCURATE>(f_.at(i), q_.at(i));
    out[0][i] = run(s.left, mode, in != nullptr ? in[0][i] : 0.0f);
    if (right) {
      s.right.set(s.left);
      s.rightOut[size_t(i)] = run(s.right, mode, in != nullptr ? in[1][i] : 0.0f);
    }
  }
  link_.mix(out[0], s.rightOut.data(), out[1], n);
}

}  // namespace eg13
```

Add `engine/svf_engine.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 4a: Document the State Variable Filter in the manual**

Append to `docs/manual/engines.md`:

````markdown
### State Variable Filter

The clean, digital state-variable filter that the Eurorack modules use inside (no module offers it on its own). Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance** (Q 0.5 to 40; the default, Q 0.707, gives a flat passband) and **Mode**: *LP*, *BP* or *HP*. In *BP* mode the peak stays at unity gain whatever the resonance.

Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state.
````

Append to `docs/manual/smoothing.md`:

````markdown
- **State Variable Filter:** Cutoff and Resonance move in a straight line across each 16-sample control block, as in the Macro Oscillator's own filters. Mode switches at once.
````

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "State Variable Filter"`
Expected: all pass. Then the full suite.

- [ ] **Step 6: Commit**

```bash
git add core/engine/svf_engine.h core/engine/svf_engine.cpp core/CMakeLists.txt tests/test_svf_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "State Variable Filter: stmlib Svf with LP / BP / HP"
```

---
### Task 8: Ring Mod / Vocoder (Warps)

**Files:**
- Create: `core/engine/warps_engine.h`, `core/engine/warps_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/warps_engine.cpp`), `docs/manual/engines.md`, `docs/manual/smoothing.md`
- Test: `tests/test_warps_engine.cpp`

**Interfaces:**
- Consumes: `PotSmoother` (M2), `StereoLink`, formatters (Task 3), `SideSource`, `sideSourceOf`, `nativeLatency` (Task 2), Warps sources in `eg13_eurorack` (Task 1).
- Produces: `class WarpsEngine final : public VoiceEngine` ("Ring Mod / Vocoder", id `"warps"`, `usesInput`, `sideInputs = 2`, `sideSource` = the Carrier and Modulator selectors): `static constexpr double kNativeRate = 96000.0; static constexpr int kNativeBlock = 60; static constexpr int kNumAlgorithms = 9; static constexpr int kNumOscillatorModes = 3; static constexpr int kSelectorSteps = 8; static constexpr int kNumCarriers = 5; static constexpr int kNumModulators = 4; static constexpr float kInputScale = 10240.0f; static constexpr float kDefaultLevel = 0.47f; static const EngineInfo& staticInfo(); static int carrierIndex(float); static int modulatorIndex(float); static float optionValue(int); static float oscillatorOffset(float); static int oscillatorMode(float); static bool shifterMode(float); static short toShort(float); static float fromShort(short); float smoothedParam(int) const; bool rightRuns() const; int nativeLatency() const override` (= 60).
  - P1 Algorithm, P2 Timbre, P3 Carrier Level, P4 Mod Level, P5 Carrier (Internal Osc / Voice / Source AUX / Input / Other part), P6 Modulator (Voice / Source AUX / Input / Other part), P7 Osc Pitch (±24 st), P8 Osc Shape (the module's three oscillator modes), P9 Mode (Modulation / Frequency shifter).
  - Carrier and Modulator are append-only option lists: each is read in 8 fixed steps (`kSelectorSteps`, option i at `(i + 0.5) / 8`), so adding an option later never changes what a saved value means. A test pins both orders. Their defaults (Internal Osc, Voice) live only in `kParams`: changing one later is a one-line edit that affects new patches only (once M7's factory presets exist, those presets would need re-saving; no state migration).
  - Inputs: `in[0]`, `in[1]` the voice L/R ("Voice": the signal reaching the Shaper from this voice's Source → Filter); `in[2]` the carrier side signal and `in[3]` the modulator side signal (Source AUX / Input / Other part; each nullable = silence). Output is delayed by exactly 60 native samples.

- [ ] **Step 1: Write the failing tests (`tests/test_warps_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/native_rate_wrapper.h"
#include "engine/warps_engine.h"
#include "test_helpers.h"
#include "warps/dsp/modulator.h"

using namespace eg13;

namespace {

constexpr double kRate = WarpsEngine::kNativeRate;
constexpr int kN = WarpsEngine::kNativeBlock;

ParamValues defaults() { return test::defaultParams(WarpsEngine::staticInfo()); }

// Carrier and Modulator options (eighths, append-only).
constexpr float kCarrierOsc = 0.5f / 8.0f;
constexpr float kCarrierVoice = 1.5f / 8.0f;
constexpr float kCarrierAux = 2.5f / 8.0f;
constexpr float kCarrierInput = 3.5f / 8.0f;
constexpr float kCarrierOther = 4.5f / 8.0f;
constexpr float kModVoice = 0.5f / 8.0f;
constexpr float kModAux = 1.5f / 8.0f;
constexpr float kModInput = 2.5f / 8.0f;
constexpr float kModOther = 3.5f / 8.0f;

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

// carrierSide / modulatorSide: what the Voice would deliver to the side inputs (nullable).
Stereo run(VoiceEngine& e, const std::vector<float>& voiceL, const std::vector<float>& voiceR,
           const std::vector<float>* carrierSide, const std::vector<float>* modulatorSide, const ParamValues& p,
           int chunk = kN) {
  Stereo s{std::vector<float>(voiceL.size()), std::vector<float>(voiceL.size())};
  for (size_t done = 0; done < voiceL.size();) {
    const int count = int(std::min<size_t>(size_t(chunk), voiceL.size() - done));
    const float* in[4] = {voiceL.data() + done, voiceR.data() + done,
                          carrierSide ? carrierSide->data() + done : nullptr,
                          modulatorSide ? modulatorSide->data() + done : nullptr};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
    done += size_t(count);
  }
  return s;
}

struct Settings {
  float algorithm, timbre, level1, level2;
};

// Upstream Warps driven as the adapter drives it, block by block: carrierJack into channel 1
// (CARRIER), modulatorJack into channel 2 (MODULATOR), the internal-oscillator button as
// carrierShape (0 = off), the easter egg as shifter.
std::vector<float> reference(const std::vector<float>& carrierJack, const std::vector<float>& modulatorJack,
                             const Settings& s, int carrierShape, bool shifter, float note = 60.0f) {
  auto m = std::make_unique<warps::Modulator>();
  std::memset(static_cast<void*>(m.get()), 0, sizeof(warps::Modulator));
  m->Init(float(kRate));
  std::vector<float> out(carrierJack.size());
  warps::ShortFrame in[kN], o[kN];
  for (size_t b = 0; b + kN <= carrierJack.size(); b += kN) {
    warps::Parameters* w = m->mutable_parameters();
    w->channel_drive[0] = s.level1;
    w->channel_drive[1] = s.level2;
    w->modulation_algorithm = s.algorithm;
    w->modulation_parameter = s.timbre;
    w->frequency_shift_pot = s.algorithm;
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = s.algorithm;
    w->note = note;
    w->carrier_shape = carrierShape;
    m->set_easter_egg(shifter);
    for (int i = 0; i < kN; ++i) {
      in[i].l = WarpsEngine::toShort(carrierJack[b + size_t(i)]);
      in[i].r = WarpsEngine::toShort(modulatorJack[b + size_t(i)]);
    }
    m->Process(in, o, kN);
    for (int i = 0; i < kN; ++i) out[b + size_t(i)] = WarpsEngine::fromShort(o[i].l);
  }
  return out;
}

ParamValues with(const Settings& s, float carrier, float modulator, bool shifter) {
  ParamValues p = defaults();
  p.p[0] = s.algorithm;
  p.p[1] = s.timbre;
  p.p[2] = s.level1;
  p.p[3] = s.level2;
  p.p[4] = carrier;
  p.p[5] = modulator;
  p.p[8] = shifter ? 0.75f : 0.25f;
  return p;
}

double magnitudeAt(const std::vector<float>& x, double rate, size_t begin, size_t end, double f) {
  const double pi = 3.14159265358979323846;
  const size_t count = end - begin;
  const double coeff = 2.0 * std::cos(2.0 * pi * f / rate);
  double s1 = 0.0, s2 = 0.0;
  for (size_t i = 0; i < count; ++i) {
    const double hann = 0.5 - 0.5 * std::cos(2.0 * pi * double(i) / double(count));
    const double s0 = double(x[begin + i]) * hann + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
}

}  // namespace

TEST_CASE("Ring Mod / Vocoder describes itself") {
  const auto& info = WarpsEngine::staticInfo();
  REQUIRE(std::string(info.id) == "warps");
  REQUIRE(std::string(info.displayName) == "Ring Mod / Vocoder");
  REQUIRE(info.paramCount == 9);
  REQUIRE(info.usesInput);
  REQUIRE(info.sideInputs == 2);
  REQUIRE(info.sideSource != nullptr);
  const char* const names[9] = {"Algorithm", "Timbre",    "Carrier Level", "Mod Level", "Carrier",
                                "Modulator", "Osc Pitch", "Osc Shape",     "Mode"};
  for (int i = 0; i < 9; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[2].defaultValue == WarpsEngine::kDefaultLevel);
  REQUIRE(info.params[3].defaultValue == WarpsEngine::kDefaultLevel);
  REQUIRE(WarpsEngine::carrierIndex(info.params[4].defaultValue) == 0);    // Internal Osc
  REQUIRE(WarpsEngine::modulatorIndex(info.params[5].defaultValue) == 0);  // Voice
  REQUIRE(WarpsEngine::oscillatorMode(info.params[7].defaultValue) == 0);
  REQUIRE_FALSE(WarpsEngine::shifterMode(info.params[8].defaultValue));
  char buf[64];
  info.params[0].format(info.params[0].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Analog RM");
  info.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Vocoder");
  info.params[0].format(2.5f / 8.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Analog RM > Digital RM 50%");
  info.params[4].format(info.params[4].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Internal Osc");
  info.params[5].format(info.params[5].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Voice");
  info.params[6].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+12.0 st");
  info.params[7].format(2.5f / 3.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Saw / Noise");
  info.params[8].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Freq shifter");
  REQUIRE(WarpsEngine::toShort(1.0f) == 10240);  // ±1.0 = ±5 V of Warps' ±8 V inputs
  REQUIRE(WarpsEngine::fromShort(10240) == 1.0f);
  REQUIRE(WarpsEngine::toShort(10.0f) == 32767);
  REQUIRE(WarpsEngine::toShort(-10.0f) == -32768);
  REQUIRE(WarpsEngine::toShort(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// The Carrier and Modulator option lists are append-only: saved projects store the selector
// value, so these orders and the 8-step reading must never change (owner, 2026-10-20: the routing
// is provisional; options may be added, never moved).
TEST_CASE("Ring Mod / Vocoder Carrier and Modulator options keep their order") {
  const auto& info = WarpsEngine::staticInfo();
  REQUIRE(WarpsEngine::kSelectorSteps == 8);
  const char* const carriers[5] = {"Internal Osc", "Voice", "Source AUX", "Input", "Other part"};
  const SideSource carrierSources[5] = {SideSource::None, SideSource::Voice, SideSource::SourceAux,
                                        SideSource::Input, SideSource::OtherPart};
  const char* const modulators[4] = {"Voice", "Source AUX", "Input", "Other part"};
  char buf[64];
  ParamValues p = defaults();
  for (int i = 0; i < 5; ++i) {
    const float v = WarpsEngine::optionValue(i);
    REQUIRE(v == (float(i) + 0.5f) / 8.0f);
    REQUIRE(WarpsEngine::carrierIndex(v) == i);
    info.params[4].format(v, buf, sizeof(buf));
    REQUIRE(std::string(buf) == carriers[i]);
    p.p[4] = v;
    REQUIRE(info.sideSource(0, p) == carrierSources[i]);
  }
  for (int i = 0; i < 4; ++i) {
    const float v = WarpsEngine::optionValue(i);
    REQUIRE(WarpsEngine::modulatorIndex(v) == i);
    info.params[5].format(v, buf, sizeof(buf));
    REQUIRE(std::string(buf) == modulators[i]);
    p.p[5] = v;
    REQUIRE(info.sideSource(1, p) == SideSource(i));
  }
  REQUIRE(WarpsEngine::carrierIndex(1.0f) == 4);    // the unused steps read as the last option
  REQUIRE(WarpsEngine::modulatorIndex(1.0f) == 3);
  REQUIRE(WarpsEngine::carrierIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
}

// The default Carrier Level and Mod Level (owner, 2026-10-20): the drive at which Warps'
// SaturatingAmplifier (warps/dsp/modulator.h:53-115) passes a full-scale EG13 voice, a 1 kHz sine
// at ±1.0 (±10240 counts, the adapter's input scaling), at unity gain without saturating. Method:
// drive the upstream amplifier alone with limit 1 (cross-modulation), measure the fundamental and
// harmonics 2..9 over 0.5 s after 0.5 s of settling. Planning measured +0.03 dB and 0.06 % THD at
// 0.47 (+0.22 dB at 0.48, -0.17 dB at 0.46); 1.0 gives +12 dB and 38 % THD.
TEST_CASE("Default levels pass a full-scale voice through Warps' amplifier at unity, unsaturated") {
  auto measure = [](float drive, double* gainDb, double* thd) {
    warps::SaturatingAmplifier amplifier;
    std::memset(static_cast<void*>(&amplifier), 0, sizeof(amplifier));
    amplifier.Init();
    const int n = 96000;
    std::vector<float> y(size_t(n));
    short in[kN];
    float out[kN], raw[kN];
    for (int b = 0; b < n / kN; ++b) {
      for (int i = 0; i < kN; ++i) {
        in[i] = WarpsEngine::toShort(float(std::sin(6.283185307179586 * 1000.0 * double(b * kN + i) / kRate)));
      }
      std::memset(raw, 0, sizeof(raw));
      amplifier.Process(drive, 1.0f, in, out, raw, 1, kN);
      for (int i = 0; i < kN; ++i) y[size_t(b * kN + i)] = out[i];
    }
    auto amplitude = [&](double f) { return 2.0 * magnitudeAt(y, kRate, 48000, 96000, f) / 24000.0; };
    const double fundamental = amplitude(1000.0);
    double harmonics = 0.0;
    for (int h = 2; h <= 9; ++h) harmonics += amplitude(1000.0 * h) * amplitude(1000.0 * h);
    *gainDb = 20.0 * std::log10(fundamental / (10240.0 / 32768.0));
    *thd = std::sqrt(harmonics) / fundamental;
  };
  double gain = 0.0, thd = 0.0;
  measure(WarpsEngine::kDefaultLevel, &gain, &thd);
  INFO("default " << WarpsEngine::kDefaultLevel << ": " << gain << " dB, THD " << 100.0 * thd << " %");
  REQUIRE(std::fabs(gain) < 0.25);
  REQUIRE(thd < 0.001);
  measure(1.0f, &gain, &thd);  // the range still reaches the saturated character
  REQUIRE(thd > 0.1);
}

TEST_CASE("Ring Mod / Vocoder with an external carrier matches upstream Warps bit for bit, one block late") {
  const auto voice = sine(kN * 200, 440.0, kRate);
  const auto side = sine(kN * 200, 130.0, kRate, 0.4f);
  for (float algorithm : {0.0f, 0.25f, 0.4f, 0.5f, 0.6f, 0.72f, 0.8f, 1.0f}) {
    const Settings s{algorithm, 0.3f, 0.7f, 0.9f};
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &side, with(s, kCarrierVoice, kModInput, false));
    const auto expected = reference(voice, side, s, 0, false);  // voice on CARRIER, side on MODULATOR
    INFO("algorithm " << algorithm);
    for (size_t i = 0; i < size_t(kN); ++i) REQUIRE(out.l[i] == 0.0f);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Each Carrier option drives Warps' carrier input") {
  const auto voice = sine(kN * 100, 440.0, kRate);
  const auto carrierSide = sine(kN * 100, 700.0, kRate, 0.3f);
  const auto modulatorSide = sine(kN * 100, 130.0, kRate, 0.4f);
  const std::vector<float> silent(voice.size(), 0.0f);
  const Settings s{0.375f, 0.2f, 0.6f, 0.6f};  // digital ring modulation
  struct Case {
    float carrier;
    const std::vector<float>* jack;  // what must reach CARRIER
    int carrierShape;
  } cases[] = {{kCarrierOsc, &silent, 1},       {kCarrierVoice, &voice, 0},        {kCarrierAux, &carrierSide, 0},
               {kCarrierInput, &carrierSide, 0}, {kCarrierOther, &carrierSide, 0}};
  for (const Case& c : cases) {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    engine.noteOn(57.0f, 1.0f);
    const Stereo out = run(engine, voice, voice, &carrierSide, &modulatorSide, with(s, c.carrier, kModInput, false));
    const auto expected = reference(*c.jack, modulatorSide, s, c.carrierShape, false, 57.0f);
    INFO("carrier option " << WarpsEngine::carrierIndex(c.carrier));
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Each Modulator option drives Warps' modulator input") {
  const auto voice = sine(kN * 100, 440.0, kRate);
  const auto modulatorSide = sine(kN * 100, 130.0, kRate, 0.4f);
  const Settings s{0.375f, 0.2f, 0.6f, 0.6f};
  struct Case {
    float modulator;
    const std::vector<float>* jack;  // what must reach MODULATOR
  } cases[] = {{kModVoice, &voice}, {kModAux, &modulatorSide}, {kModInput, &modulatorSide}, {kModOther, &modulatorSide}};
  for (const Case& c : cases) {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &modulatorSide, with(s, kCarrierVoice, c.modulator, false));
    const auto expected = reference(voice, *c.jack, s, 0, false);  // Voice + Voice: self-modulation
    INFO("modulator option " << WarpsEngine::modulatorIndex(c.modulator));
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Ring Mod / Vocoder's Internal Osc carrier is Warps' oscillator at the note plus Osc Pitch") {
  const auto voice = sine(kN * 120, 220.0, kRate);
  for (int mode = 0; mode < WarpsEngine::kNumOscillatorModes; ++mode) {
    for (float algorithm : {0.25f, 0.6f, 0.8f, 1.0f}) {
      const Settings s{algorithm, 0.4f, 0.8f, 0.6f};
      ParamValues p = with(s, kCarrierOsc, kModVoice, false);
      p.p[6] = 0.75f;                           // +12 st
      p.p[7] = (float(mode) + 0.5f) / 3.0f;
      WarpsEngine engine;
      engine.prepare(kRate, 256);
      engine.noteOn(57.0f, 1.0f);
      const Stereo out = run(engine, voice, voice, nullptr, nullptr, p);
      // CARRIER silent (it would be the oscillator's FM), MODULATOR = the voice, button mode + 1.
      const std::vector<float> silent(voice.size(), 0.0f);
      const auto expected = reference(silent, voice, s, 1 + mode, false, 57.0f + 12.0f);
      INFO("mode " << mode << " algorithm " << algorithm);
      for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
    }
  }
}

TEST_CASE("Ring Mod / Vocoder frequency shifter matches Warps' easter egg, internal and external carrier") {
  const auto voice = sine(kN * 200, 1000.0, kRate);
  const auto side = sine(kN * 200, 300.0, kRate, 0.4f);
  const Settings s{0.9f, 0.6f, 0.2f, 0.8f};
  const std::vector<float> silent(voice.size(), 0.0f);
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, nullptr, with(s, kCarrierOsc, kModVoice, true));
    const auto expected = reference(silent, voice, s, 1, true);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    const Stereo out = run(engine, voice, voice, nullptr, &side, with(s, kCarrierVoice, kModInput, true));
    const auto expected = reference(voice, side, s, 0, true);
    for (size_t i = 0; i + kN < out.l.size(); ++i) REQUIRE(out.l[i + kN] == expected[i]);
  }
}

TEST_CASE("Ring Mod / Vocoder digital ring modulation makes sum and difference tones") {
  const auto voice = sine(96000, 1000.0, kRate);
  const auto side = sine(96000, 300.0, kRate);
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const Stereo out = run(engine, voice, voice, nullptr, &side, with({0.375f, 0.0f, 0.5f, 0.5f}, kCarrierVoice, kModInput, false));
  const double sum = magnitudeAt(out.l, kRate, 9600, 96000, 1300.0);
  const double difference = magnitudeAt(out.l, kRate, 9600, 96000, 700.0);
  const double carrierLeak = magnitudeAt(out.l, kRate, 9600, 96000, 1000.0);
  INFO("1300 Hz " << sum << " 700 Hz " << difference << " 1000 Hz " << carrierLeak);
  REQUIRE(sum > 10.0 * carrierLeak);
  REQUIRE(difference > 10.0 * carrierLeak);
}

TEST_CASE("Ring Mod / Vocoder internal oscillator tracks the note with its offset") {
  ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierOsc, kModInput, false);  // crossfade: all carrier
  p.p[6] = 0.75f;                                                                  // +12 st
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  engine.noteOn(57.0f, 1.0f);  // A3 + 12 st = 440 Hz
  const std::vector<float> silent(96000, 0.0f);
  const Stereo out = run(engine, silent, silent, nullptr, nullptr, p);
  REQUIRE_THAT(test::estimateFrequency(out.l, kRate, 9600, 96000), Catch::Matchers::WithinRel(440.0, 0.003));
}

TEST_CASE("Ring Mod / Vocoder frequency shifter moves the voice by Warps' shift") {
  // Algorithm 0.9: 2|0.9 - 0.5| = 0.8 > 0.4, so 4 * 2^(180 * 0.4 / 12) = 256 Hz (modulator.cc:79-95).
  ParamValues p = with({0.9f, 0.98f, 0.0f, 1.0f}, kCarrierOsc, kModVoice, true);
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto voice = sine(96000, 1000.0, kRate);
  const Stereo out = run(engine, voice, voice, nullptr, nullptr, p);
  const double peak = test::peakFrequency(out.l, kRate, 9600, 96000, 200.0, 3000.0);
  INFO("peak " << peak);
  REQUIRE(std::fabs(std::fabs(peak - 1000.0) - 256.0) < 3.0);
}

TEST_CASE("Ring Mod / Vocoder with an absent modulator is silent") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto voice = sine(9600, 440.0, kRate);
  const Stereo out = run(engine, voice, voice, nullptr, nullptr, with({0.25f, 0.5f, 0.47f, 0.47f}, kCarrierVoice, kModInput, false));
  REQUIRE(test::rms(out.l, 0, out.l.size()) < 1e-4);
}

TEST_CASE("Ring Mod / Vocoder smooths its pots like the firmware") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const std::vector<float> block(kN, 0.1f);
  ParamValues p = defaults();
  p.p[0] = 0.25f;
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE(engine.smoothedParam(0) == 0.25f);  // snapped after reset
  p.p[0] = 0.75f;
  p.p[5] = kModInput;  // stepped: immediate
  p.p[6] = 1.0f;       // Osc Pitch: the note pot's 0.1
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE_THAT(engine.smoothedParam(0), Catch::Matchers::WithinAbs(0.25f + 0.0264f * 0.5f, 1e-6));
  REQUIRE(engine.smoothedParam(5) == kModInput);
  REQUIRE_THAT(engine.smoothedParam(6), Catch::Matchers::WithinAbs(0.5f + 0.1f * 0.5f, 1e-6));
  for (int i = 0; i < 400; ++i) run(engine, block, block, nullptr, nullptr, p);
  REQUIRE_THAT(engine.smoothedParam(0), Catch::Matchers::WithinAbs(0.75f, 1e-4));
  engine.reset();
  p.p[0] = 0.1f;
  run(engine, block, block, nullptr, nullptr, p);
  REQUIRE(engine.smoothedParam(0) == 0.1f);
}

TEST_CASE("Ring Mod / Vocoder runs its second modulator only for a stereo voice, fading it in") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  const auto l = sine(kN * 40, 440.0, kRate);
  const auto r = sine(kN * 40, 660.0, kRate);
  const auto m = sine(kN * 40, 110.0, kRate);
  const ParamValues p = with({0.375f, 0.0f, 0.6f, 0.6f}, kCarrierVoice, kModInput, false);
  const Stereo mono = run(engine, l, r, nullptr, &m, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo stereo = run(engine, l, r, nullptr, &m, p);
  REQUIRE(engine.rightRuns());
  // The right output starts on the left output and fades to its own instance over 10 ms.
  for (size_t i = 0; i < size_t(kN); ++i) REQUIRE(stereo.r[i] == stereo.l[i]);  // still the old block
  REQUIRE(std::fabs(stereo.r[kN] - stereo.l[kN]) < 0.01f);
  REQUIRE(stereo.r != stereo.l);
}

TEST_CASE("Ring Mod / Vocoder through the wrapper at 44.1, 48 and 96 kHz with odd host blocks") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    NativeRateWrapper wrapped(std::make_unique<WarpsEngine>(), WarpsEngine::kNativeRate, WarpsEngine::kNativeBlock);
    wrapped.prepare(rate, 256);
    if (rate == kRate) REQUIRE(wrapped.inputLatencySamples() == kN);
    ParamValues p = with({0.0f, 0.0f, 0.5f, 0.5f}, kCarrierOsc, kModInput, false);
    p.p[6] = 0.75f;
    wrapped.noteOn(57.0f, 1.0f);
    const std::vector<float> silent(size_t(rate), 0.0f);
    const Stereo out = run(wrapped, silent, silent, nullptr, nullptr, p, 37);
    for (float s : out.l) REQUIRE(std::isfinite(s));
    INFO("rate " << rate);
    REQUIRE_THAT(test::estimateFrequency(out.l, rate, size_t(rate * 0.1), size_t(rate)),
                 Catch::Matchers::WithinRel(440.0, 0.003));
  }
}

TEST_CASE("Ring Mod / Vocoder output does not depend on the host block size at 96 kHz") {
  const auto voice = sine(kN * 50, 440.0, kRate);
  const auto side = sine(kN * 50, 97.0, kRate);
  const ParamValues p = with({0.6f, 0.5f, 0.7f, 0.7f}, kCarrierAux, kModInput, false);
  NativeRateWrapper a(std::make_unique<WarpsEngine>(), kRate, kN), b(std::make_unique<WarpsEngine>(), kRate, kN);
  a.prepare(kRate, 256);
  b.prepare(kRate, 256);
  REQUIRE(run(a, voice, voice, &side, &side, p, 60).l == run(b, voice, voice, &side, &side, p, 37).l);
}

TEST_CASE("Ring Mod / Vocoder survives NaN and every algorithm stays within Warps' range") {
  const auto voice = sine(kN * 40, 440.0, kRate, 2.0f);  // driven hard
  const auto side = sine(kN * 40, 97.0, kRate, 2.0f);
  const float limit = 32768.0f / WarpsEngine::kInputScale;  // Warps' 16-bit output, read back
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  {
    WarpsEngine engine;
    engine.prepare(kRate, 256);
    for (float s : run(engine, voice, voice, &side, &side, bad).l) REQUIRE(std::isfinite(s));
  }
  for (int step = 0; step <= 32; ++step) {
    for (float carrier : {kCarrierOsc, kCarrierVoice}) {
      for (bool shifter : {false, true}) {
        WarpsEngine engine;
        engine.prepare(kRate, 256);
        engine.setStereo(true);
        const Stereo out = run(engine, voice, side, nullptr, &side, with({float(step) / 32.0f, 0.7f, 1.0f, 1.0f}, carrier, kModInput, shifter));
        for (size_t i = 0; i < out.l.size(); ++i) {
          REQUIRE(std::fabs(out.l[i]) <= limit);
          REQUIRE(std::fabs(out.r[i]) <= limit);
        }
      }
    }
  }
}

TEST_CASE("Ring Mod / Vocoder processing does not allocate") {
  WarpsEngine engine;
  engine.prepare(kRate, 256);
  std::vector<float> l(kN, 0.2f), r(kN, -0.2f), m(kN, 0.1f), ol(kN), orr(kN);
  const float* in[4] = {l.data(), r.data(), m.data(), m.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p = defaults();
  engine.process(in, out, p, kN);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 400; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[0] = float(block % 9) / 8.0f;
      p.p[4] = WarpsEngine::optionValue(block % 5);
      p.p[5] = WarpsEngine::optionValue(block % 4);
      p.p[7] = float(block % 3) / 3.0f + 0.1f;
      p.p[8] = block % 7 == 0 ? 0.75f : 0.25f;
      engine.process(in, out, p, block % 2 ? kN : 17);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/warps_engine.h` not found).

- [ ] **Step 3: Write `core/engine/warps_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the upstream Warps modulator, shown as "Ring Mod / Vocoder". Runs at Warps' rate
// and block size (96 kHz, 60 samples: warps.cc:91,97,100) behind a one-block FIFO, so any
// host block size works; the FIFO's 60 native samples are its nativeLatency().
// As on the module (owner, 2026-10-20, provisional): Carrier picks the CARRIER input, either
// Warps' internal oscillator (the module's button; at the voice's pitch plus Osc Pitch) or a
// signal: the voice, or side input in[2]. Modulator picks the MODULATOR input: the voice, or
// side input in[3].
class WarpsEngine final : public VoiceEngine {
 public:
  static constexpr double kNativeRate = 96000.0;
  static constexpr int kNativeBlock = 60;
  static constexpr int kNumAlgorithms = 9;       // warps/dsp/parameters.h:43-53
  static constexpr int kNumOscillatorModes = 3;  // the button's modes, carrier_shape 1..3
  // Carrier and Modulator are append-only option lists read in 8 fixed steps, so adding an
  // option never changes what a saved value means.
  static constexpr int kSelectorSteps = 8;
  static constexpr int kNumCarriers = 5;    // Internal Osc, Voice, Source AUX, Input, Other part
  static constexpr int kNumModulators = 4;  // Voice, Source AUX, Input, Other part
  // EG13 ±1.0 = ±5 V into Warps' ±8 V inputs, as VCV converts (Warps.cpp:102-103): 10240 counts.
  // Outputs are read back with the same scale, so a unity path through Warps is unity in EG13.
  static constexpr float kInputScale = 5.0f / 16.0f * 32768.0f;
  // Carrier Level and Mod Level default: unity gain, unsaturated, through Warps' amplifier for a
  // full-scale voice (measured; see test_warps_engine.cpp). Owner decision 2026-10-20.
  static constexpr float kDefaultLevel = 0.47f;

  WarpsEngine();
  ~WarpsEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: memset + Init, no allocation
  void noteOn(float note, float) override { note_ = note; }
  void noteOff() override {}
  void setNote(float note) override { note_ = note; }
  void setStereo(bool stereo) override { stereo_ = stereo; }
  int nativeLatency() const override { return kNativeBlock; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static int carrierIndex(float normalized);    // P5 -> 0..4 (0 = Internal Osc)
  static int modulatorIndex(float normalized);  // P6 -> 0..3 (0 = Voice)
  static float optionValue(int index);          // the centre of option index's step
  static float oscillatorOffset(float normalized);  // P7: -24..+24 semitones
  static int oscillatorMode(float normalized);      // P8 thirds: 0..2 (carrier_shape 1..3)
  static bool shifterMode(float normalized);        // P9 halves: modulation / frequency shifter
  static short toShort(float x);                    // EG13 sample -> Warps' 16-bit input
  static float fromShort(short x);                  // Warps' 16-bit output -> EG13 sample
  float smoothedParam(int index) const;             // test hook: pot-filtered P1..P9
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  void renderBlock(const ParamValues& p);

  std::unique_ptr<Impl> impl_;
  float note_ = 60.0f;
  bool stereo_ = false;
  int fill_ = 0;      // position in the block being collected (and the block being played)
  PotSmoother pots_;  // CvScaler pot smoothing, once per native block
  StereoLink link_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/warps_engine.cpp`**

```cpp
#include "engine/warps_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/param_format.h"
#include "warps/dsp/modulator.h"

namespace eg13 {

namespace {

// Warps' algorithms in knob order (warps/dsp/parameters.h:43-53).
const char* const kAlgorithmNames[WarpsEngine::kNumAlgorithms] = {
    "Crossfade", "Fold", "Analog RM", "Digital RM", "XOR", "Compare", "Spectral", "Morph", "Vocoder"};
// Append-only (see kSelectorSteps): never reorder, only add at the end.
const char* const kCarrierNames[WarpsEngine::kNumCarriers] = {"Internal Osc", "Voice", "Source AUX", "Input",
                                                              "Other part"};
const char* const kModulatorNames[WarpsEngine::kNumModulators] = {"Voice", "Source AUX", "Input", "Other part"};
// The button's three modes name the oscillator's waveform in the cross-modulation and vocoder
// regions (carrier_shape - 1 and + 1, modulator.cc:233-237; OscillatorShape, parameters.h:35-41).
const char* const kModeShapeNames[WarpsEngine::kNumOscillatorModes] = {"Sine / Saw", "Triangle / Pulse",
                                                                      "Saw / Noise"};
const char* const kModeNames[2] = {"Modulation", "Freq shifter"};

// The knob crossfades between adjacent algorithms (modulator.cc:292-311), so the text names the
// algorithm, or the pair it is between and how far.
void formatAlgorithm(float v, char* buf, size_t size) {
  const float position = clamp01(v) * 8.0f;
  const int low = std::min(int(position), 8);
  const float frac = position - float(low);
  if (low == 8 || frac < 0.05f) {
    std::snprintf(buf, size, "%s", kAlgorithmNames[low]);
  } else if (frac > 0.95f) {
    std::snprintf(buf, size, "%s", kAlgorithmNames[low + 1]);
  } else {
    std::snprintf(buf, size, "%s > %s %d%%", kAlgorithmNames[low], kAlgorithmNames[low + 1],
                  int(std::lround(frac * 100.0f)));
  }
}
void formatCarrier(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kCarrierNames[WarpsEngine::carrierIndex(v)]);
}
void formatModulator(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModulatorNames[WarpsEngine::modulatorIndex(v)]);
}
void formatPitch(float v, char* buf, size_t size) { formatSemitones(WarpsEngine::oscillatorOffset(v), buf, size); }
void formatShape(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeShapeNames[WarpsEngine::oscillatorMode(v)]);
}
void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[WarpsEngine::shifterMode(v) ? 1 : 0]);
}

// Defaults: Timbre 0.5 as VCV (Warps.cpp:44). Algorithm on the analog ring modulator (2/8), the
// engine's namesake (VCV starts on crossfade, Warps.cpp:43). Levels at the measured unity point
// (VCV 1.0, Warps.cpp:46-47). Carrier Internal Osc and Modulator Voice: the common hardware patch,
// a signal into MODULATOR with the internal oscillator on (owner, 2026-10-20, provisional). These
// defaults live only here: changing one is a one-line edit that affects new patches only.
const EngineParamInfo kParams[9] = {
    {"Algorithm", 2.0f / 8.0f, formatAlgorithm},
    {"Timbre", 0.5f, formatPercent01},
    {"Carrier Level", WarpsEngine::kDefaultLevel, formatPercent01},
    {"Mod Level", WarpsEngine::kDefaultLevel, formatPercent01},
    {"Carrier", 0.5f / float(WarpsEngine::kSelectorSteps), formatCarrier},
    {"Modulator", 0.5f / float(WarpsEngine::kSelectorSteps), formatModulator},
    {"Osc Pitch", 0.5f, formatPitch},
    {"Osc Shape", 0.5f / 3.0f, formatShape},
    {"Mode", 0.25f, formatMode},
};

// Side input 0 feeds CARRIER, side input 1 MODULATOR. Internal Osc needs no signal (None) and
// Voice is the engine's own input; the Voice delivers the rest (Source AUX, Input, Other part).
SideSource warpsSideSource(int input, const ParamValues& p) {
  if (input == 0) {
    const int carrier = WarpsEngine::carrierIndex(p.p[4]);
    return carrier == 0 ? SideSource::None : SideSource(carrier - 1);
  }
  return SideSource(WarpsEngine::modulatorIndex(p.p[5]));
}

const EngineInfo kInfo = {"warps", "Ring Mod / Vocoder", 9, kParams, true, false, 2, warpsSideSource};

// Firmware pot smoothing, once per 60-sample block: CvScaler::Read runs once per audio block
// (warps.cc:63-70) and BIND low-passes each pot by 0.33 * 0.08 (cv_scaler.cc:65-75, 125-128).
// Osc Pitch follows the LEVEL 1 pot's note filter, 0.1 (cv_scaler.cc:159-160). Carrier,
// Modulator, Osc Shape and Mode are stepped (the button, ui.cc). Within each block Warps interpolates
// its own parameters (ParameterInterpolator and ProcessXmod, modulator.cc:157-201).
const float kPotLp[9] = {0.0264f, 0.0264f, 0.0264f, 0.0264f, 1.0f, 1.0f, 0.1f, 1.0f, 1.0f};

void initModulator(warps::Modulator& m) {
  std::memset(static_cast<void*>(&m), 0, sizeof(m));  // Init leaves members unset (Warps.cpp:61)
  m.Init(float(WarpsEngine::kNativeRate));
}

}  // namespace

struct WarpsEngine::Impl {
  warps::Modulator modulator[2];  // left, right (the right runs only while stereo)
  warps::ShortFrame in[kNativeBlock];
  warps::ShortFrame out[kNativeBlock];
  float pending[4][kNativeBlock];  // the block being collected: voice L, voice R, carrier side, modulator side
  float ready[2][kNativeBlock];    // the last processed block, played while the next fills
};

WarpsEngine::WarpsEngine()
    : impl_(std::make_unique<Impl>()), pots_(kPotLp, kInfo.paramCount, kNativeBlock) {
  initModulator(impl_->modulator[1]);
  reset();
}

WarpsEngine::~WarpsEngine() = default;

const EngineInfo& WarpsEngine::staticInfo() { return kInfo; }

const EngineInfo& WarpsEngine::info() const { return kInfo; }

float WarpsEngine::oscillatorOffset(float normalized) { return (clamp01(normalized) - 0.5f) * 48.0f; }

int WarpsEngine::oscillatorMode(float normalized) { return stepIndex(normalized, kNumOscillatorModes); }

int WarpsEngine::carrierIndex(float normalized) {
  return std::min(stepIndex(normalized, kSelectorSteps), kNumCarriers - 1);
}

int WarpsEngine::modulatorIndex(float normalized) {
  return std::min(stepIndex(normalized, kSelectorSteps), kNumModulators - 1);
}

float WarpsEngine::optionValue(int index) { return (float(index) + 0.5f) / float(kSelectorSteps); }

bool WarpsEngine::shifterMode(float normalized) { return clamp01(normalized) >= 0.5f; }

short WarpsEngine::toShort(float x) {
  const float v = x * kInputScale;
  if (v != v) return 0;  // NaN
  if (v <= -32768.0f) return -32768;
  if (v >= 32767.0f) return 32767;
  return short(int(v));  // truncation, like VCV's (int) cast (Warps.cpp:102-103)
}

float WarpsEngine::fromShort(short x) { return float(x) / kInputScale; }

float WarpsEngine::smoothedParam(int index) const {
  return index >= 0 && index < kInfo.paramCount ? pots_[index] : 0.0f;
}

void WarpsEngine::prepare(double, int) {
  link_.prepare(kNativeRate);  // the engine always runs at 96 kHz (in or out of the wrapper)
  reset();
}

void WarpsEngine::reset() {
  Impl& s = *impl_;
  initModulator(s.modulator[0]);  // modulator[1] is initialised whenever stereo starts
  std::memset(s.pending, 0, sizeof(s.pending));
  std::memset(s.ready, 0, sizeof(s.ready));
  fill_ = 0;
  pots_.reset();
  link_.reset();
}

void WarpsEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  for (int i = 0; i < n; ++i) {
    s.pending[0][fill_] = in != nullptr ? in[0][i] : 0.0f;
    s.pending[1][fill_] = in != nullptr ? in[1][i] : 0.0f;
    s.pending[2][fill_] = in != nullptr && in[2] != nullptr ? in[2][i] : 0.0f;
    s.pending[3][fill_] = in != nullptr && in[3] != nullptr ? in[3][i] : 0.0f;
    out[0][i] = s.ready[0][fill_];
    out[1][i] = s.ready[1][fill_];
    if (++fill_ == kNativeBlock) {
      renderBlock(p);
      fill_ = 0;
    }
  }
}

void WarpsEngine::renderBlock(const ParamValues& p) {
  Impl& s = *impl_;
  pots_.update(p, kNativeBlock);
  const bool shifter = shifterMode(p.p[8]);
  const int carrier = carrierIndex(p.p[4]);
  const bool internal = carrier == 0;
  const bool voiceCarries = carrier == 1;
  const bool voiceModulates = modulatorIndex(p.p[5]) == 0;
  // Warps' modulator keeps pointers into itself (filter_bank.h:70, sample_rate_converter.h:279),
  // so its state cannot be copied: the right instance starts fresh and fades in.
  if (link_.request(stereo_, false) == StereoLink::Start::ResetRight) initModulator(s.modulator[1]);
  const int instances = link_.rightRuns() ? 2 : 1;
  for (int c = 0; c < instances; ++c) {
    warps::Modulator& m = s.modulator[c];
    warps::Parameters* w = m.mutable_parameters();
    // VCV's mapping (Warps.cpp:78-95): each knob straight to its parameter.
    w->channel_drive[0] = pots_[2];  // no effect with the oscillator on (modulator.cc:215)
    w->channel_drive[1] = pots_[3];
    w->modulation_algorithm = pots_[0];
    w->modulation_parameter = pots_[1];
    w->frequency_shift_pot = pots_[0];
    w->frequency_shift_cv = 0.0f;
    w->phase_shift = pots_[0];
    // The internal oscillator's pitch: the module takes it from the LEVEL 1 pot and V/OCT
    // (cv_scaler.cc:149-173); EG13 plays it at the voice's pitch plus Osc Pitch. Warps runs at
    // its own 96 kHz here, so no rate correction (VCV adds one at other rates, Warps.cpp:97).
    w->note = note_ + oscillatorOffset(pots_[6]);
    // The button: 0 takes the carrier from the CARRIER input; 1..3 replace it with the internal
    // oscillator in one of three modes (modulator.cc:209-286; Warps.cpp:68-69). In the easter egg
    // a non-zero value selects its quadrature oscillator (modulator.cc:78-98).
    w->carrier_shape = internal ? 1 + oscillatorMode(p.p[7]) : 0;  // only Internal Osc uses P7, P8
    m.set_easter_egg(shifter);
    for (int i = 0; i < kNativeBlock; ++i) {
      const float voice = s.pending[c][i];
      // CARRIER: this channel's voice or the carrier side signal, with the oscillator off. With
      // it on, the module reads this jack as the oscillator's FM (modulator.cc:228-231) and, in
      // the easter egg, adds it to the shifted signal (:143-145); v1 has no audio-rate FM (spec
      // §6), so it is silent then.
      s.in[i].l = internal ? short(0) : toShort(voiceCarries ? voice : s.pending[2][i]);
      // MODULATOR: this channel's voice, or the modulator side signal.
      s.in[i].r = toShort(voiceModulates ? voice : s.pending[3][i]);
    }
    m.Process(s.in, s.out, kNativeBlock);
    for (int i = 0; i < kNativeBlock; ++i) s.ready[c][i] = fromShort(s.out[i].l);
  }
  link_.mix(s.ready[0], s.ready[1], s.ready[1], kNativeBlock);
}

}  // namespace eg13
```

Add `engine/warps_engine.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 5: Document the Ring Mod / Vocoder in the manual**

Append to `docs/manual/engines.md`:

````markdown
## Shapers

Every voice has a Shaper slot after its Filter. **Off** passes the sound through unchanged.

### Ring Mod / Vocoder (based on Warps)

Warps combines two signals, a **carrier** and a **modulator**, in one of its algorithms. Like the module it has two inputs, CARRIER and MODULATOR, and its internal oscillator can stand in for the carrier. EG13 lets you choose what feeds each input. The routing is provisional; it may be revisited once more patches are made, and if options are added, saved patches keep their meaning.

- **Carrier**:
  - *Internal Osc* (default): Warps' own oscillator, playing the voice's pitch plus **Osc Pitch** (±24 semitones). **Osc Shape** picks the module's three oscillator modes. Warps chooses the waveform itself for each algorithm region: *Sine / Saw*, *Triangle / Pulse* or *Saw / Noise*, where the first is used for the cross-modulations and the second for the vocoder. Osc Pitch and Osc Shape only matter with this setting. As on the module, **Carrier Level** has no effect while the internal oscillator plays.
  - *Voice*: the voice itself, meaning the sound reaching the Shaper from this voice's Source and Filter.
  - *Source AUX*: the Source's second output: the Macro Oscillator's AUX, the Resonator's even partials. Other sources give their main output.
  - *Input*: the part's Input, as its Input source setting routes it.
  - *Other part*: the other part's output.
- **Modulator**: *Voice* (default), *Source AUX*, *Input* or *Other part*, with the same meanings. Voice into the modulator with the internal oscillator as carrier is the classic way to play Warps; for example, the vocoder then puts the voice's formants on the oscillator. Voice into both inputs is allowed: it is self-modulation.
- If a chosen signal isn't there (no sidechain, a silent part), that input is silent.
- **Algorithm** is Warps' big knob, and it is continuous: *Crossfade*, *Fold*, *Analog RM*, *Digital RM*, *XOR*, *Compare*, *Spectral*, *Morph* and *Vocoder*. Between two positions it blends the neighbouring algorithms, and the display names both, for example "Fold > Analog RM 40%". The default is *Analog RM*. **Timbre** is the algorithm's own control.
- **Carrier Level** and **Mod Level** drive Warps' input amplifiers. Their default, 47 %, passes a full-scale voice at unity gain without saturating. Turn them up for the module's overdriven, squarer character (at 100 % the amplifier adds about 12 dB and heavy distortion).
- **Mode** *Freq shifter* is the module's hidden frequency-shifter mode.
  - With Carrier = Internal Osc, it shifts the Modulator signal (the voice, by default) by an amount set by Algorithm, from no shift at the centre to several hundred Hz up or down at the ends. Timbre mixes the upper and lower sidebands, Carrier Level sets feedback and Mod Level the dry/wet mix.
  - With any other Carrier, it multiplies the carrier and modulator into a single sideband instead.
- Warps runs at 96 kHz in blocks of 60 samples. That adds to EG13's fixed latency ([Latency](latency.md)).
- Stereo: while the voice is stereo, a second Warps runs for the right channel. It cannot copy the left one's state, so it starts fresh and fades in over 10 ms. The Source AUX, Input and Other part signals are mono (left and right summed).
````

Append to `docs/manual/smoothing.md`:

````markdown
- **Ring Mod / Vocoder:** Warps' own knob filter, once per 60-sample block. Algorithm, Timbre and both Levels have a time constant of about 25 ms; Osc Pitch about 6 ms. Within each block Warps glides its parameters itself. Carrier, Modulator, Osc Shape and Mode switch at once.
````

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Ring Mod|Default levels"`
Expected: all pass. Then the full suite and the Docker command. If a bit-for-bit test fails, report the case, first differing index and both values as BLOCKED.

- [ ] **Step 7: Commit**

```bash
git add core/engine/warps_engine.h core/engine/warps_engine.cpp core/CMakeLists.txt tests/test_warps_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Ring Mod / Vocoder: Warps at 96 kHz with Carrier and Modulator selectors and the frequency shifter"
```

---
### Task 9: EQ (VCV Shelves model)

**Files:**
- Manual: `docs/manual/engines.md`, `docs/manual/smoothing.md` (same commit)
- Create: `core/engine/shelves_engine.h`, `core/engine/shelves_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/shelves_engine.cpp`)
- Test: `tests/test_shelves_engine.cpp`

**Interfaces:**
- Consumes: `ParamRamp`, `StereoLink`, `knobHz`, `stepIndex`, formatters (Task 3), `eg13_vcv` (Task 1).
- Produces: `class ShelvesEngine final : public VoiceEngine` ("EQ", id `"shelves"`, host rate, `usesInput`): `static constexpr float kVolts = 5.0f; static constexpr int kNumOutputs = 7; static const EngineInfo& staticInfo(); static float gainDb(float); static float qFactor(float); static int outputIndex(float); static bool padOn(float); float smoothedParam(int) const; bool rightRuns() const;`. P1 High Freq, P2 High Gain, P3 Mid 1 Freq, P4 Mid 1 Gain, P5 Mid 1 Q, P6 Mid 2 Freq, P7 Mid 2 Gain, P8 Mid 2 Q, P9 Low Freq, P10 Low Gain (VCV's parameter order, `Shelves.cpp:17-26`), P11 Input Pad (Off / −6 dB), P12 Output (EQ, Mid 1 LP/BP/HP, Mid 2 LP/BP/HP).

- [ ] **Step 1: Write the failing tests (`tests/test_shelves_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Shelves/shelves.hpp"
#include "alloc_counter.h"
#include "engine/param_format.h"
#include "engine/shelves_engine.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(ShelvesEngine::staticInfo()); }

std::vector<float> sine(size_t n, double hz, double rate, float amp = 0.25f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / rate));
  return x;
}

struct Stereo {
  std::vector<float> l, r;
};

Stereo render(ShelvesEngine& e, const std::vector<float>& inL, const std::vector<float>& inR, const ParamValues& p) {
  Stereo s{std::vector<float>(inL.size()), std::vector<float>(inL.size())};
  for (size_t done = 0; done < inL.size(); done += 16) {
    const int count = int(std::min<size_t>(16, inL.size() - done));
    const float* in[2] = {inL.data() + done, inR.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    e.process(in, out, p, count);
  }
  return s;
}

double gainDbAt(double hz, const ParamValues& p) {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(24000, hz, 48000.0);
  const Stereo out = render(engine, in, in, p);
  return 20.0 * std::log10(test::rms(out.l, 4800, 24000) / test::rms(in, 4800, 24000));
}

// The model's frame for constant EG13 parameters (VCV's knob mapping, Shelves.cpp:125-136).
shelves::ShelvesEngine::Frame frameFor(const ParamValues& p) {
  shelves::ShelvesEngine::Frame f = {};
  f.hs_freq_knob = p.p[0];
  f.hs_gain_knob = 2.0f * p.p[1] - 1.0f;
  f.p1_freq_knob = p.p[2];
  f.p1_gain_knob = 2.0f * p.p[3] - 1.0f;
  f.p1_q_knob = p.p[4];
  f.p2_freq_knob = p.p[5];
  f.p2_gain_knob = 2.0f * p.p[6] - 1.0f;
  f.p2_q_knob = p.p[7];
  f.ls_freq_knob = p.p[8];
  f.ls_gain_knob = 2.0f * p.p[9] - 1.0f;
  f.pre_gain = ShelvesEngine::padOn(p.p[10]);
  const bool mid2 = ShelvesEngine::outputIndex(p.p[11]) >= 4;
  f.p2_lp_out_connected = f.p2_bp_out_connected = f.p2_hp_out_connected = mid2;
  return f;
}

// The adapter is built with Rack's own -funsafe-math-optimizations (Rack compile.mk:17), this
// test without it, so they agree to 1e-5 rather than bit for bit (owner, 2026-10-20). Planning
// measured at most 5.6e-6.
bool nearModel(float actual, float expected) {
  return std::fabs(actual - expected) <= 1e-5f * std::max(1.0f, std::fabs(expected));
}

float tap(const shelves::ShelvesEngine::Frame& f, int output) {
  switch (output) {
    case 1: return f.p1_lp_out;
    case 2: return f.p1_bp_out;
    case 3: return f.p1_hp_out;
    case 4: return f.p2_lp_out;
    case 5: return f.p2_bp_out;
    case 6: return f.p2_hp_out;
    default: return f.main_out;
  }
}

}  // namespace

TEST_CASE("EQ describes itself") {
  const auto& info = ShelvesEngine::staticInfo();
  REQUIRE(std::string(info.id) == "shelves");
  REQUIRE(std::string(info.displayName) == "EQ");
  REQUIRE(info.paramCount == 12);
  REQUIRE(info.usesInput);
  const char* const names[12] = {"High Freq", "High Gain", "Mid 1 Freq", "Mid 1 Gain", "Mid 1 Q", "Mid 2 Freq",
                                 "Mid 2 Gain", "Mid 2 Q", "Low Freq", "Low Gain", "Input Pad", "Output"};
  for (int i = 0; i < 12; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  for (int i : {1, 3, 6, 9}) REQUIRE(ShelvesEngine::gainDb(info.params[i].defaultValue) == 0.0f);
  REQUIRE_FALSE(ShelvesEngine::padOn(info.params[10].defaultValue));
  REQUIRE(ShelvesEngine::outputIndex(info.params[11].defaultValue) == 0);
  REQUIRE(ShelvesEngine::qFactor(0.0f) == 0.5f);
  REQUIRE(std::fabs(ShelvesEngine::qFactor(1.0f) - 40.0f) < 1e-3f);
  char buf[32];
  info.params[1].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+18.0 dB");
  info.params[11].format(2.5f / 7.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Mid 1 BP");
  info.params[10].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-6 dB");
}

TEST_CASE("EQ matches the VCV Shelves model to 1e-5") {
  for (int output = 0; output < ShelvesEngine::kNumOutputs; ++output) {
    for (float pad : {0.25f, 0.75f}) {
      ParamValues p = defaults();
      p.p[0] = 0.7f;
      p.p[1] = 0.2f;
      p.p[2] = 0.4f;
      p.p[3] = 0.8f;
      p.p[4] = 0.6f;
      p.p[5] = 0.6f;
      p.p[6] = 0.3f;
      p.p[7] = 0.3f;
      p.p[8] = 0.3f;
      p.p[9] = 0.9f;
      p.p[10] = pad;
      p.p[11] = (float(output) + 0.5f) / 7.0f;
      ShelvesEngine engine;
      engine.prepare(48000.0, 256);
      const auto in = sine(4800, 220.0, 48000.0, 0.5f);
      const Stereo out = render(engine, in, in, p);
      shelves::ShelvesEngine ref;
      ref.setSampleRate(48000.f);
      shelves::ShelvesEngine::Frame f = frameFor(p);
      INFO("output " << output << " pad " << pad);
      for (size_t i = 0; i < in.size(); ++i) {
        f.main_in = in[i] * ShelvesEngine::kVolts;
        ref.process(f);
        REQUIRE(nearModel(out.l[i], tap(f, output) / ShelvesEngine::kVolts));
        REQUIRE(out.r[i] == out.l[i]);
      }
    }
  }
}

TEST_CASE("EQ ramps its knobs across each block and snaps after reset") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(48, 1000.0, 48000.0, 0.5f);
  ParamValues a = defaults();
  ParamValues b = a;
  b.p[1] = 0.9f;  // High Gain
  b.p[2] = 0.7f;  // Mid 1 Freq
  std::vector<float> out(48), right(48);
  for (int block = 0; block < 3; ++block) {
    const float* i2[2] = {in.data() + block * 16, in.data() + block * 16};
    float* o2[2] = {out.data() + block * 16, right.data() + block * 16};
    engine.process(i2, o2, block == 0 ? a : b, 16);
  }
  REQUIRE(engine.smoothedParam(1) == 0.9f);
  shelves::ShelvesEngine ref;
  ref.setSampleRate(48000.f);
  shelves::ShelvesEngine::Frame f = frameFor(a);
  const float dg = (0.9f - 0.5f) / 16.0f, df = (0.7f - 0.5f) / 16.0f;
  for (int i = 0; i < 48; ++i) {
    float gain = 0.9f, freq = 0.7f;
    if (i < 16) {
      gain = 0.5f;
      freq = 0.5f;
    } else if (i < 31) {
      gain = 0.5f + dg * float(i - 15);
      freq = 0.5f + df * float(i - 15);
    }
    f.hs_gain_knob = 2.0f * gain - 1.0f;
    f.p1_freq_knob = freq;
    f.main_in = in[size_t(i)] * ShelvesEngine::kVolts;
    ref.process(f);
    REQUIRE(nearModel(out[size_t(i)], f.main_out / ShelvesEngine::kVolts));
  }
  engine.reset();
  const float* i2[2] = {in.data(), in.data()};
  float* o2[2] = {out.data(), right.data()};
  engine.process(i2, o2, b, 16);
  shelves::ShelvesEngine fresh;
  fresh.setSampleRate(48000.f);
  shelves::ShelvesEngine::Frame g = frameFor(b);
  for (int i = 0; i < 16; ++i) {
    g.main_in = in[size_t(i)] * ShelvesEngine::kVolts;
    fresh.process(g);
    REQUIRE(nearModel(out[size_t(i)], g.main_out / ShelvesEngine::kVolts));
  }
}

TEST_CASE("EQ at its defaults is flat, in phase, and the pad lowers it by 6 dB") {
  const ParamValues p = defaults();
  for (double hz : {50.0, 1000.0, 8000.0}) REQUIRE(std::fabs(gainDbAt(hz, p)) < 0.2);
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  const auto in = sine(4800, 1000.0, 48000.0);
  const Stereo out = render(engine, in, in, p);
  double dot = 0.0;
  for (size_t i = 480; i < in.size(); ++i) dot += double(in[i]) * double(out.l[i]);
  REQUIRE(dot > 0.0);
  ParamValues pad = p;
  pad.p[10] = 0.75f;
  REQUIRE(std::fabs(gainDbAt(1000.0, pad) + 6.02) < 0.2);
}

TEST_CASE("EQ shelves and bells move the right bands") {
  ParamValues low = defaults();
  low.p[9] = 1.0f;  // Low Gain +18 dB, corner ~632 Hz
  REQUIRE(std::fabs(gainDbAt(40.0, low) - 18.0) < 1.5);
  REQUIRE(std::fabs(gainDbAt(10000.0, low)) < 2.0);  // the shelf's tail: +1.7 dB measured during planning
  ParamValues high = defaults();
  high.p[1] = 0.0f;  // High Gain -18 dB
  REQUIRE(std::fabs(gainDbAt(15000.0, high) + 18.0) < 1.5);
  REQUIRE(std::fabs(gainDbAt(40.0, high)) < 1.0);
  ParamValues bell = defaults();
  bell.p[3] = 1.0f;  // Mid 1 +18 dB at ~632 Hz
  REQUIRE(gainDbAt(knobHz(0.5f), bell) > 15.0);
  REQUIRE(std::fabs(gainDbAt(40.0, bell)) < 2.0);
}

TEST_CASE("EQ filter-tap outputs pass their bands") {
  ParamValues p = defaults();
  p.p[11] = 1.5f / 7.0f;  // Mid 1 LP
  REQUIRE(gainDbAt(60.0, p) > gainDbAt(8000.0, p) + 30.0);
  p.p[11] = 3.5f / 7.0f;  // Mid 1 HP
  REQUIRE(gainDbAt(8000.0, p) > gainDbAt(60.0, p) + 30.0);
  p.p[11] = 5.5f / 7.0f;  // Mid 2 BP
  REQUIRE(gainDbAt(knobHz(0.5f), p) > gainDbAt(60.0, p) + 20.0);
}

TEST_CASE("EQ runs its right instance only while stereo, starting seamlessly") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues p = defaults();
  p.p[3] = 0.9f;
  const auto x = sine(4800, 440.0, 48000.0);
  const auto y = sine(4800, 660.0, 48000.0);
  const Stereo mono = render(engine, x, y, p);
  REQUIRE_FALSE(engine.rightRuns());
  REQUIRE(mono.l == mono.r);
  engine.setStereo(true);
  const Stereo same = render(engine, x, x, p);
  REQUIRE(engine.rightRuns());
  REQUIRE(same.l == same.r);
  const Stereo apart = render(engine, x, y, p);
  REQUIRE(apart.l != apart.r);
}

TEST_CASE("EQ survives NaN and processing does not allocate") {
  ShelvesEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  const auto x = sine(480, 440.0, 48000.0);
  for (float s : render(engine, x, x, bad).l) REQUIRE(std::isfinite(s));
  ParamValues p = defaults();
  std::vector<float> l(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), l.data()};
  float* o[2] = {ol.data(), orr.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 300; ++block) {
      engine.setStereo(block % 100 < 50);
      p.p[1] = float(block % 10) / 10.0f;
      p.p[11] = float(block % 7) / 7.0f;
      engine.process(in, o, p, 16);
    }
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/shelves_engine.h` not found).

- [ ] **Step 3: Write `core/engine/shelves_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/pot_smoother.h"
#include "engine/stereo_link.h"
#include "engine/voice_engine.h"

namespace eg13 {

// Adapter for the VCV Shelves model (AudibleInstruments src/Shelves, GPL-3.0-or-later), shown as
// "EQ". Runs at the host rate (the model oversamples internally).
class ShelvesEngine final : public VoiceEngine {
 public:
  static constexpr float kVolts = 5.0f;  // EG13 ±1.0 = ±5 V, VCV's audio level
  static constexpr int kNumOutputs = 7;  // EQ, Mid 1 LP/BP/HP, Mid 2 LP/BP/HP

  ShelvesEngine();
  ~ShelvesEngine() override;

  void prepare(double hostRate, int maxBlock) override;
  void reset() override;  // audio-thread safe: no allocation
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void setStereo(bool stereo) override { stereo_ = stereo; }
  void process(const float* const* in, float* const* out, const ParamValues& p,
               int n) override;
  const EngineInfo& info() const override;

  static const EngineInfo& staticInfo();
  static float gainDb(float normalized);   // -18..+18 dB (shelves.hpp:30)
  static float qFactor(float normalized);  // 0.5..40, exponential (shelves.hpp:31-32)
  static int outputIndex(float normalized);
  static bool padOn(float normalized);
  float smoothedParam(int index) const;  // test hook: P1..P10 where the last block ended
  bool rightRuns() const { return link_.rightRuns(); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  double rate_ = 48000.0;
  bool stereo_ = false;
  ParamRamp ramps_[10];
  StereoLink link_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/shelves_engine.cpp`**

```cpp
#include "engine/shelves_engine.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "Shelves/shelves.hpp"
#include "engine/param_format.h"

namespace eg13 {

namespace {

const char* const kOutputNames[ShelvesEngine::kNumOutputs] = {
    "EQ", "Mid 1 LP", "Mid 1 BP", "Mid 1 HP", "Mid 2 LP", "Mid 2 BP", "Mid 2 HP"};

void formatFreq(float v, char* buf, size_t size) { formatHz(knobHz(v), buf, size); }
void formatGain(float v, char* buf, size_t size) { formatDb(ShelvesEngine::gainDb(v), buf, size); }
void formatQ(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "Q %.2f", double(ShelvesEngine::qFactor(v)));
}
void formatPad(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", ShelvesEngine::padOn(v) ? "-6 dB" : "Off");
}
void formatOutput(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kOutputNames[ShelvesEngine::outputIndex(v)]);
}

// VCV's parameters in VCV's order (Shelves.cpp:17-26) and defaults: frequencies at the middle
// of their log range (Shelves.cpp:7,66-69), gains 0 dB (Shelves.cpp:71-74), Qs at the middle
// (Shelves.cpp:12,76-77), pre-gain off (Shelves.cpp:107). Output picks the main output or one
// of the six filter taps (Shelves.cpp:45-53).
const EngineParamInfo kParams[12] = {
    {"High Freq", 0.5f, formatFreq},  {"High Gain", 0.5f, formatGain}, {"Mid 1 Freq", 0.5f, formatFreq},
    {"Mid 1 Gain", 0.5f, formatGain}, {"Mid 1 Q", 0.5f, formatQ},      {"Mid 2 Freq", 0.5f, formatFreq},
    {"Mid 2 Gain", 0.5f, formatGain}, {"Mid 2 Q", 0.5f, formatQ},      {"Low Freq", 0.5f, formatFreq},
    {"Low Gain", 0.5f, formatGain},   {"Input Pad", 0.25f, formatPad}, {"Output", 0.5f / 7.0f, formatOutput},
};

const EngineInfo kInfo = {"shelves", "EQ", 12, kParams, true, false, 0, nullptr};

float delivered(const shelves::ShelvesEngine::Frame& f, int output) {
  switch (output) {
    case 1: return f.p1_lp_out / ShelvesEngine::kVolts;
    case 2: return f.p1_bp_out / ShelvesEngine::kVolts;
    case 3: return f.p1_hp_out / ShelvesEngine::kVolts;
    case 4: return f.p2_lp_out / ShelvesEngine::kVolts;
    case 5: return f.p2_bp_out / ShelvesEngine::kVolts;
    case 6: return f.p2_hp_out / ShelvesEngine::kVolts;
    default: return f.main_out / ShelvesEngine::kVolts;
  }
}

}  // namespace

struct ShelvesEngine::Impl {
  shelves::ShelvesEngine left;
  shelves::ShelvesEngine right;
  std::vector<float> rightOut;
};

ShelvesEngine::ShelvesEngine() : impl_(std::make_unique<Impl>()) {}

ShelvesEngine::~ShelvesEngine() = default;

const EngineInfo& ShelvesEngine::staticInfo() { return kInfo; }

const EngineInfo& ShelvesEngine::info() const { return kInfo; }

float ShelvesEngine::gainDb(float normalized) { return (2.0f * clamp01(normalized) - 1.0f) * 18.0f; }

float ShelvesEngine::qFactor(float normalized) { return 0.5f * std::pow(80.0f, clamp01(normalized)); }

int ShelvesEngine::outputIndex(float normalized) { return stepIndex(normalized, kNumOutputs); }

bool ShelvesEngine::padOn(float normalized) { return clamp01(normalized) >= 0.5f; }

float ShelvesEngine::smoothedParam(int index) const {
  return index >= 0 && index < 10 ? ramps_[index].value() : 0.0f;
}

void ShelvesEngine::prepare(double hostRate, int maxBlock) {
  rate_ = hostRate;
  impl_->rightOut.assign(size_t(maxBlock), 0.0f);
  link_.prepare(hostRate);
  reset();
}

void ShelvesEngine::reset() {
  impl_->left = shelves::ShelvesEngine();
  impl_->left.setSampleRate(float(rate_));
  impl_->right = impl_->left;
  for (auto& ramp : ramps_) ramp.reset();
  link_.reset();
}

void ShelvesEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  // The VCV module reads its knobs every sample (Shelves.cpp:125-136); the model low-passes
  // frequency and Q like the hardware's CV amplifiers (shelves.hpp:295-301). EG13's parameters
  // change at block boundaries, so they ramp across each block.
  for (int k = 0; k < 10; ++k) ramps_[k].begin(clamp01(p.p[k]), n);
  const int output = outputIndex(p.p[11]);
  if (link_.request(stereo_, true) == StereoLink::Start::CopyLeft) s.right = s.left;
  const bool right = link_.rightRuns();
  shelves::ShelvesEngine::Frame frame = {};  // CV inputs unpatched
  frame.pre_gain = padOn(p.p[10]);
  // Mid 2's taps are only computed while one of them is patched (shelves.hpp:417-420,469-474).
  frame.p2_lp_out_connected = frame.p2_bp_out_connected = frame.p2_hp_out_connected = output >= 4;
  for (int i = 0; i < n; ++i) {
    frame.hs_freq_knob = ramps_[0].at(i);
    frame.hs_gain_knob = 2.0f * ramps_[1].at(i) - 1.0f;
    frame.p1_freq_knob = ramps_[2].at(i);
    frame.p1_gain_knob = 2.0f * ramps_[3].at(i) - 1.0f;
    frame.p1_q_knob = ramps_[4].at(i);
    frame.p2_freq_knob = ramps_[5].at(i);
    frame.p2_gain_knob = 2.0f * ramps_[6].at(i) - 1.0f;
    frame.p2_q_knob = ramps_[7].at(i);
    frame.ls_freq_knob = ramps_[8].at(i);
    frame.ls_gain_knob = 2.0f * ramps_[9].at(i) - 1.0f;
    frame.main_in = (in != nullptr ? in[0][i] : 0.0f) * kVolts;
    s.left.process(frame);
    out[0][i] = delivered(frame, output);
    if (right) {
      frame.main_in = (in != nullptr ? in[1][i] : 0.0f) * kVolts;
      s.right.process(frame);
      s.rightOut[size_t(i)] = delivered(frame, output);
    }
  }
  link_.mix(out[0], s.rightOut.data(), out[1], n);
}

}  // namespace eg13
```

Add `engine/shelves_engine.cpp` to `core/CMakeLists.txt` and to the `set_source_files_properties` line Task 5 added, so it reads:

```cmake
set_source_files_properties(engine/ripples_engine.cpp engine/shelves_engine.cpp
                            PROPERTIES COMPILE_OPTIONS "-funsafe-math-optimizations")
```

- [ ] **Step 4a: Document the EQ in the manual**

Append to `docs/manual/engines.md` (after the Ring Mod / Vocoder section):

````markdown
### EQ (the VCV model of Shelves)

A four-band equalizer modelled on the analog circuit: **High Freq** / **High Gain** (high shelf), **Mid 1 Freq** / **Mid 1 Gain** / **Mid 1 Q**, **Mid 2 Freq** / **Mid 2 Gain** / **Mid 2 Q** (two bells), **Low Freq** / **Low Gain** (low shelf). Frequencies run 20 Hz to 20 kHz, gains ±18 dB, Q 0.5 to 40. At the defaults (gains 0 dB) it is flat.

- **Input Pad** (*Off* / *−6 dB*) is the module's pre-gain option: the input goes in 6 dB lower, leaving headroom before the circuit clips (about ±10.5 V inside), and the output is 6 dB lower too.
- **Output** chooses what you hear: the *EQ*, or one of the module's six filter outputs: *Mid 1 LP*, *Mid 1 BP*, *Mid 1 HP*, *Mid 2 LP*, *Mid 2 BP*, *Mid 2 HP*, which turns the EQ into a multimode filter.
- Stereo: while the voice is stereo, a second EQ runs for the right channel, starting from the left one's exact state.
````

Append to `docs/manual/smoothing.md`:

````markdown
- **EQ:** all ten knobs move in a straight line across each 16-sample control block. Input Pad and Output switch at once.
````


- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "EQ"`
Expected: all pass. Then the full suite and the Docker command.

- [ ] **Step 6: Commit**

```bash
git add core/engine/shelves_engine.h core/engine/shelves_engine.cpp core/CMakeLists.txt tests/test_shelves_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "EQ: VCV Shelves model with its pad and filter-tap outputs"
```

---
### Task 10: Filter and Shaper slots: registry, Off, factories, ProcessorSlot

**Files:**
- Manual: `docs/manual/parts-and-routing.md` (same commit)
- Create: `core/engine/bypass_engine.h`, `core/engine/bypass_engine.cpp`, `core/voice/processor_factory.h`, `core/voice/processor_factory.cpp`, `core/voice/processor_slot.h`, `core/voice/processor_slot.cpp`
- Modify: `core/engine/engine_registry.h`, `core/engine/engine_registry.cpp`, `core/CMakeLists.txt`, `tests/test_engines.h`
- Test: `tests/test_engine_registry.cpp`, `tests/test_processor_slot.cpp`

**Interfaces:**
- Consumes: the five engines (Tasks 5–9), `NativeRateWrapper` (Task 2), `StereoDetector` (Task 3), `SourceSpec` (M2).
- Produces:
  - `enum class FilterEngine : int { Off = 0, Analog = 1, Classic = 2, StateVariable = 3 }; constexpr int kNumFilterEngines = 4; constexpr int kFilterEngineSelectorMax = 31;`
  - `enum class ShaperEngine : int { Off = 0, RingMod = 1, Eq = 2 }; constexpr int kNumShaperEngines = 3; constexpr int kShaperEngineSelectorMax = 31;`
  - `int filterEngineFromSelector(double)`, `int shaperEngineFromSelector(double)` (unknown → Off), `const EngineInfo& filterEngineInfo(int)`, `const EngineInfo& shaperEngineInfo(int)` (unknown → Off's info).
  - `class BypassEngine` ("Off", id `"off"`, `usesInput`, no parameters): copies its input to its output.
  - `SourceSpec makeOff(); SourceSpec makeFilter(int); SourceSpec makeShaper(int); std::vector<SourceSpec> makeAllFilters(); std::vector<SourceSpec> makeAllShapers(); std::vector<SourceSpec> makeOffOnly();`
  - `constexpr double kSlotFadeSeconds = 0.01;` and `class ProcessorSlot` with `explicit ProcessorSlot(std::vector<SourceSpec>)`, `prepare(double, int)`, `reset()`, `restart()`, `latencySamples()`, `engineCount()`, `engine()`, `info()`, `stereo()`, `fading()`, `setEngine(int)`, `noteOn(float, float)`, `noteOff()`, `setNote(float)`, `process(const float* const* in, const float* const* sides, float* const* out, const ParamValues& p, int n)` (`sides`: nullable array of the engine's two side inputs, each nullable).
  - Test engines: `GainEngine` (out = in · 4 · P1), `ConstantEngine` (outputs 0.5 whatever the input).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_engines.h`, before the closing namespace brace:

```cpp
// Uses input: out = in * 4 * P1 (in nullptr -> zeros).
class GainEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = in ? in[0][i] * 4.0f * p.p[0] : 0.0f;
      out[1][i] = in ? in[1][i] * 4.0f * p.p[0] : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineParamInfo kParams[1] = {{"Gain", 0.25f, nullptr}};
    static const EngineInfo kInfo = {"gain", "Gain", 1, kParams, true, false, 0, nullptr};
    return kInfo;
  }
};

// Outputs 0.5 on both channels whatever its input: an oscillator in a processor slot.
class ConstantEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) out[0][i] = out[1][i] = 0.5f;
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"constant", "Constant", 0, nullptr, true, false, 0, nullptr};
    return kInfo;
  }
};
```

Append to `tests/test_engine_registry.cpp` (add `#include "voice/processor_factory.h"` and `#include "engine/bypass_engine.h"` and `#include <vector>` at the top):

```cpp
TEST_CASE("Filter and Shaper engine ids are permanent") {
  REQUIRE(int(FilterEngine::Off) == 0);
  REQUIRE(int(FilterEngine::Analog) == 1);
  REQUIRE(int(FilterEngine::Classic) == 2);
  REQUIRE(int(FilterEngine::StateVariable) == 3);
  REQUIRE(kNumFilterEngines == 4);
  REQUIRE(kFilterEngineSelectorMax == 31);
  REQUIRE(int(ShaperEngine::Off) == 0);
  REQUIRE(int(ShaperEngine::RingMod) == 1);
  REQUIRE(int(ShaperEngine::Eq) == 2);
  REQUIRE(kNumShaperEngines == 3);
  REQUIRE(kShaperEngineSelectorMax == 31);
}

TEST_CASE("Filter and Shaper selectors fall back to Off") {
  REQUIRE(filterEngineFromSelector(2.0) == 2);
  REQUIRE(filterEngineFromSelector(4.0) == 0);
  REQUIRE(filterEngineFromSelector(std::numeric_limits<double>::quiet_NaN()) == 0);
  REQUIRE(shaperEngineFromSelector(1.4) == 1);
  REQUIRE(shaperEngineFromSelector(3.0) == 0);
  REQUIRE(shaperEngineFromSelector(-2.0) == 0);
}

TEST_CASE("Filter and Shaper engines report their display names") {
  const char* const filters[4] = {"Off", "Analog Filter", "Classic Filters", "State Variable Filter"};
  for (int e = 0; e < 4; ++e) REQUIRE(std::string(filterEngineInfo(e).displayName) == filters[e]);
  const char* const shapers[3] = {"Off", "Ring Mod / Vocoder", "EQ"};
  for (int e = 0; e < 3; ++e) REQUIRE(std::string(shaperEngineInfo(e).displayName) == shapers[e]);
  REQUIRE(std::string(filterEngineInfo(9).displayName) == "Off");
  for (int e = 0; e < 4; ++e) REQUIRE(filterEngineInfo(e).paramCount <= 4);   // spec §7.1: P1-P4
  for (int e = 0; e < 3; ++e) REQUIRE(shaperEngineInfo(e).paramCount <= 12);  // P1-P12 (amended)
  REQUIRE(shaperEngineInfo(int(ShaperEngine::RingMod)).sideInputs == 2);
}

TEST_CASE("Filter and Shaper factories build every engine at its rate") {
  const auto filters = makeAllFilters();
  REQUIRE(filters.size() == 4);
  for (int e = 0; e < 4; ++e) {
    REQUIRE(std::string(filters[size_t(e)].engine->info().id) == filterEngineInfo(e).id);
    REQUIRE(filters[size_t(e)].nativeRate == 0.0);  // host rate
    REQUIRE(filters[size_t(e)].engine->info().usesInput);
  }
  const auto shapers = makeAllShapers();
  REQUIRE(shapers.size() == 3);
  for (int e = 0; e < 3; ++e) {
    REQUIRE(std::string(shapers[size_t(e)].engine->info().id) == shaperEngineInfo(e).id);
    REQUIRE(shapers[size_t(e)].engine->info().usesInput);
  }
  REQUIRE(shapers[1].nativeRate == 96000.0);
  REQUIRE(shapers[1].nativeBlock == 60);
  REQUIRE(shapers[2].nativeRate == 0.0);
  REQUIRE(makeOffOnly().size() == 1);
}

TEST_CASE("Off passes its input through") {
  BypassEngine off;
  off.prepare(48000.0, 16);
  std::vector<float> l = {0.1f, -0.2f, 0.3f}, r = {0.4f, 0.5f, -0.6f}, ol(3), orr(3);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  off.process(in, out, ParamValues{}, 3);
  REQUIRE(ol == l);
  REQUIRE(orr == r);
  off.process(nullptr, out, ParamValues{}, 3);
  for (float s : ol) REQUIRE(s == 0.0f);
}
```

`tests/test_processor_slot.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "test_engines.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;

struct Stereo {
  std::vector<float> l, r;
};

Stereo run(ProcessorSlot& slot, const std::vector<float>& l, const std::vector<float>& r, const ParamValues& p,
           const float* side = nullptr) {
  Stereo s{std::vector<float>(l.size()), std::vector<float>(l.size())};
  for (size_t done = 0; done < l.size(); done += 16) {
    const int n = int(std::min<size_t>(16, l.size() - done));
    const float* in[2] = {l.data() + done, r.data() + done};
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    const float* sides[2] = {side ? side + done : nullptr, side ? side + done : nullptr};
    slot.process(in, sides, out, p, n);
  }
  return s;
}

int peakAt(const std::vector<float>& x) {
  return int(std::max_element(x.begin(), x.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); }) -
             x.begin());
}

std::vector<float> sine(size_t n, double hz, float amp = 0.5f) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = amp * float(std::sin(6.283185307179586 * hz * double(i) / kRate));
  return x;
}

std::vector<SourceSpec> twoGains() {
  std::vector<SourceSpec> specs;
  specs.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
  specs.push_back({std::make_unique<test::GainEngine>(), 0.0, 16});
  return specs;
}

}  // namespace

TEST_CASE("The Filter slot has no latency at common host rates") {
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    ProcessorSlot filters(makeAllFilters());
    filters.prepare(rate, 16);
    REQUIRE(filters.latencySamples() == 0);
  }
}

TEST_CASE("Every Shaper engine is padded to one slot latency") {
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(kRate, 16);
  const int latency = shapers.latencySamples();
  INFO("Shaper slot latency " << latency << " samples");
  REQUIRE(latency > 0);
  std::vector<float> impulse(4800, 0.0f);
  impulse[1000] = 0.5f;
  ParamValues off;
  REQUIRE(peakAt(run(shapers, impulse, impulse, off).l) == 1000 + latency);  // Off: exactly the latency

  shapers.setEngine(int(ShaperEngine::Eq));
  shapers.reset();
  ParamValues flat = test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq)));
  REQUIRE(std::abs(peakAt(run(shapers, impulse, impulse, flat).l) - (1000 + latency)) <= 8);

  shapers.setEngine(int(ShaperEngine::RingMod));
  shapers.reset();
  ParamValues carrierOnly = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
  carrierOnly.p[0] = 0.0f;  // crossfade ...
  carrierOnly.p[1] = 0.0f;  // ... all the way to the carrier
  carrierOnly.p[2] = 0.5f;
  carrierOnly.p[4] = 1.5f / 8.0f;  // Carrier: Voice
  carrierOnly.p[5] = 2.5f / 8.0f;  // Modulator: Input (absent: silence)
  REQUIRE(std::abs(peakAt(run(shapers, impulse, impulse, carrierOnly).l) - (1000 + latency)) <= 16);
}

TEST_CASE("Switching engines while sounding crossfades without a step") {
  ProcessorSlot slot(twoGains());
  slot.prepare(kRate, 16);
  const std::vector<float> dc(4800, 0.5f);
  ParamValues before;
  before.p[0] = 0.25f;  // gain 1
  run(slot, dc, dc, before);
  slot.setEngine(1);
  ParamValues after;
  after.p[0] = 0.75f;  // gain 3, for the new engine; the old one keeps 0.25
  const Stereo out = run(slot, dc, dc, after);
  REQUIRE(std::fabs(out.l[0] - 0.5f) < 0.01f);  // starts where it was
  for (size_t i = 1; i < out.l.size(); ++i) {
    REQUIRE(out.l[i] >= out.l[i - 1]);
    REQUIRE(out.l[i] - out.l[i - 1] <= 1.0f / 480.0f * 1.01f + 1e-6f);
  }
  REQUIRE(out.l.back() == 1.5f);
  REQUIRE_FALSE(slot.fading());
}

TEST_CASE("A Shaper switch holds the old engine for the slot latency, so the level does not dip") {
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(kRate, 16);
  const auto x = sine(9600, 1000.0, 0.25f);
  ParamValues off;
  run(shapers, x, x, off);
  shapers.setEngine(int(ShaperEngine::Eq));
  const Stereo out = run(shapers, x, x, test::defaultParams(shaperEngineInfo(int(ShaperEngine::Eq))));
  const double reference = test::rms(x, 0, 4800);
  for (size_t w = 0; w + 48 <= 2400; w += 48) {  // one 1 kHz period per window
    const double db = 20.0 * std::log10(test::rms(out.l, w, w + 48) / reference);
    INFO("window at " << w << ": " << db << " dB");
    REQUIRE(std::fabs(db) < 0.5);
  }
}

TEST_CASE("The slot tells its engine when the input is stereo, with a hold") {
  auto probe = std::make_unique<test::StereoProbeEngine>();
  test::StereoProbeEngine* raw = probe.get();
  std::vector<SourceSpec> specs;
  specs.push_back({std::move(probe), 0.0, 16});
  ProcessorSlot slot(std::move(specs));
  slot.prepare(kRate, 16);
  const auto l = sine(480, 220.0);
  const auto r = sine(480, 330.0);
  run(slot, l, l, ParamValues{});
  REQUIRE_FALSE(raw->stereo());
  run(slot, l, r, ParamValues{});
  REQUIRE(raw->stereo());
  run(slot, l, l, ParamValues{});  // 10 ms of mono: still stereo (50 ms hold)
  REQUIRE(raw->stereo());
  const std::vector<float> longer = sine(2400, 220.0);
  run(slot, longer, longer, ParamValues{});
  REQUIRE_FALSE(raw->stereo());
}

TEST_CASE("A real filter goes stereo and back without a jump") {
  for (int engine : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
    ProcessorSlot filters(makeAllFilters());
    filters.prepare(kRate, 16);
    filters.setEngine(engine);
    filters.reset();
    ParamValues p = test::defaultParams(filterEngineInfo(engine));
    p.p[0] = 0.6f;
    p.p[1] = 0.5f;
    // L is a sine; R equals L, then drifts away smoothly, then returns to L exactly.
    const size_t n = 24000;
    const auto l = sine(n, 220.0);
    std::vector<float> r(l);
    for (size_t i = 4800; i < 14400; ++i) {
      const double t = double(i - 4800) / 9600.0;
      r[i] = l[i] * float(1.0 + 0.5 * std::sin(3.14159265358979 * t));
    }
    const Stereo out = run(filters, l, r, p);
    INFO("filter " << engine);
    float maxStep = 0.0f;
    for (size_t i = 1; i < n; ++i) maxStep = std::max(maxStep, std::fabs(out.r[i] - out.r[i - 1]));
    REQUIRE(maxStep < 0.08f);  // a cut between instances would jump by up to the signal's size
    REQUIRE(test::rms(out.r, 8000, 12000) > 1.1 * test::rms(out.l, 8000, 12000));  // stereo got through
    REQUIRE_FALSE(filters.stereo());  // back to mono after the hold
  }
}

TEST_CASE("Slot processing, engine switches and stereo changes do not allocate") {
  ProcessorSlot shapers(makeAllShapers());
  ProcessorSlot filters(makeAllFilters());
  shapers.prepare(kRate, 16);
  filters.prepare(kRate, 16);
  std::vector<float> l(16, 0.2f), r(16, -0.2f), side(16, 0.1f), ol(16), orr(16);
  const float* in[2] = {l.data(), r.data()};
  float* out[2] = {ol.data(), orr.data()};
  ParamValues p;
  p.p[0] = 0.5f;
  const float* sides[2] = {side.data(), side.data()};
  shapers.process(in, sides, out, p, 16);
  filters.process(in, nullptr, out, p, 16);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 600; ++block) {
      if (block % 60 == 0) {
        shapers.setEngine((block / 60) % kNumShaperEngines);
        filters.setEngine((block / 60) % kNumFilterEngines);
      }
      in[1] = block % 200 < 100 ? r.data() : l.data();
      shapers.process(in, sides, out, p, 16);
      filters.process(in, nullptr, out, p, 16);
      if (block == 300) {
        shapers.restart();
        filters.restart();
      }
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`FilterEngine`, `voice/processor_factory.h`, `voice/processor_slot.h`, `engine/bypass_engine.h`).

- [ ] **Step 3: Write `core/engine/bypass_engine.h` and `.cpp`**

```cpp
#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// "Off" in the Filter and Shaper slots: the input passes through unchanged (padded to the
// slot's latency by its wrapper).
class BypassEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override;
  const EngineInfo& info() const override;
  static const EngineInfo& staticInfo();
};

}  // namespace eg13
```

```cpp
#include "engine/bypass_engine.h"

namespace eg13 {

namespace {
const EngineInfo kInfo = {"off", "Off", 0, nullptr, true, false, 0, nullptr};
}  // namespace

const EngineInfo& BypassEngine::staticInfo() { return kInfo; }

const EngineInfo& BypassEngine::info() const { return kInfo; }

void BypassEngine::process(const float* const* in, float* const* out, const ParamValues&, int n) {
  for (int i = 0; i < n; ++i) {
    out[0][i] = in != nullptr ? in[0][i] : 0.0f;
    out[1][i] = in != nullptr ? in[1][i] : 0.0f;
  }
}

}  // namespace eg13
```

- [ ] **Step 4: Extend the registry**

Append to `core/engine/engine_registry.h` before the closing namespace brace:

```cpp
// Filter and Shaper engine ids. Saved in projects: never renumber.
enum class FilterEngine : int { Off = 0, Analog = 1, Classic = 2, StateVariable = 3 };
constexpr int kNumFilterEngines = 4;
constexpr int kFilterEngineSelectorMax = 31;

enum class ShaperEngine : int { Off = 0, RingMod = 1, Eq = 2 };
constexpr int kNumShaperEngines = 3;
constexpr int kShaperEngineSelectorMax = 31;

int filterEngineFromSelector(double value);  // unknown values -> Off
int shaperEngineFromSelector(double value);  // unknown values -> Off
const EngineInfo& filterEngineInfo(int engine);
const EngineInfo& shaperEngineInfo(int engine);
```

In `core/engine/engine_registry.cpp`, add the includes

```cpp
#include "engine/bypass_engine.h"
#include "engine/ripples_engine.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/svf_engine.h"
#include "engine/warps_engine.h"
```

and append before the closing namespace brace:

```cpp
namespace {
int fromSelector(double value, int count) {
  if (!std::isfinite(value)) return 0;
  const long e = std::lround(value);
  return e >= 0 && e < count ? int(e) : 0;
}
}  // namespace

int filterEngineFromSelector(double value) { return fromSelector(value, kNumFilterEngines); }

int shaperEngineFromSelector(double value) { return fromSelector(value, kNumShaperEngines); }

const EngineInfo& filterEngineInfo(int engine) {
  switch (FilterEngine(engine)) {
    case FilterEngine::Analog: return RipplesEngine::staticInfo();
    case FilterEngine::Classic: return SstFilterEngine::staticInfo();
    case FilterEngine::StateVariable: return SvfEngine::staticInfo();
    case FilterEngine::Off: break;
  }
  return BypassEngine::staticInfo();
}

const EngineInfo& shaperEngineInfo(int engine) {
  switch (ShaperEngine(engine)) {
    case ShaperEngine::RingMod: return WarpsEngine::staticInfo();
    case ShaperEngine::Eq: return ShelvesEngine::staticInfo();
    case ShaperEngine::Off: break;
  }
  return BypassEngine::staticInfo();
}
```

- [ ] **Step 5: Write `core/voice/processor_factory.h` and `.cpp`**

```cpp
#pragma once

#include <vector>

#include "voice/source_factory.h"

namespace eg13 {

// Filter and Shaper engines with the rate and block size each runs at (SourceSpec serves
// every slot).
SourceSpec makeOff();
SourceSpec makeFilter(int engine);  // index = FilterEngine; unknown -> Off
SourceSpec makeShaper(int engine);  // index = ShaperEngine; unknown -> Off
std::vector<SourceSpec> makeAllFilters();
std::vector<SourceSpec> makeAllShapers();
std::vector<SourceSpec> makeOffOnly();  // a slot that can only be Off (test voices)

}  // namespace eg13
```

```cpp
#include "voice/processor_factory.h"

#include <memory>

#include "engine/bypass_engine.h"
#include "engine/engine_registry.h"
#include "engine/ripples_engine.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/svf_engine.h"
#include "engine/warps_engine.h"

namespace eg13 {

namespace {
constexpr int kHostBlock = 16;  // host-rate engines: the wrapper bypasses; any block size works
}  // namespace

SourceSpec makeOff() { return {std::make_unique<BypassEngine>(), 0.0, kHostBlock}; }

SourceSpec makeFilter(int engine) {
  switch (FilterEngine(engine)) {
    case FilterEngine::Analog: return {std::make_unique<RipplesEngine>(), 0.0, kHostBlock};
    case FilterEngine::Classic: return {std::make_unique<SstFilterEngine>(), 0.0, kHostBlock};
    case FilterEngine::StateVariable: return {std::make_unique<SvfEngine>(), 0.0, kHostBlock};
    case FilterEngine::Off: break;
  }
  return makeOff();
}

SourceSpec makeShaper(int engine) {
  switch (ShaperEngine(engine)) {
    case ShaperEngine::RingMod:
      return {std::make_unique<WarpsEngine>(), WarpsEngine::kNativeRate, WarpsEngine::kNativeBlock};
    case ShaperEngine::Eq: return {std::make_unique<ShelvesEngine>(), 0.0, kHostBlock};
    case ShaperEngine::Off: break;
  }
  return makeOff();
}

std::vector<SourceSpec> makeAllFilters() {
  std::vector<SourceSpec> all;
  for (int e = 0; e < kNumFilterEngines; ++e) all.push_back(makeFilter(e));
  return all;
}

std::vector<SourceSpec> makeAllShapers() {
  std::vector<SourceSpec> all;
  for (int e = 0; e < kNumShaperEngines; ++e) all.push_back(makeShaper(e));
  return all;
}

std::vector<SourceSpec> makeOffOnly() {
  std::vector<SourceSpec> only;
  only.push_back(makeOff());
  return only;
}

}  // namespace eg13
```

- [ ] **Step 6: Write `core/voice/processor_slot.h`**

```cpp
#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "engine/stereo_link.h"
#include "voice/source_factory.h"

namespace eg13 {

constexpr double kSlotFadeSeconds = 0.01;  // crossfade between engines on a change

// One Filter or Shaper slot of a voice (spec §2). Holds every engine of the slot, preallocated,
// each in its own NativeRateWrapper and padded to one slot latency, so a change of engine never
// moves the output in time. Tells its engine whether the input is stereo (§5.1).
class ProcessorSlot {
 public:
  explicit ProcessorSlot(std::vector<SourceSpec> engines);  // index = engine id; at least one

  void prepare(double hostRate, int maxBlock);  // main thread: pads every engine to latencySamples()
  void reset();    // every engine; no fade; mono
  void restart();  // the current engine only: a new note on an idle voice, or after a NaN

  int latencySamples() const { return latency_; }
  int engineCount() const { return int(engines_.size()); }
  int engine() const { return current_; }
  const EngineInfo& info() const { return engines_[size_t(current_)]->info(); }
  bool stereo() const { return detector_.stereo(); }
  bool fading() const { return previous_ >= 0; }

  // Audio thread. The old engine keeps running with its last parameters, alone for the slot
  // latency (when the new engine's first output arrives), then crossfades to the new one over
  // kSlotFadeSeconds. A change during a fade drops the oldest engine.
  void setEngine(int engine);

  void noteOn(float note, float velocity);
  void noteOff();
  void setNote(float note);

  // in: two channels; sides: the engine's two mono side inputs (the array and each entry
  // nullable); out: two channels, not aliasing in. n <= the maxBlock given to prepare().
  void process(const float* const* in, const float* const* sides, float* const* out,
               const ParamValues& p, int n);

 private:
  NativeRateWrapper& at(int engine) { return *engines_[size_t(engine)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> engines_;
  int current_ = 0;
  int previous_ = -1;
  int latency_ = 0;
  StereoDetector detector_;
  ParamValues last_{};        // the current engine's parameters from the last block
  ParamValues fadeParams_{};  // the previous engine's, held through the fade
  int fadeHold_ = 0;
  float fadeWeight_ = 1.0f;
  float fadeStep_ = 1.0f;
  float note_ = 60.0f;
  float velocity_ = 1.0f;
  bool gate_ = false;
  std::vector<float> fade_[2];
};

}  // namespace eg13
```

- [ ] **Step 7: Write `core/voice/processor_slot.cpp`**

```cpp
#include "voice/processor_slot.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace eg13 {

ProcessorSlot::ProcessorSlot(std::vector<SourceSpec> engines) {
  for (auto& e : engines) {
    engines_.push_back(std::make_unique<NativeRateWrapper>(std::move(e.engine), e.nativeRate, e.nativeBlock));
  }
  assert(!engines_.empty() && "a slot needs at least one engine");
}

void ProcessorSlot::prepare(double hostRate, int maxBlock) {
  latency_ = 0;
  for (auto& e : engines_) {
    e->prepare(hostRate, maxBlock);
    latency_ = std::max(latency_, e->inputPathLatencySamples());
  }
  for (auto& e : engines_) e->setExtraDelay(latency_ - e->inputPathLatencySamples());
  detector_.prepare(hostRate);
  fadeStep_ = float(1.0 / std::max(1.0, hostRate * kSlotFadeSeconds));
  for (auto& buffer : fade_) buffer.assign(size_t(maxBlock), 0.0f);
  reset();
}

void ProcessorSlot::reset() {
  for (auto& e : engines_) e->reset();
  detector_.reset();
  previous_ = -1;
  fadeHold_ = 0;
  fadeWeight_ = 1.0f;
  gate_ = false;
  last_ = ParamValues{};
}

void ProcessorSlot::restart() {
  at(current_).reset();
  detector_.reset();
  previous_ = -1;
  fadeHold_ = 0;
  fadeWeight_ = 1.0f;
}

void ProcessorSlot::setEngine(int engine) {
  engine = std::clamp(engine, 0, engineCount() - 1);
  if (engine == current_) return;
  previous_ = current_;
  fadeParams_ = last_;
  current_ = engine;
  NativeRateWrapper& next = at(current_);
  next.reset();
  next.setNote(note_);
  if (gate_) next.noteOn(note_, velocity_);
  fadeHold_ = latency_;
  fadeWeight_ = 0.0f;
}

void ProcessorSlot::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = velocity;
  gate_ = true;
  at(current_).noteOn(note, velocity);
  if (previous_ >= 0) at(previous_).noteOn(note, velocity);
}

void ProcessorSlot::noteOff() {
  gate_ = false;
  at(current_).noteOff();
  if (previous_ >= 0) at(previous_).noteOff();
}

void ProcessorSlot::setNote(float note) {
  note_ = note;
  at(current_).setNote(note);
  if (previous_ >= 0) at(previous_).setNote(note);
}

void ProcessorSlot::process(const float* const* in, const float* const* sides, float* const* out,
                            const ParamValues& p, int n) {
  const bool stereo = detector_.update(in[0], in[1], n);
  const float* channels[4] = {in[0], in[1], sides != nullptr ? sides[0] : nullptr,
                              sides != nullptr ? sides[1] : nullptr};
  NativeRateWrapper& current = at(current_);
  current.setStereo(stereo);
  current.process(channels, out, p, n);
  last_ = p;
  if (previous_ < 0) return;
  float* old[2] = {fade_[0].data(), fade_[1].data()};
  NativeRateWrapper& previous = at(previous_);
  previous.setStereo(stereo);
  previous.process(channels, old, fadeParams_, n);
  for (int i = 0; i < n; ++i) {
    if (fadeHold_ > 0) {
      --fadeHold_;
    } else {
      fadeWeight_ = std::min(1.0f, fadeWeight_ + fadeStep_);
    }
    out[0][i] = old[0][i] + fadeWeight_ * (out[0][i] - old[0][i]);
    out[1][i] = old[1][i] + fadeWeight_ * (out[1][i] - old[1][i]);
  }
  if (fadeWeight_ >= 1.0f) previous_ = -1;
}

}  // namespace eg13
```

Add `engine/bypass_engine.cpp`, `voice/processor_factory.cpp` and `voice/processor_slot.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 7a: Document the slots in the manual**

In `docs/manual/parts-and-routing.md`, after the "Engine switching" section's second paragraph, add:

````markdown
Changing the **Filter** or **Shaper** type is smooth: while a note sounds, the old engine keeps playing with its last settings until the new one's sound arrives, then the two crossfade over 10 ms. The timing never changes, because every Filter and every Shaper (Off included) is delayed to the same latency.

## Stereo

The voice chain is stereo. A Filter or Shaper runs a second, right-channel copy only while its input is stereo, which saves CPU for mono sounds.

- The input counts as stereo from the first moment left and right differ. It goes back to mono after 50 ms in which they are identical; silence counts as identical.
- When stereo starts, the right copy continues exactly where the left one is, so nothing jumps. The Ring Mod / Vocoder can't copy its state, so its right copy fades in over 10 ms instead.
- When stereo stops, the right channel fades back to the left one over 10 ms.
````

- [ ] **Step 8: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: all pass. Record the "Shaper slot latency" printed by `ctest --preset mac -R "padded to one slot latency" -V` for Task 15's README figures. If the Ring Mod impulse lands more than 16 samples off, report the measured offset as BLOCKED rather than widening the tolerance (Warps' own 6× resampling filters account for a few samples).

- [ ] **Step 9: Commit**

```bash
git add core/engine/bypass_engine.h core/engine/bypass_engine.cpp core/engine/engine_registry.h core/engine/engine_registry.cpp \
  core/voice/processor_factory.h core/voice/processor_factory.cpp core/voice/processor_slot.h core/voice/processor_slot.cpp \
  core/CMakeLists.txt tests/test_engines.h tests/test_engine_registry.cpp tests/test_processor_slot.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Filter and Shaper slots: registry, Off, factories, padded latency, stereo detection, crossfaded switching"
```

---
### Task 11: Voice and Part: Source → Filter → Shaper, side signals, latency, memory

**Files:**
- Manual: `docs/manual/parts-and-routing.md` (same commit)
- Modify: `core/voice/voice.h`, `core/voice/voice.cpp`, `core/part/part.h`, `core/part/part.cpp`, `tests/test_engines.h`
- Test: `tests/test_voice.cpp`, `tests/test_part.cpp`

**Interfaces:**
- Consumes: `ProcessorSlot`, `makeAllFilters`, `makeAllShapers`, `makeOffOnly` (Task 10), `sideSourceOf`, `setAuxEnabled` (Task 2).
- Produces:
  - `struct VoiceInputs { const float* const* input = nullptr; const float* const* other = nullptr; };` (two channels each, nullable).
  - `struct SlotParams { ParamValues source, filter, shaper; };`
  - `Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters, std::vector<SourceSpec> shapers)`; the M2 constructors keep working with Off-only slots. `setFilterEngine(int)`, `setShaperEngine(int)`, `filterEngine()`, `shaperEngine()`, `process(const VoiceInputs&, float* const* out, const SlotParams&, int n)`; the M2 `process(in, out, ParamValues, n)` overloads remain (Filter/Shaper parameters zero). `latencySamples()` = Source + Filter + Shaper latency.
  - `PartSettings` gains `int filterEngine = 0; int shaperEngine = 0;`. `Part()` builds every Source, Filter and Shaper engine per voice. `Part::process(const VoiceInputs&, float* const* out, const SlotParams&, int n)`; the M2 overloads remain.
  - Test engine `AuxSourceEngine` (L = R = 0.25 while gated; AUX = 0.75 when enabled).

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_engines.h`, before the closing namespace brace:

```cpp
// A source with an AUX output: L = R = 0.25 while gated; AUX (when enabled) = 0.75.
class AuxSourceEngine final : public VoiceEngine {
 public:
  void prepare(double, int) override {}
  void reset() override { gate_ = false; }
  void noteOn(float, float) override { gate_ = true; }
  void noteOff() override { gate_ = false; }
  void setNote(float) override {}
  void setAuxEnabled(bool on) override { aux_ = on; }
  void process(const float* const*, float* const* out, const ParamValues&, int n) override {
    for (int i = 0; i < n; ++i) {
      out[0][i] = out[1][i] = gate_ ? 0.25f : 0.0f;
      if (aux_) out[2][i] = gate_ ? 0.75f : 0.0f;
    }
  }
  const EngineInfo& info() const override {
    static const EngineInfo kInfo = {"auxsource", "AUX source", 0, nullptr, false, true, 0, nullptr};
    return kInfo;
  }

 private:
  bool gate_ = false;
  bool aux_ = false;
};
```

Append to `tests/test_voice.cpp` (add `#include "engine/warps_engine.h"` and `#include "voice/processor_factory.h"` and `#include "voice/processor_slot.h"` at the top):

```cpp
namespace {

std::vector<SourceSpec> specs(SourceSpec a) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(a));
  return v;
}

std::vector<SourceSpec> specs(SourceSpec a, SourceSpec b) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(a));
  v.push_back(std::move(b));
  return v;
}

SourceSpec hostRate(std::unique_ptr<VoiceEngine> e) { return {std::move(e), 0.0, 16}; }

std::vector<float> runChain(Voice& voice, int n, const SlotParams& p, const VoiceInputs& in = VoiceInputs{},
                            std::vector<float>* right = nullptr) {
  std::vector<float> l(size_t(n), 0.0f), r(size_t(n), 0.0f);
  for (int done = 0; done < n; done += 64) {
    const int count = std::min(64, n - done);
    float* out[2] = {l.data() + done, r.data() + done};
    const float* input[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    const float* other[2] = {in.other ? in.other[0] + done : nullptr, in.other ? in.other[1] + done : nullptr};
    voice.process(VoiceInputs{in.input ? input : nullptr, in.other ? other : nullptr}, out, p, count);
  }
  if (right) *right = r;
  return l;
}

}  // namespace

TEST_CASE("The voice chain runs Source, then Filter, then Shaper") {
  Voice plain(sine());
  Voice chain(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::GainEngine>())),
              specs(makeOff(), hostRate(std::make_unique<test::GainEngine>())));
  for (Voice* v : {&plain, &chain}) v->prepare(48000.0, 256);
  chain.setFilterEngine(1);
  chain.setShaperEngine(1);
  plain.trigger(69, 1.0f);
  chain.trigger(69, 1.0f);
  SlotParams p;
  p.filter.p[0] = 0.5f;   // x2
  p.shaper.p[0] = 0.75f;  // x3
  const auto a = runChain(plain, 4800, SlotParams{});
  const auto b = runChain(chain, 4800, p);
  for (size_t i = 0; i < a.size(); ++i) REQUIRE(std::fabs(b[i] - 6.0f * a[i]) <= 1e-6f);
}

TEST_CASE("Voice latency adds the Filter and Shaper slots") {
  Voice voice(makeAllSources(), makeAllFilters(), makeAllShapers());
  voice.prepare(48000.0, 256);
  Voice sourceOnly(makeAllSources());
  sourceOnly.prepare(48000.0, 256);
  ProcessorSlot shapers(makeAllShapers());
  shapers.prepare(48000.0, Voice::kControlBlock);
  INFO("voice " << voice.latencySamples() << " source " << sourceOnly.latencySamples() << " shaper "
                << shapers.latencySamples());
  REQUIRE(shapers.latencySamples() > 0);
  REQUIRE(voice.latencySamples() == sourceOnly.latencySamples() + shapers.latencySamples());
}

TEST_CASE("The Shaper's side inputs follow their selectors") {
  Voice voice(specs(hostRate(std::make_unique<test::AuxSourceEngine>())), makeOffOnly(),
              specs(makeOff(), hostRate(std::make_unique<test::SideEchoEngine>())));
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(60, 1.0f);
  const std::vector<float> inL(256, 0.1f), inR(256, 0.3f), otherL(256, -0.4f), otherR(256, 0.0f);
  const float* input[2] = {inL.data(), inR.data()};
  const float* other[2] = {otherL.data(), otherR.data()};
  const VoiceInputs in{input, other};
  const struct {
    float selector;
    float expected;
  } cases[] = {{0.5f / 4.0f, 0.0f}, {1.5f / 4.0f, 0.75f}, {2.5f / 4.0f, 0.2f}, {3.5f / 4.0f, -0.2f}};
  for (const auto& c : cases) {
    for (int input = 0; input < 2; ++input) {
      SlotParams p;
      p.shaper.p[input] = c.selector;    // SideEchoEngine: P1 picks side 1 (out L), P2 side 2 (out R)
      p.shaper.p[1 - input] = 0.0f;      // the other one: Voice, so nothing is passed
      std::vector<float> right;
      const auto l = runChain(voice, 256, p, in, &right);
      const float got = input == 0 ? l.back() : right.back();
      const float other = input == 0 ? right.back() : l.back();
      INFO("side " << input << " selector " << c.selector);
      REQUIRE(std::fabs(got - c.expected) < 1e-6f);
      REQUIRE(other == 0.0f);
    }
  }
}

TEST_CASE("Source AUX falls back to the main output for sources without AUX") {
  Voice voice(specs(sine()), makeOffOnly(), specs(makeOff(), hostRate(std::make_unique<test::SideEchoEngine>())));
  Voice plain(sine());
  for (Voice* v : {&voice, &plain}) v->prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(69, 1.0f);
  plain.trigger(69, 1.0f);
  SlotParams p;
  p.shaper.p[0] = 1.5f / 4.0f;  // side 1: Source AUX
  const auto l = runChain(voice, 4800, p);
  const auto sine = runChain(plain, 4800, SlotParams{});
  for (size_t i = 0; i < l.size(); ++i) REQUIRE(l[i] == sine[i]);  // (L + R) / 2 of a mono sine = L
}

TEST_CASE("Absent modulator sources give silence, not garbage") {
  Voice voice(specs(sine()), makeOffOnly(), makeAllShapers());
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(int(ShaperEngine::RingMod));
  voice.trigger(69, 1.0f);
  SlotParams p;
  p.shaper = test::defaultParams(WarpsEngine::staticInfo());
  p.shaper.p[0] = 0.375f;  // digital ring modulation: silent when either input is
  p.shaper.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
  for (int modulator : {2, 3}) {  // Modulator Input, Other part: nothing there
    p.shaper.p[5] = WarpsEngine::optionValue(modulator);
    const auto out = runChain(voice, 9600, p);
    for (float s : out) REQUIRE(std::isfinite(s));
    REQUIRE(test::rms(out, 4800, 9600) < 1e-4);
  }
  p.shaper.p[5] = WarpsEngine::optionValue(0);  // Modulator: Voice
  for (int carrier : {3, 4}) {  // Carrier Input, Other part: nothing there
    p.shaper.p[4] = WarpsEngine::optionValue(carrier);
    const auto out = runChain(voice, 9600, p);
    for (float s : out) REQUIRE(std::isfinite(s));
    REQUIRE(test::rms(out, 4800, 9600) < 1e-4);
  }
  p.shaper.p[4] = WarpsEngine::optionValue(1);  // Carrier: Voice
  p.shaper.p[5] = WarpsEngine::optionValue(1);  // Modulator: Source AUX, the sine itself
  const auto self = runChain(voice, 9600, p);
  REQUIRE(test::rms(self, 4800, 9600) > 0.01);
}

TEST_CASE("A Shaper that keeps sounding does not hold the voice forever") {
  Voice voice(specs(sine()), makeOffOnly(), specs(makeOff(), hostRate(std::make_unique<test::ConstantEngine>())));
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(1);
  voice.trigger(60, 1.0f);
  runChain(voice, 4800, SlotParams{});
  voice.release();
  runChain(voice, 1920, SlotParams{});  // 40 ms: still active
  REQUIRE(voice.active());
  runChain(voice, 4800, SlotParams{});
  REQUIRE_FALSE(voice.active());
}

TEST_CASE("A NaN from the Filter restarts the chain and mutes the block") {
  Voice voice(specs(sine()), specs(makeOff(), hostRate(std::make_unique<test::NanOnceEngine>())), makeOffOnly());
  voice.prepare(48000.0, 256);
  voice.setFilterEngine(1);
  voice.trigger(60, 1.0f);
  const auto first = runChain(voice, 64, SlotParams{});
  for (size_t i = 0; i < 16; ++i) REQUIRE(first[i] == 0.0f);
  const auto later = runChain(voice, 4800, SlotParams{});
  for (float s : later) REQUIRE(std::isfinite(s));
  REQUIRE(voice.active());
}

TEST_CASE("The Ring Mod oscillator follows the voice's note") {
  Voice voice(specs(sine()), makeOffOnly(), makeAllShapers());
  voice.prepare(48000.0, 256);
  voice.setShaperEngine(int(ShaperEngine::RingMod));
  voice.trigger(57, 1.0f);
  SlotParams p;
  p.shaper = test::defaultParams(WarpsEngine::staticInfo());  // Carrier Internal Osc
  p.shaper.p[0] = 0.0f;    // crossfade ...
  p.shaper.p[1] = 0.0f;    // ... all the way to the carrier
  p.shaper.p[6] = 0.75f;   // Osc Pitch +12 st: 440 Hz
  const auto out = runChain(voice, 48000, p);
  REQUIRE_THAT(test::estimateFrequency(out, 48000.0, 9600, 48000), Catch::Matchers::WithinRel(440.0, 0.003));
}

TEST_CASE("Voice processing with every engine and slot switches does not allocate") {
  Voice voice(makeAllSources(), makeAllFilters(), makeAllShapers());
  voice.prepare(48000.0, 256);
  std::vector<float> l(256), r(256), in(256, 0.1f);
  float* out[2] = {l.data(), r.data()};
  const float* input[2] = {in.data(), in.data()};
  SlotParams p;
  for (float& v : p.source.p) v = 0.5f;
  for (float& v : p.filter.p) v = 0.5f;
  for (float& v : p.shaper.p) v = 0.5f;
  voice.trigger(60, 1.0f);
  voice.process(VoiceInputs{input, input}, out, p, 256);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 60; ++block) {
      if (block % 10 == 0) {
        voice.setFilterEngine((block / 10) % kNumFilterEngines);
        voice.setShaperEngine((block / 10) % kNumShaperEngines);
        voice.setEngine((block / 10) % kNumSourceEngines);
      }
      if (block == 30) voice.trigger(64, 0.7f);
      voice.process(VoiceInputs{input, input}, out, p, 256);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

Append to `tests/test_part.cpp` (add `#include <cstdio>`, `#include "voice/processor_factory.h"` at the top, and `#include <sys/resource.h>`):

```cpp
TEST_CASE("A part switches every voice's Filter from its settings") {
  auto rmsWith = [](int filter) {
    Part part;
    part.prepare(kRate, kBlock);
    PartSettings s = part.settings();
    s.filterEngine = filter;
    part.applySettings(s);
    part.noteOn(60, 1.0f);
    SlotParams p;
    p.source = test::defaultParams(sourceEngineInfo(0));
    p.filter = test::defaultParams(filterEngineInfo(filter));
    p.filter.p[0] = 0.1f;  // cutoff ~40 Hz where it applies
    std::vector<float> l(24000), r(24000);
    for (int done = 0; done < 24000; done += kBlock) {
      float* out[2] = {l.data() + done, r.data() + done};
      part.process(VoiceInputs{}, out, p, kBlock);
    }
    return test::rms(l, 4800, 24000);
  };
  const double open = rmsWith(int(FilterEngine::Off));
  REQUIRE(open > 0.01);
  for (int filter : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
    INFO("filter " << filter);
    REQUIRE(rmsWith(filter) < 0.3 * open);
  }
}

TEST_CASE("Memory per part with every engine preallocated", "[memory]") {
  struct rusage before {};
  getrusage(RUSAGE_SELF, &before);
  size_t constructed = 0, prepared = 0;
  std::unique_ptr<Part> part;
  {
    test::ScopedAllocationCounter counter;
    part = std::make_unique<Part>();
    constructed = counter.bytes();
  }
  {
    test::ScopedAllocationCounter counter;
    part->prepare(48000.0, 512);
    prepared = counter.bytes();
  }
  struct rusage after {};
  getrusage(RUSAGE_SELF, &after);
#ifdef __APPLE__
  const double rssMb = double(after.ru_maxrss - before.ru_maxrss) / 1048576.0;  // bytes
#else
  const double rssMb = double(after.ru_maxrss - before.ru_maxrss) / 1024.0;  // kilobytes
#endif
  std::printf("Part memory: %.2f MB constructed + %.2f MB prepared (48 kHz, 512); peak RSS grew %.2f MB\n",
              double(constructed) / 1048576.0, double(prepared) / 1048576.0, rssMb);
  REQUIRE(constructed + prepared < size_t(64) * 1024 * 1024);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`VoiceInputs`, `SlotParams`, the three-slot `Voice` constructor, `setFilterEngine`, `PartSettings::filterEngine`).

- [ ] **Step 3: Replace `core/voice/voice.h`**

```cpp
#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "voice/processor_slot.h"
#include "voice/source_factory.h"

namespace eg13 {

// What a voice reads besides its notes: the part's Input as its Input source setting routes it
// (spec §2.3), and the other part's output (soft-limited by the Synth). The Shaper's side
// signal (the Ring Mod / Vocoder modulator) comes from either, or from the Source's AUX.
struct VoiceInputs {
  const float* const* input = nullptr;  // two channels, nullable
  const float* const* other = nullptr;  // two channels, nullable
};

// Post-modulation parameters of each slot in the voice chain.
struct SlotParams {
  ParamValues source;
  ParamValues filter;
  ParamValues shaper;
};

// One synth voice: Source -> Filter -> Shaper (spec §2), plus note, glide and bend handling and
// silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples

  enum class Alignment { Notes, Input };

  explicit Voice(SourceSpec source);                // one source; Filter and Shaper only Off
  explicit Voice(std::vector<SourceSpec> sources);  // index = SourceEngine; Filter, Shaper Off
  Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters,
        std::vector<SourceSpec> shapers);  // index = SourceEngine / FilterEngine / ShaperEngine

  // main thread
  void prepare(double hostRate, int maxBlock, Alignment alignment = Alignment::Notes);
  void reset();
  void setEngine(int engine);        // Source; audio thread
  void setFilterEngine(int engine);  // audio thread; crossfades while sounding
  void setShaperEngine(int engine);  // audio thread; crossfades while sounding
  int engine() const { return current_; }
  int filterEngine() const { return filter_.engine(); }
  int shaperEngine() const { return shaper_.engine(); }
  int engineCount() const { return int(sources_.size()); }
  void trigger(int key, float velocity);
  void glideTo(int key);
  void release();
  void setPitchOffset(float semitones) { pitchOffset_ = semitones; }
  void setGlideSeconds(float seconds) { glideSeconds_ = seconds; }

  // Adds n samples into out[0], out[1].
  void process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n);
  // Source parameters only (Filter and Shaper get zeros). in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& p, int n);
  void process(float* const* out, const ParamValues& p, int n) { process(nullptr, out, p, n); }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return float(currentNote_); }
  int latencySamples() const { return latency_; }  // Source + Filter + Shaper

 private:
  bool blockIsFinite(int count) const;
  // The Shaper's side inputs for this control block: Source AUX, or the part's Input or the
  // other part's output summed to mono; nullptr for Voice, None or a missing signal.
  void sideSignals(const VoiceInputs& in, const ParamValues& shaper, int offset, int count, const float** sides);
  void restartChain();
  NativeRateWrapper& source() { return *sources_[size_t(current_)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> sources_;
  ProcessorSlot filter_;
  ProcessorSlot shaper_;
  int current_ = 0;
  int latency_ = 0;
  double hostRate_ = 48000.0;
  float targetNote_ = 60.0f;
  double currentNote_ = 60.0;  // double: float stalls short of the target on slow glides
  float pitchOffset_ = 0.0f;
  float glideSeconds_ = 0.0f;
  bool gate_ = false;
  bool retrigger_ = false;  // re-send the note after a NaN reset
  float velocity_ = 1.0f;
  bool active_ = false;
  int silentSamples_ = 0;
  int silenceLimit_ = 2400;
  float source_[3][kControlBlock] = {};  // Source L, R, AUX
  float filtered_[2][kControlBlock] = {};
  float side_[2][kControlBlock] = {};  // mono side inputs (Ring Mod CARRIER, MODULATOR)
  float buffer_[2][kControlBlock] = {};  // the voice's output (Shaper)
};

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/voice/voice.cpp`**

```cpp
#include "voice/voice.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

#include "voice/processor_factory.h"

namespace eg13 {

namespace {
constexpr float kSilenceThreshold = 3.1623e-5f;  // -90 dBFS
constexpr double kSilenceSeconds = 0.05;

std::vector<SourceSpec> only(SourceSpec source) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(source));
  return v;
}

bool finite(const float* a, const float* b, int count) {
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(a[i]) || !std::isfinite(b[i])) return false;
  }
  return true;
}
}  // namespace

Voice::Voice(SourceSpec source) : Voice(only(std::move(source))) {}

Voice::Voice(std::vector<SourceSpec> sources) : Voice(std::move(sources), makeOffOnly(), makeOffOnly()) {}

Voice::Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters, std::vector<SourceSpec> shapers)
    : filter_(std::move(filters)), shaper_(std::move(shapers)) {
  for (auto& s : sources) {
    sources_.push_back(std::make_unique<NativeRateWrapper>(std::move(s.engine), s.nativeRate, s.nativeBlock));
    sources_.back()->setAuxEnabled(true);  // Source AUX, for the Ring Mod modulator (spec §4)
  }
  assert(!sources_.empty() && "a voice needs at least one source engine");
}

void Voice::prepare(double hostRate, int maxBlock, Alignment alignment) {
  hostRate_ = hostRate;
  silenceLimit_ = int(hostRate * kSilenceSeconds);
  auto path = [alignment](const NativeRateWrapper& s) {
    return alignment == Alignment::Input && s.info().usesInput ? s.inputLatencySamples()
                                                               : s.outputLatencySamples();
  };
  int sourceLatency = 0;
  for (auto& s : sources_) {
    s->prepare(hostRate, maxBlock);
    sourceLatency = std::max(sourceLatency, path(*s));
  }
  for (auto& s : sources_) s->setExtraDelay(sourceLatency - path(*s));
  // The Filter and Shaper see one control block per call.
  filter_.prepare(hostRate, kControlBlock);
  shaper_.prepare(hostRate, kControlBlock);
  latency_ = sourceLatency + filter_.latencySamples() + shaper_.latencySamples();
  reset();
}

void Voice::reset() {
  for (auto& s : sources_) s->reset();
  filter_.reset();
  shaper_.reset();
  gate_ = false;
  retrigger_ = false;
  active_ = false;
  silentSamples_ = 0;
}

void Voice::restartChain() {
  source().reset();
  filter_.restart();
  shaper_.restart();
}

void Voice::setEngine(int engine) {
  engine = std::clamp(engine, 0, int(sources_.size()) - 1);
  if (engine == current_) return;
  // Hard cut: the old engine is left as is (it is reset when selected again or by
  // reset()), and a held note restarts after latencySamples() of silence.
  current_ = engine;
  source().reset();
  retrigger_ = false;
  silentSamples_ = 0;
  if (gate_) {
    source().noteOn(float(currentNote_) + pitchOffset_, velocity_);
  } else {
    active_ = false;
  }
}

void Voice::setFilterEngine(int engine) { filter_.setEngine(engine); }

void Voice::setShaperEngine(int engine) { shaper_.setEngine(engine); }

void Voice::trigger(int key, float velocity) {
  targetNote_ = float(key);
  velocity_ = velocity;
  retrigger_ = false;
  if (!active_) {
    restartChain();  // drop stale converter samples and filter state from the previous note
    currentNote_ = double(targetNote_);
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = double(targetNote_);
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
  const float note = float(currentNote_) + pitchOffset_;
  source().noteOn(note, velocity);
  filter_.noteOn(note, velocity);
  shaper_.noteOn(note, velocity);
}

void Voice::glideTo(int key) {
  targetNote_ = float(key);
  if (glideSeconds_ <= 0.0f) currentNote_ = double(targetNote_);
}

bool Voice::blockIsFinite(int count) const {
  return finite(source_[0], source_[1], count) && finite(buffer_[0], buffer_[1], count);
}

void Voice::release() {
  gate_ = false;
  retrigger_ = false;  // a pending NaN re-gate must not outlive the key release
  source().noteOff();
  filter_.noteOff();
  shaper_.noteOff();
}

void Voice::sideSignals(const VoiceInputs& in, const ParamValues& shaper, int offset, int count,
                        const float** sides) {
  sides[0] = sides[1] = nullptr;
  const EngineInfo& info = shaper_.info();
  if (info.sideSource == nullptr) return;
  for (int k = 0; k < std::min(info.sideInputs, 2); ++k) {
    const float* const* stereo = nullptr;
    switch (info.sideSource(k, shaper)) {
      case SideSource::Voice:  // the engine uses its own input, channel by channel
      case SideSource::None:   // the engine makes the signal (Warps' internal oscillator)
        continue;
      case SideSource::SourceAux:
        sides[k] = source_[2];
        continue;
      case SideSource::Input:
        stereo = in.input;
        break;
      case SideSource::OtherPart:
        stereo = in.other;
        break;
    }
    if (stereo == nullptr) continue;  // nothing there: the engine reads silence
    for (int i = 0; i < count; ++i) side_[k][i] = 0.5f * (stereo[0][offset + i] + stereo[1][offset + i]);
    sides[k] = side_[k];
  }
}

void Voice::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  SlotParams params;
  params.source = p;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Voice::process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n) {
  if (!active_) return;
  if (retrigger_ && gate_) {  // the held note after a NaN restart (restartChain)
    const float note = float(currentNote_) + pitchOffset_;
    source().noteOn(note, velocity_);
    filter_.noteOn(note, velocity_);
    shaper_.noteOn(note, velocity_);
    retrigger_ = false;
  }
  for (int done = 0; done < n;) {
    const int count = std::min(kControlBlock, n - done);
    const double coeff =
        glideSeconds_ > 0.0f
            ? std::exp(-double(count) / (double(glideSeconds_) * hostRate_))
            : 0.0;
    currentNote_ = double(targetNote_) + (currentNote_ - double(targetNote_)) * coeff;
    if (std::fabs(currentNote_ - double(targetNote_)) < 1e-4) currentNote_ = double(targetNote_);
    const float note = float(currentNote_) + pitchOffset_;
    source().setNote(note);
    filter_.setNote(note);
    shaper_.setNote(note);

    float* src[3] = {source_[0], source_[1], source_[2]};
    const float* inBlock[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    source().process(in.input ? inBlock : nullptr, src, p.source, count);
    if (!source().info().hasAux) {
      // Sources without an AUX output give their main output instead (spec §4).
      for (int i = 0; i < count; ++i) source_[2][i] = 0.5f * (source_[0][i] + source_[1][i]);
    }
    const float* filterIn[2] = {source_[0], source_[1]};
    float* filterOut[2] = {filtered_[0], filtered_[1]};
    filter_.process(filterIn, nullptr, filterOut, p.filter, count);
    const float* shaperIn[2] = {filtered_[0], filtered_[1]};
    float* shaperOut[2] = {buffer_[0], buffer_[1]};
    const float* sides[2] = {nullptr, nullptr};
    sideSignals(in, p.shaper, done, count, sides);
    shaper_.process(shaperIn, sides, shaperOut, p.shaper, count);

    if (!blockIsFinite(count)) {
      // Spec §9: a NaN/Inf from an engine resets the chain and mutes this block; a
      // held note starts again on the next call.
      restartChain();
      retrigger_ = gate_;
      active_ = gate_;
      return;
    }
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
      out[0][done + i] += buffer_[0][i];
      out[1][done + i] += buffer_[1][i];
      peak = std::max(peak, std::max(std::fabs(filtered_[0][i]), std::fabs(filtered_[1][i])));
    }
    done += count;

    // Silence is measured at the Shaper input: the Ring Mod's internal oscillator keeps the
    // Shaper's output alive after the note has died. The Shaper latency lets its output flush.
    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
      if (silentSamples_ >= silenceLimit_ + shaper_.latencySamples()) {
        active_ = false;
        return;
      }
    } else {
      silentSamples_ = 0;
    }
  }
}

}  // namespace eg13
```

- [ ] **Step 5: Update `core/part/part.h` and `core/part/part.cpp`**

In `part.h`: add to `PartSettings`, after `int engine = 0;  // SourceEngine`:

```cpp
  int filterEngine = 0;  // FilterEngine
  int shaperEngine = 0;  // ShaperEngine
```

change the `Part()` comment to `// every Source, Filter and Shaper engine`, and replace the two `process` declarations with:

```cpp
  // Overwrites out[0], out[1] with n samples (n <= maxBlock).
  void process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n);
  // Source parameters only; in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& source, int n);
  void process(float* const* out, const ParamValues& source, int n) {
    process(nullptr, out, source, n);
  }
```

In `part.cpp`: add `#include "voice/processor_factory.h"`; change the default constructor to

```cpp
Part::Part() {
  for (int v = 0; v < kVoices; ++v) {
    voices_.push_back(std::make_unique<Voice>(makeAllSources(), makeAllFilters(), makeAllShapers()));
  }
}
```

in `applySettings`, after `const bool engineChanged = s.engine != settings_.engine;` add

```cpp
  const bool filterChanged = s.filterEngine != settings_.filterEngine;
  const bool shaperChanged = s.shaperEngine != settings_.shaperEngine;
```

and after the `if (engineChanged) {...}` block add

```cpp
  if (filterChanged) {
    for (auto& voice : voices_) voice->setFilterEngine(s.filterEngine);
  }
  if (shaperChanged) {
    for (auto& voice : voices_) voice->setShaperEngine(s.shaperEngine);
  }
```

Replace `Part::process` with:

```cpp
void Part::process(const float* const* in, float* const* out, const ParamValues& source, int n) {
  SlotParams params;
  params.source = source;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Part::process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n) {
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  float* mix[2] = {mix_[0].data(), mix_[1].data()};
  for (int v = 0; v < kVoices; ++v) {
    voices_[size_t(v)]->process(in, mix, params, n);
    if (allocator_.sounding(v) && !voices_[size_t(v)]->active()) allocator_.voiceIdle(v);
  }
  for (int i = 0; i < n; ++i) {
    const float gl = gainL_.next();
    const float gr = gainR_.next();
    if (settings_.mono) {
      const float centre = 0.5f * (mix_[0][size_t(i)] + mix_[1][size_t(i)]) * gl;
      out[0][i] = centre;
      out[1][i] = centre;
    } else {
      out[0][i] = mix_[0][size_t(i)] * gl;
      out[1][i] = mix_[1][size_t(i)] * gr;
    }
  }
}
```

- [ ] **Step 5a: Document the voice chain in the manual**

In `docs/manual/parts-and-routing.md`, at the end of the "Parts and voices" section, add:

````markdown
A voice stops (and becomes free for a new note) when its note is released and the sound reaching its Shaper has been silent (below −90 dBFS) for 50 ms, plus the Shaper's latency. Silence is measured before the Shaper, because the Ring Mod / Vocoder's internal oscillator keeps sounding after the note has died. The amplitude envelopes coming in the next milestone (M4) will close the output itself.
````

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all pass, M2's voice and part tests unchanged. Run `ctest --preset mac -R "Memory per part" -V` and record the printed figures for Task 15. Then the Docker command.

- [ ] **Step 7: Commit**

```bash
git add core/voice/voice.h core/voice/voice.cpp core/part/part.h core/part/part.cpp tests/test_engines.h tests/test_voice.cpp tests/test_part.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Voice: Source -> Filter -> Shaper with side signals and chain latency; parts switch slots"
```

---
### Task 12: Parameter table v4: Filter and Shaper types and parameters

**Files:**
- Manual: `docs/manual/engines.md` (same commit)
- Modify: `core/synth/param_table.h`, `core/synth/param_table.cpp`
- Test: `tests/test_param_table.cpp`

**Interfaces:**
- Consumes: `filterEngineInfo`, `shaperEngineInfo`, `filterEngineFromSelector`, `shaperEngineFromSelector`, `kNumFilterEngines`, `kNumShaperEngines` (Task 10).
- Produces:
  - `constexpr size_t kFilterParamCount = 4; constexpr size_t kShaperParamCount = 12;` `kParamsPerPart = 50`, `kParamCount = 104`.
  - `enum class SlotKind : int { Source = 0, Filter = 1, Shaper = 2 }; constexpr int kNumSlotKinds = 3;`
  - `constexpr size_t slotParamCount(SlotKind)`, `slotEngineParamIndex(SlotKind, int part)`, `slotParamIndex(SlotKind, int part, int p)`, `filterEngineParamIndex(int)`, `filterParamIndex(int, int)`, `shaperEngineParamIndex(int)`, `shaperParamIndex(int, int)`; `sourceEngineParamIndex`, `sourceParamIndex`, `partParamIndex`, `masterParamIndex` keep their signatures.
  - `struct PartEngines { int source = 0; int filter = 0; int shaper = 0; };`
  - `int slotEngineCount(SlotKind)`, `int slotEngineFromSelector(SlotKind, double)`, `const EngineInfo& slotEngineInfo(SlotKind, int)`, `bool slotParamOf(size_t index, SlotKind*, int* part, int* p)`, `bool slotSelectorOf(size_t index, SlotKind*, int* part)`.
  - `formatParam(index, value, buf, size, const PartEngines& = {})` and `paramName(index, const PartEngines& = {})`; the M2 forms taking an `int sourceEngine` remain.
  - `ParamFormat::FilterEngine`, `ParamFormat::ShaperEngine`. Filter and Shaper selectors are named "Type", modules "Part A/Filter", "Part A/Shaper" (and B).
  - Defaults: Filter type Off (instrument), Analog Filter (EG13 FX, Part A); Shaper type Off; their P defaults are the default engine's.

- [ ] **Step 1: Write the failing tests**

In `tests/test_param_table.cpp`, replace the first test case with:

```cpp
TEST_CASE("The table has 104 parameters with unique IDs") {
  REQUIRE(kSourceParamCount == 16);
  REQUIRE(kFilterParamCount == 4);
  REQUIRE(kShaperParamCount == 12);
  REQUIRE(kParamsPerPart == 50);
  REQUIRE(kParamCount == 104);
  std::set<uint32_t> ids;
  for (size_t i = 0; i < kParamCount; ++i) ids.insert(paramSpec(i).id);
  REQUIRE(ids.size() == kParamCount);
  for (size_t i = 0; i < kParamCount; ++i) REQUIRE(paramIndex(paramSpec(i).id) == int(i));
  REQUIRE(paramIndex(0xDEADBEEF) == -1);
}
```

and, in "Source P9..P16 have their own IDs after P1..P8", replace the three layout lines at its end with:

```cpp
  // Layout per part: Source selector + P1..P16, Filter + P1..P4, Shaper + P1..P12, part settings.
  REQUIRE(filterEngineParamIndex(0) == 17);
  REQUIRE(shaperEngineParamIndex(0) == 22);
  REQUIRE(partParamIndex(0, PartParam::Level) == 35);
  REQUIRE(sourceEngineParamIndex(1) == 50);
  REQUIRE(masterParamIndex(MasterParam::Level) == 100);
```

Append (add `#include "engine/engine_registry.h"` and `#include "engine/ripples_engine.h"` at the top):

```cpp
TEST_CASE("Filter and Shaper parameters have permanent IDs") {
  for (int part = 0; part < kNumParts; ++part) {
    const uint32_t p = uint32_t(part);
    REQUIRE(paramSpec(filterEngineParamIndex(part)).id == params::id(p, params::Slot::Filter, 0));
    REQUIRE(paramSpec(shaperEngineParamIndex(part)).id == params::id(p, params::Slot::Shaper, 0));
    for (int i = 0; i < int(kFilterParamCount); ++i) {
      REQUIRE(paramSpec(filterParamIndex(part, i)).id == params::id(p, params::Slot::Filter, uint32_t(i + 1)));
      REQUIRE(paramSpec(filterParamIndex(part, i)).maxValue == 1.0f);
      REQUIRE_FALSE(paramSpec(filterParamIndex(part, i)).stepped);
    }
    for (int i = 0; i < int(kShaperParamCount); ++i) {
      REQUIRE(paramSpec(shaperParamIndex(part, i)).id == params::id(p, params::Slot::Shaper, uint32_t(i + 1)));
    }
    for (size_t selector : {filterEngineParamIndex(part), shaperEngineParamIndex(part)}) {
      REQUIRE(paramSpec(selector).stepped);
      REQUIRE(paramSpec(selector).minValue == 0.0f);
      REQUIRE(paramSpec(selector).maxValue == 31.0f);
      REQUIRE(std::string(paramSpec(selector).name) == "Type");
    }
  }
  REQUIRE(std::string(paramSpec(filterParamIndex(1, 0)).module) == "Part B/Filter");
  REQUIRE(std::string(paramSpec(shaperParamIndex(0, 11)).module) == "Part A/Shaper");
  // Source IDs are unchanged.
  REQUIRE(paramSpec(sourceParamIndex(0, 0)).id == params::id(0, params::Slot::Source, 1));
  REQUIRE(paramSpec(sourceParamIndex(1, 15)).id == params::id(1, params::Slot::Source, 16));
}

TEST_CASE("Filter and Shaper defaults per plugin") {
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(paramDefault(filterEngineParamIndex(part), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(shaperEngineParamIndex(part), PluginKind::Effect) == 0.0f);
  }
  REQUIRE(paramDefault(filterEngineParamIndex(0), PluginKind::Effect) == float(FilterEngine::Analog));
  REQUIRE(paramDefault(filterEngineParamIndex(1), PluginKind::Effect) == 0.0f);
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Effect) ==
          RipplesEngine::staticInfo().params[0].defaultValue);
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Instrument) == 0.0f);  // Off: unused
  REQUIRE(std::string(paramSpec(filterParamIndex(0, 0)).name) == "(unused)");
}

TEST_CASE("Filter and Shaper parameters name and format through their engines") {
  char buf[64];
  const PartEngines analog{0, int(FilterEngine::Analog), int(ShaperEngine::Eq)};
  REQUIRE(std::string(paramName(filterParamIndex(0, 0), analog)) == "Frequency");
  formatParam(filterParamIndex(0, 0), 1.0, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "20.00 kHz");
  REQUIRE(std::string(paramName(filterParamIndex(0, 3), analog)) == "(unused)");
  formatParam(filterParamIndex(0, 3), 0.5, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "-");
  REQUIRE(std::string(paramName(shaperParamIndex(1, 11), analog)) == "Output");
  formatParam(shaperParamIndex(1, 11), 0.5 / 7.0, buf, sizeof(buf), analog);
  REQUIRE(std::string(buf) == "EQ");
  const PartEngines off;
  REQUIRE(std::string(paramName(shaperParamIndex(0, 0), off)) == "(unused)");
  REQUIRE(std::string(paramName(sourceParamIndex(0, 0), off)) == "Model");
  formatParam(filterEngineParamIndex(0), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Analog Filter");
  formatParam(filterEngineParamIndex(0), 3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "State Variable Filter");
  formatParam(filterEngineParamIndex(0), 17, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Off");
  formatParam(shaperEngineParamIndex(0), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Ring Mod / Vocoder");
}

TEST_CASE("Slot lookups find every slot parameter and selector") {
  int counted[kNumSlotKinds] = {};
  for (size_t i = 0; i < kParamCount; ++i) {
    SlotKind kind = SlotKind::Source;
    int part = -1, p = -1;
    if (slotParamOf(i, &kind, &part, &p)) {
      REQUIRE(slotParamIndex(kind, part, p) == i);
      ++counted[int(kind)];
    }
    if (slotSelectorOf(i, &kind, &part)) REQUIRE(slotEngineParamIndex(kind, part) == i);
  }
  REQUIRE(counted[0] == 2 * 16);
  REQUIRE(counted[1] == 2 * 4);
  REQUIRE(counted[2] == 2 * 12);
  for (int e = 0; e < slotEngineCount(SlotKind::Filter); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Filter, e).paramCount <= int(kFilterParamCount));
  }
  for (int e = 0; e < slotEngineCount(SlotKind::Shaper); ++e) {
    REQUIRE(slotEngineInfo(SlotKind::Shaper, e).paramCount <= int(kShaperParamCount));
  }
  REQUIRE(slotEngineFromSelector(SlotKind::Shaper, 2.0) == 2);
  REQUIRE(slotEngineFromSelector(SlotKind::Filter, 9.0) == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`kFilterParamCount`, `filterEngineParamIndex`, `PartEngines`, `slotParamOf`).

- [ ] **Step 3: Replace `core/synth/param_table.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/voice_engine.h"
#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
// Source P1..P16, Filter P1..P4, Shaper P1..P12 per part (spec §7.1; Shaper amended in M3).
// Engines use the first EngineInfo::paramCount; the rest are named "(unused)" and show "-".
constexpr size_t kSourceParamCount = 16;
constexpr size_t kFilterParamCount = 4;
constexpr size_t kShaperParamCount = 12;

enum class SlotKind : int { Source = 0, Filter = 1, Shaper = 2 };
constexpr int kNumSlotKinds = 3;

// Value = parameter-ID index within Slot::Part. Never renumber.
enum class PartParam : uint32_t {
  Level = 1,
  Pan,
  Glide,
  Transpose,
  BaseNote,
  MidiChannel,
  KeyLow,
  KeyHigh,
  VoiceMode,
  VoiceCount,
  Trigger,
  BendRange,
  Mono,
  Output,
  InputSource,
};
constexpr size_t kPartParamCount = 15;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote };
constexpr size_t kMasterParamCount = 4;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

// Per part: Source selector + P1..P16, Filter selector + P1..P4, Shaper selector + P1..P12,
// then the part settings. Table order only: saved state uses parameter IDs.
constexpr size_t kParamsPerPart =
    3 + kSourceParamCount + kFilterParamCount + kShaperParamCount + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

constexpr size_t slotParamCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kSourceParamCount
         : kind == SlotKind::Filter ? kFilterParamCount
                                    : kShaperParamCount;
}
// Where a slot's selector sits within a part's block.
constexpr size_t slotOffset(SlotKind kind) {
  return kind == SlotKind::Source   ? 0
         : kind == SlotKind::Filter ? 1 + kSourceParamCount
                                    : 2 + kSourceParamCount + kFilterParamCount;
}
constexpr size_t slotEngineParamIndex(SlotKind kind, int part) {
  return size_t(part) * kParamsPerPart + slotOffset(kind);
}
constexpr size_t slotParamIndex(SlotKind kind, int part, int p) {
  return slotEngineParamIndex(kind, part) + 1 + size_t(p);
}
constexpr size_t sourceEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Source, part); }
constexpr size_t sourceParamIndex(int part, int p) { return slotParamIndex(SlotKind::Source, part, p); }
constexpr size_t filterEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Filter, part); }
constexpr size_t filterParamIndex(int part, int p) { return slotParamIndex(SlotKind::Filter, part, p); }
constexpr size_t shaperEngineParamIndex(int part) { return slotEngineParamIndex(SlotKind::Shaper, part); }
constexpr size_t shaperParamIndex(int part, int p) { return slotParamIndex(SlotKind::Shaper, part, p); }
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + 3 + kSourceParamCount + kFilterParamCount + kShaperParamCount +
         size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,  // a Source/Filter/Shaper P, formatted by the slot's selected engine
  Percent,
  Pan,
  GlideTime,
  Semitones,
  BendRange,
  Note,
  Channel,
  VoiceMode,
  VoiceCount,
  Trigger,
  OnOff,
  OutputBus,
  PartMode,
  SourceEngine,
  InputSource,
  FilterEngine,
  ShaperEngine,
};

struct ParamSpec {
  uint32_t id;
  const char* name;
  const char* module;
  float minValue;
  float maxValue;
  float defaultValue;   // instrument
  float effectDefault;  // EG13 FX
  bool stepped;
  ParamFormat format;
};

// The engine selected in each slot of a part, for engine-aware names and text.
struct PartEngines {
  int source = 0;
  int filter = 0;
  int shaper = 0;
};

int slotEngineCount(SlotKind kind);
int slotEngineFromSelector(SlotKind kind, double value);  // unknown -> 0 (Macro / Off / Off)
const EngineInfo& slotEngineInfo(SlotKind kind, int engine);
// If index is a Source/Filter/Shaper P: its slot, part and P (0-based).
bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p);
// If index is a slot's engine selector: its slot and part.
bool slotSelectorOf(size_t index, SlotKind* kind, int* part);

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
// engines: the part's selected engines, used for Source/Filter/Shaper P names and text.
void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines = {});
void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine);
const char* paramName(size_t index, const PartEngines& engines = {});
const char* paramName(size_t index, int sourceEngine);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/synth/param_table.cpp`**

```cpp
#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/engine_registry.h"
#include "params/param_ids.h"

namespace eg13 {

static_assert(kSourceParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Source P");
static_assert(kShaperParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Shaper P");

namespace {

const char* const kSlotModules[kNumSlotKinds][kNumParts] = {
    {"Part A/Source", "Part B/Source"},
    {"Part A/Filter", "Part B/Filter"},
    {"Part A/Shaper", "Part B/Shaper"},
};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};
const SlotKind kSlotKinds[kNumSlotKinds] = {SlotKind::Source, SlotKind::Filter, SlotKind::Shaper};

struct PartParamDef {
  PartParam param;
  const char* name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool stepped;
  ParamFormat format;
};

const PartParamDef kPartDefs[kPartParamCount] = {
    {PartParam::Level, "Level", 0, 1, 1, false, ParamFormat::Percent},
    {PartParam::Pan, "Pan", -1, 1, 0, false, ParamFormat::Pan},
    {PartParam::Glide, "Glide", 0, 1, 0, false, ParamFormat::GlideTime},
    {PartParam::Transpose, "Transpose", -24, 24, 0, true, ParamFormat::Semitones},
    {PartParam::BaseNote, "Base Note", 0, 127, 60, true, ParamFormat::Note},
    {PartParam::MidiChannel, "MIDI Channel", 0, 16, 0, true, ParamFormat::Channel},
    {PartParam::KeyLow, "Key Low", 0, 127, 0, true, ParamFormat::Note},
    {PartParam::KeyHigh, "Key High", 0, 127, 127, true, ParamFormat::Note},
    {PartParam::VoiceMode, "Voice Mode", 0, 2, 0, true, ParamFormat::VoiceMode},
    {PartParam::VoiceCount, "Voices", 1, 8, 8, true, ParamFormat::VoiceCount},
    {PartParam::Trigger, "Trigger", 0, 1, 0, true, ParamFormat::Trigger},
    {PartParam::BendRange, "Bend Range", 0, 24, 2, true, ParamFormat::BendRange},
    {PartParam::Mono, "Mono", 0, 1, 0, true, ParamFormat::OnOff},
    {PartParam::Output, "Output", 0, 2, 0, true, ParamFormat::OutputBus},
    {PartParam::InputSource, "Input Source", 0, 2, 0, true, ParamFormat::InputSource},
};

params::Slot idSlot(SlotKind kind) {
  return kind == SlotKind::Source   ? params::Slot::Source
         : kind == SlotKind::Filter ? params::Slot::Filter
                                    : params::Slot::Shaper;
}

// Default engine per slot, part and plugin. EG13 FX's Part A is its init patch's chain (spec §6:
// Audio Input -> Analog Filter); everything else starts on the slot's first engine (Macro
// Oscillator, Off, Off). The instrument init patch arrives with modulation in M4.
int defaultEngine(SlotKind kind, int part, PluginKind plugin) {
  if (plugin == PluginKind::Effect && part == 0) {
    if (kind == SlotKind::Source) return int(SourceEngine::AudioInput);
    if (kind == SlotKind::Filter) return int(FilterEngine::Analog);
  }
  return 0;
}

int engineFor(SlotKind kind, const PartEngines& e) {
  return kind == SlotKind::Source ? e.source : kind == SlotKind::Filter ? e.filter : e.shaper;
}

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    for (int part = 0; part < kNumParts; ++part) {
      for (SlotKind kind : kSlotKinds) {
        const int instrument = defaultEngine(kind, part, PluginKind::Instrument);
        const int effect = defaultEngine(kind, part, PluginKind::Effect);
        const ParamFormat selectorFormat = kind == SlotKind::Source   ? ParamFormat::SourceEngine
                                           : kind == SlotKind::Filter ? ParamFormat::FilterEngine
                                                                      : ParamFormat::ShaperEngine;
        const float selectorMax = float(kind == SlotKind::Source   ? kSourceEngineSelectorMax
                                        : kind == SlotKind::Filter ? kFilterEngineSelectorMax
                                                                   : kShaperEngineSelectorMax);
        const char* module = kSlotModules[int(kind)][part];
        t[slotEngineParamIndex(kind, part)] = {params::id(uint32_t(part), idSlot(kind), 0),
                                               kind == SlotKind::Source ? "Engine" : "Type",
                                               module, 0.0f, selectorMax, float(instrument), float(effect),
                                               true, selectorFormat};
        const EngineInfo& inst = slotEngineInfo(kind, instrument);
        const EngineInfo& fx = slotEngineInfo(kind, effect);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          const bool used = p < inst.paramCount;
          t[slotParamIndex(kind, part, p)] = {params::id(uint32_t(part), idSlot(kind), uint32_t(p + 1)),
                                              used ? inst.params[p].name : "(unused)", module, 0.0f, 1.0f,
                                              used ? inst.params[p].defaultValue : 0.0f,
                                              p < fx.paramCount ? fx.params[p].defaultValue : 0.0f, false,
                                              ParamFormat::Engine};
        }
      }
      for (const PartParamDef& d : kPartDefs) {
        t[partParamIndex(part, d.param)] = {params::id(uint32_t(part), params::Slot::Part, uint32_t(d.param)),
                                            d.name, kPartModules[part], d.minValue, d.maxValue,
                                            d.defaultValue, d.defaultValue, d.stepped, d.format};
      }
    }
    t[partParamIndex(0, PartParam::Trigger)].effectDefault = 1.0f;  // EG13 FX: Part A always on
    auto master = [](MasterParam m) {
      return params::id(params::kMasterPart, params::Slot::Part, uint32_t(m));
    };
    t[masterParamIndex(MasterParam::Level)] = {master(MasterParam::Level), "Level", "Master", 0, 1,
                                               0.8f, 1.0f, false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Dry)] = {master(MasterParam::Dry), "Dry", "Master", 0, 1, 0, 0,
                                             false, ParamFormat::Percent};
    t[masterParamIndex(MasterParam::Mode)] = {master(MasterParam::Mode), "Mode", "Master", 0, 3, 0, 0,
                                              true, ParamFormat::PartMode};
    t[masterParamIndex(MasterParam::SplitNote)] = {master(MasterParam::SplitNote), "Split Note",
                                                   "Master", 0, 127, 60, 60, true, ParamFormat::Note};
    return t;
  }();
  return specs;
}

const char* pick(const char* const* names, int count, float value) {
  const int i = std::clamp(int(std::lround(value)), 0, count - 1);
  return names[i];
}

}  // namespace

int slotEngineCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kNumSourceEngines
         : kind == SlotKind::Filter ? kNumFilterEngines
                                    : kNumShaperEngines;
}

int slotEngineFromSelector(SlotKind kind, double value) {
  return kind == SlotKind::Source   ? sourceEngineFromSelector(value)
         : kind == SlotKind::Filter ? filterEngineFromSelector(value)
                                    : shaperEngineFromSelector(value);
}

const EngineInfo& slotEngineInfo(SlotKind kind, int engine) {
  return kind == SlotKind::Source   ? sourceEngineInfo(engine)
         : kind == SlotKind::Filter ? filterEngineInfo(engine)
                                    : shaperEngineInfo(engine);
}

bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (SlotKind k : kSlotKinds) {
    const size_t first = slotOffset(k) + 1;
    if (local >= first && local < first + slotParamCount(k)) {
      *kind = k;
      *part = int(index / kParamsPerPart);
      *p = int(local - first);
      return true;
    }
  }
  return false;
}

bool slotSelectorOf(size_t index, SlotKind* kind, int* part) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (SlotKind k : kSlotKinds) {
    if (local == slotOffset(k)) {
      *kind = k;
      *part = int(index / kParamsPerPart);
      return true;
    }
  }
  return false;
}

const ParamSpec& paramSpec(size_t index) { return table()[index]; }

int paramIndex(uint32_t id) {
  const auto& t = table();
  for (size_t i = 0; i < kParamCount; ++i) {
    if (t[i].id == id) return int(i);
  }
  return -1;
}

float paramDefault(size_t index, PluginKind kind) {
  const ParamSpec& s = paramSpec(index);
  return kind == PluginKind::Effect ? s.effectDefault : s.defaultValue;
}

float clampParam(size_t index, double value) {
  const ParamSpec& s = paramSpec(index);
  if (std::isnan(value)) return s.minValue;
  double v = std::clamp(value, double(s.minValue), double(s.maxValue));
  if (s.stepped) v = std::round(v);
  return float(v);
}

float glideSeconds(float normalized) { return 2.0f * normalized * normalized; }

void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine) {
  PartEngines engines;
  engines.source = sourceEngine;
  formatParam(index, value, buf, size, engines);
}

void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines) {
  static const char* const kNotes[12] = {"C", "C#", "D", "D#", "E", "F",
                                         "F#", "G", "G#", "A", "A#", "B"};
  static const char* const kVoiceModes[3] = {"Poly", "Mono", "Legato"};
  static const char* const kTriggers[2] = {"MIDI", "Always on"};
  static const char* const kOnOff[2] = {"Off", "On"};
  static const char* const kOutputs[3] = {"Main", "Bus 2", "Other part"};
  static const char* const kModes[4] = {"Single", "Layer", "Split", "Multi"};
  static const char* const kInputSources[3] = {"External", "Other part", "Both"};

  const ParamSpec& s = paramSpec(index);
  const float v = clampParam(index, value);
  switch (s.format) {
    case ParamFormat::Engine: {
      SlotKind kind = SlotKind::Source;
      int part = 0, p = 0;
      slotParamOf(index, &kind, &part, &p);
      const EngineInfo& info = slotEngineInfo(kind, engineFor(kind, engines));
      if (p < info.paramCount && info.params[p].format != nullptr) {
        info.params[p].format(v, buf, size);
      } else {
        std::snprintf(buf, size, "-");
      }
      break;
    }
    case ParamFormat::SourceEngine:
      std::snprintf(buf, size, "%s", sourceEngineInfo(sourceEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::FilterEngine:
      std::snprintf(buf, size, "%s", filterEngineInfo(filterEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::ShaperEngine:
      std::snprintf(buf, size, "%s", shaperEngineInfo(shaperEngineFromSelector(v)).displayName);
      break;
    case ParamFormat::InputSource:
      std::snprintf(buf, size, "%s", pick(kInputSources, 3, v));
      break;
    case ParamFormat::Percent:
      std::snprintf(buf, size, "%d%%", int(std::lround(v * 100.0f)));
      break;
    case ParamFormat::Pan:
      if (v < -0.005f) {
        std::snprintf(buf, size, "L%d", int(std::lround(-v * 100.0f)));
      } else if (v > 0.005f) {
        std::snprintf(buf, size, "R%d", int(std::lround(v * 100.0f)));
      } else {
        std::snprintf(buf, size, "C");
      }
      break;
    case ParamFormat::GlideTime: {
      const float seconds = glideSeconds(v);
      if (seconds <= 0.0f) {
        std::snprintf(buf, size, "Off");
      } else if (seconds < 1.0f) {
        std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
      } else {
        std::snprintf(buf, size, "%.2f s", double(seconds));
      }
      break;
    }
    case ParamFormat::Semitones:
      std::snprintf(buf, size, "%+d st", int(v));
      break;
    case ParamFormat::BendRange:
      std::snprintf(buf, size, "+/-%d st", int(v));
      break;
    case ParamFormat::Note: {
      const int note = int(v);
      std::snprintf(buf, size, "%s%d", kNotes[note % 12], note / 12 - 1);
      break;
    }
    case ParamFormat::Channel:
      if (int(v) == 0) {
        std::snprintf(buf, size, "Omni");
      } else {
        std::snprintf(buf, size, "%d", int(v));
      }
      break;
    case ParamFormat::VoiceMode:
      std::snprintf(buf, size, "%s", pick(kVoiceModes, 3, v));
      break;
    case ParamFormat::VoiceCount:
      std::snprintf(buf, size, "%d", int(v));
      break;
    case ParamFormat::Trigger:
      std::snprintf(buf, size, "%s", pick(kTriggers, 2, v));
      break;
    case ParamFormat::OnOff:
      std::snprintf(buf, size, "%s", pick(kOnOff, 2, v));
      break;
    case ParamFormat::OutputBus:
      std::snprintf(buf, size, "%s", pick(kOutputs, 3, v));
      break;
    case ParamFormat::PartMode:
      std::snprintf(buf, size, "%s", pick(kModes, 4, v));
      break;
  }
}

const char* paramName(size_t index, int sourceEngine) {
  PartEngines engines;
  engines.source = sourceEngine;
  return paramName(index, engines);
}

const char* paramName(size_t index, const PartEngines& engines) {
  SlotKind kind = SlotKind::Source;
  int part = 0, p = 0;
  if (!slotParamOf(index, &kind, &part, &p)) return paramSpec(index).name;
  const EngineInfo& info = slotEngineInfo(kind, engineFor(kind, engines));
  return p < info.paramCount ? info.params[p].name : "(unused)";
}

}  // namespace eg13
```

- [ ] **Step 4a: Document the slot parameters in the manual**

In `docs/manual/engines.md`, replace the second sentence of the introduction ("A slot has more parameters ... The Source slot has 16 parameters.") with:

````markdown
A slot has more parameters than most engines use: the unused ones are named "(unused)" and show "-". The Source slot has 16 parameters, the Filter slot 4 and the Shaper slot 12, plus a selector for the engine in each slot (the Filter's and Shaper's are called *Type*).
````

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all tests pass. The Synth does not read the new selectors until Task 13, so its M2 behaviour, including EG13 FX's exact pass-through tests, is unchanged here; its calls to `paramName`/`formatParam` with an `int` engine use the M2 overloads.

- [ ] **Step 6: Commit**

```bash
git add core/synth/param_table.h core/synth/param_table.cpp tests/test_param_table.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Parameter table v4: Filter type + P1-P4 and Shaper type + P1-P12 per part"
```

---
### Task 13: Synth v4: three-slot stash and switching, modulator sources, FX filter

**Files:**
- Manual: `docs/manual/parts-and-routing.md` (same commit)
- Modify: `core/synth/synth.h`, `core/synth/synth.cpp`
- Test: `tests/test_synth.cpp`

**Interfaces:**
- Consumes: `SlotKind`, slot index helpers, `PartEngines`, `slotSelectorOf`, `slotEngineFromSelector`, `slotEngineInfo` (Task 12); `VoiceInputs`, `SlotParams`, `PartSettings::filterEngine/shaperEngine` (Task 11).
- Produces:
  - `kSourceStashSize`, `kFilterStashSize`, `kShaperStashSize`; `kStashSize` = their sum; `stashIndex(part, engine, p)` (Source, unchanged); `constexpr size_t slotStashIndex(SlotKind, int part, int engine, int p)`.
  - `kMaxHostUpdates = kNumParts * (16 + 4 + 12)` = 64.
  - `int Synth::engine(SlotKind kind, int part) const` (main thread); `engine(int part)` stays (Source).
  - A selector change for any slot through `setParam` swaps that slot's P values with the stash and queues host updates (as M2 does for Source). `paramName`/`formatParam` follow all three of a part's engines.
  - Each chunk, every part's voices get `VoiceInputs{routed Input (nullable), the other part's output (soft-limited)}` and `SlotParams` for all three slots.

- [ ] **Step 1: Write the failing tests**

In `tests/test_synth.cpp`, add `#include <utility>` and `#include "engine/warps_engine.h"` to the includes, and add to the anonymous namespace near the top (after `masterId`):

```cpp
uint32_t filterTypeId(int part) { return paramSpec(filterEngineParamIndex(part)).id; }
uint32_t shaperTypeId(int part) { return paramSpec(shaperEngineParamIndex(part)).id; }
uint32_t filterId(int part, int p) { return paramSpec(filterParamIndex(part, p)).id; }
uint32_t shaperId(int part, int p) { return paramSpec(shaperParamIndex(part, p)).id; }

// The exact pass-through tests below predate the Filter: they run it Off.
void filtersOff(Synth& synth) {
  for (int part = 0; part < kNumParts; ++part) synth.setParam(filterTypeId(part), 0);
}
```

Add `filtersOff(synth);` right after the `Synth synth(PluginKind::Effect);` line (before `prepare`) in these existing tests: "EG13 FX init patch passes audio at unity through Part A" (and rename it to "EG13 FX with the Filter Off passes audio at unity through Part A"), "FX: processed input is aligned with Dry", "Part B can take Part A as its input", and the helper `checkFeedDelay`.

Append:

```cpp
TEST_CASE("EG13 FX init patch runs Audio Input through the open Analog Filter") {
  Synth synth(PluginKind::Effect);
  REQUIRE(synth.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
  REQUIRE(synth.engine(SlotKind::Filter, 1) == int(FilterEngine::Off));
  REQUIRE(synth.engine(SlotKind::Shaper, 0) == int(ShaperEngine::Off));
  synth.prepare(48000.0, kBlock);
  const size_t latency = size_t(synth.latencySamples());
  const Stereo in = sineInput(48000, 48000.0);
  Stereo out(48000);
  run(synth, out, 0, 48000, &in);
  double dot = 0.0;
  for (size_t i = 9600; i < 48000; ++i) dot += double(out.l[i]) * double(in.l[i - latency]);
  REQUIRE(dot > 0.0);
  const double gainDb = 20.0 * std::log10(test::rms(out.l, 9600, 48000) / test::rms(in.l, 9600, 48000));
  INFO("open Analog Filter gain " << gainDb << " dB");
  REQUIRE(std::fabs(gainDb) < 1.0);
}

TEST_CASE("Switching Filter and Shaper types swaps their P values and reports them") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  runBlocks(synth, 1);
  HostUpdate updates[kMaxHostUpdates];
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kFilterParamCount));
  REQUIRE(updates[0].id == filterId(0, 0));
  REQUIRE(updates[0].value == filterEngineInfo(int(FilterEngine::Analog)).params[0].defaultValue);
  REQUIRE(synth.takeEngineChanged());
  synth.setParam(filterId(0, 0), 0.3);
  synth.setParam(filterTypeId(0), double(FilterEngine::Classic));
  runBlocks(synth, 1);
  REQUIRE(synth.param(filterId(0, 0)) == double(filterEngineInfo(int(FilterEngine::Classic)).params[0].defaultValue));
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  runBlocks(synth, 1);
  REQUIRE(synth.param(filterId(0, 0)) == double(0.3f));

  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  synth.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kShaperParamCount));
  REQUIRE(updates[11].id == shaperId(1, 11));
  synth.setParam(shaperId(1, 10), 0.9);  // EQ Input Pad on
  synth.setParam(shaperTypeId(1), double(ShaperEngine::RingMod));
  synth.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  REQUIRE(synth.param(shaperId(1, 10)) == double(0.9f));
  synth.setActive(false);
}

TEST_CASE("Filter and Shaper names and text follow their engines") {
  Synth synth(PluginKind::Instrument);
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "(unused)");
  synth.setParam(filterTypeId(0), double(FilterEngine::StateVariable));
  synth.setParam(shaperTypeId(0), double(ShaperEngine::Eq));
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Cutoff");
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 2))) == "Mode");
  REQUIRE(std::string(synth.paramName(shaperParamIndex(0, 0))) == "High Freq");
  REQUIRE(std::string(synth.paramName(shaperParamIndex(1, 0))) == "(unused)");
  char buf[64];
  synth.formatParam(filterParamIndex(0, 2), 2.5 / 3.0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "HP");
  synth.formatParam(filterEngineParamIndex(0), 3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "State Variable Filter");
}

TEST_CASE("Switching Filter type keeps the output aligned in time") {
  auto onset = [](int filter, int shaper) {
    Synth synth(PluginKind::Effect);
    synth.setParam(filterTypeId(0), filter);
    synth.setParam(shaperTypeId(0), shaper);
    synth.prepare(48000.0, kBlock);
    Stereo in(9600);
    in.l[4000] = in.r[4000] = 0.25f;
    Stereo out(9600);
    run(synth, out, 0, 9600, &in);
    size_t peak = 0;
    for (size_t i = 0; i < out.l.size(); ++i) {
      if (std::fabs(out.l[i]) > std::fabs(out.l[peak])) peak = i;
    }
    return std::make_pair(synth.latencySamples(), int(peak) - 4000);
  };
  const auto off = onset(int(FilterEngine::Off), int(ShaperEngine::Off));
  REQUIRE(off.second == off.first);  // Off and Off: exactly the reported latency
  for (int filter : {int(FilterEngine::Analog), int(FilterEngine::Classic), int(FilterEngine::StateVariable)}) {
    const auto with = onset(filter, int(ShaperEngine::Off));
    INFO("filter " << filter << " latency " << with.first << " onset " << with.second);
    REQUIRE(with.first == off.first);  // the reported latency does not change with the type
    // Only the filter's own group delay moves the peak: the Analog Filter's oversampling
    // filters delay it by ~6 samples (measured during planning), as in VCV.
    REQUIRE(std::abs(with.second - off.second) <= 8);
  }
  const auto eq = onset(int(FilterEngine::Off), int(ShaperEngine::Eq));
  REQUIRE(eq.first == off.first);
  REQUIRE(std::abs(eq.second - off.second) <= 8);
}

TEST_CASE("Switching Filter type while audio plays makes no step") {
  Synth synth(PluginKind::Effect);
  synth.setParam(filterTypeId(0), 0);
  synth.prepare(48000.0, kBlock);
  synth.setActive(true);
  const Stereo in = sineInput(24000, 48000.0);
  Stereo out(24000);
  run(synth, out, 0, 9600, &in);
  synth.setParam(filterTypeId(0), double(FilterEngine::StateVariable));  // open: nearly transparent
  run(synth, out, 9600, 24000, &in);
  float maxStep = 0.0f;
  for (size_t i = 4800; i < 24000; ++i) maxStep = std::max(maxStep, std::fabs(out.l[i] - out.l[i - 1]));
  REQUIRE(maxStep < 0.05f);  // a 220 Hz, 0.5 sine moves at most 0.0144 per sample
  synth.setActive(false);
}

TEST_CASE("The Ring Mod modulator takes the part's Input and the other part's output") {
  // Carrier = Voice (Part A's Audio Input), so the Modulator choice decides what multiplies it.
  auto render = [](float modulator, bool partBPlays) {
    Synth synth(PluginKind::Effect);
    synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
    filtersOff(synth);
    synth.setParam(shaperTypeId(0), double(ShaperEngine::RingMod));
    synth.setParam(shaperId(0, 0), 0.375);  // digital ring modulation
    synth.setParam(shaperId(0, 1), 0.0);
    synth.setParam(shaperId(0, 2), 0.5);
    synth.setParam(shaperId(0, 3), 0.5);
    synth.setParam(shaperId(0, 4), WarpsEngine::optionValue(1));  // Carrier: Voice (Part A's input)
    synth.setParam(shaperId(0, 5), modulator);
    if (partBPlays) {  // Part B: the input through Audio Input, heard only by Part A
      synth.setParam(paramSpec(sourceEngineParamIndex(1)).id, 4);
      synth.setParam(partId(1, PartParam::Trigger), 1);
      synth.setParam(partId(1, PartParam::Output), 2);
    }
    synth.prepare(48000.0, kBlock);
    Stereo in(48000);
    for (size_t i = 0; i < in.l.size(); ++i) {
      in.l[i] = in.r[i] = 0.4f * float(std::sin(6.283185307179586 * 1000.0 * double(i) / 48000.0));
    }
    Stereo out(48000);
    run(synth, out, 0, 48000, &in);
    for (float s : out.l) REQUIRE(std::isfinite(s));
    return out.l;
  };
  auto magnitude = [](const std::vector<float>& x, double hz) {
    const double c = 2.0 * std::cos(6.283185307179586 * hz / 48000.0);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 9600; i < x.size(); ++i) {
      const double s0 = double(x[i]) + c * s1 - s2;
      s2 = s1;
      s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2));
  };
  const auto input = render(WarpsEngine::optionValue(2), false);  // Input: the carrier times itself
  REQUIRE(magnitude(input, 2000.0) > 10.0 * magnitude(input, 1000.0));
  const auto other = render(WarpsEngine::optionValue(3), true);  // Other part: Part B's copy of the input
  REQUIRE(magnitude(other, 2000.0) > 10.0 * magnitude(other, 1000.0));
  const auto silentB = render(WarpsEngine::optionValue(3), false);  // Other part, but Part B plays nothing
  REQUIRE(test::rms(silentB, 9600, silentB.size()) < 1e-4);
}

TEST_CASE("Filter and Shaper switching while playing does not allocate") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  Stereo in = sineInput(kBlock, kRate), out(kBlock);
  run(synth, out, 0, kBlock, &in);
  synth.noteOn(0, 60, 0.8f);
  HostUpdate updates[kMaxHostUpdates];
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int block = 0; block < 120; ++block) {
      if (block % 10 == 0) {
        synth.setParam(filterTypeId(block % 20 == 0 ? 0 : 1), (block / 10) % kNumFilterEngines);
        synth.setParam(shaperTypeId(0), (block / 10) % kNumShaperEngines);
        synth.setParam(shaperId(0, 4), WarpsEngine::optionValue((block / 10) % WarpsEngine::kNumCarriers));
        synth.setParam(shaperId(0, 5), WarpsEngine::optionValue((block / 10) % WarpsEngine::kNumModulators));
      }
      run(synth, out, 0, kBlock, &in);
      synth.takeHostUpdates(updates, int(kMaxHostUpdates));
      synth.takeEngineChanged();
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  synth.setActive(false);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`Synth::engine(SlotKind, int)` does not exist).

- [ ] **Step 3: Update `core/synth/synth.h`**

Replace the stash and host-update block (from `// Every engine's remembered P1..P16, per part.` to the `kMaxHostUpdates` line) with:

```cpp
// Every engine's remembered parameters, per part and slot: Source engines' P1..P16 first (M2's
// layout, so stashIndex() is unchanged), then Filter engines' P1..P4, then Shaper engines'
// P1..P12.
constexpr size_t kSourceStashSize = size_t(kNumParts) * size_t(kNumSourceEngines) * kSourceParamCount;
constexpr size_t kFilterStashSize = size_t(kNumParts) * size_t(kNumFilterEngines) * kFilterParamCount;
constexpr size_t kShaperStashSize = size_t(kNumParts) * size_t(kNumShaperEngines) * kShaperParamCount;
constexpr size_t kStashSize = kSourceStashSize + kFilterStashSize + kShaperStashSize;
constexpr size_t stashIndex(int part, int engine, int p) {
  return (size_t(part) * size_t(kNumSourceEngines) + size_t(engine)) * kSourceParamCount + size_t(p);
}
constexpr size_t slotStashIndex(SlotKind kind, int part, int engine, int p) {
  return kind == SlotKind::Source ? stashIndex(part, engine, p)
         : kind == SlotKind::Filter
             ? kSourceStashSize +
                   (size_t(part) * size_t(kNumFilterEngines) + size_t(engine)) * kFilterParamCount + size_t(p)
             : kSourceStashSize + kFilterStashSize +
                   (size_t(part) * size_t(kNumShaperEngines) + size_t(engine)) * kShaperParamCount + size_t(p);
}
using EngineStash = std::array<float, kStashSize>;

struct HostUpdate {
  uint32_t id;
  float value;
};
// Host-update queue capacity: one entry per slot P per part (coalesced per id).
constexpr size_t kMaxHostUpdates = size_t(kNumParts) * (kSourceParamCount + kFilterParamCount + kShaperParamCount);
```

In the class, replace `int engine(int part) const;       // main thread` with:

```cpp
  int engine(int part) const { return engine(SlotKind::Source, part); }  // main thread
  int engine(SlotKind kind, int part) const;                               // main thread
```

replace the comment above `takeHostUpdates` mentioning "P1..P16" with "A selector change through setParam switches at once: that slot's P values go to the old engine's stash entry and the new engine's entry is loaded", and in the private section replace `void switchEngine(int part, int next);  // from setParam, before storing the selector` and `std::array<int, kNumParts> currentEngine_{};  // setParam's thread` with:

```cpp
  void switchEngine(SlotKind kind, int part, int next);  // from setParam, before storing the selector
  void adoptEngines(const ParamSnapshot& values);        // currentEngine_ from a loaded snapshot
  PartEngines partEngines(int part) const;               // main thread
  void feedOther(int part, int n);  // otherIn_[part]: the other part's output, soft-limited
```

```cpp
  std::array<std::array<int, kNumParts>, kNumSlotKinds> currentEngine_{};  // setParam's thread
  std::vector<float> otherIn_[kNumParts][2];
```

- [ ] **Step 4: Update `core/synth/synth.cpp`**

In `prepare`, after the `partIn_` loop, add:

```cpp
  for (auto& part : otherIn_) {
    for (auto& buffer : part) buffer.assign(size_t(maxBlock), 0.0f);
  }
```

Replace `setParam`'s selector loop

```cpp
  for (int part = 0; part < kNumParts; ++part) {
    if (size_t(index) == sourceEngineParamIndex(part)) switchEngine(part, sourceEngineFromSelector(clamped));
  }
```

with:

```cpp
  SlotKind kind = SlotKind::Source;
  int part = 0;
  if (slotSelectorOf(size_t(index), &kind, &part)) {
    switchEngine(kind, part, slotEngineFromSelector(kind, clamped));
  }
```

Replace `defaultStash` with:

```cpp
EngineStash Synth::defaultStash() {
  EngineStash stash{};
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      for (int e = 0; e < slotEngineCount(kind); ++e) {
        const EngineInfo& info = slotEngineInfo(kind, e);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          stash[slotStashIndex(kind, part, e, p)] = p < info.paramCount ? info.params[p].defaultValue : 0.0f;
        }
      }
    }
  }
  return stash;
}

void Synth::adoptEngines(const ParamSnapshot& values) {
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      currentEngine_[size_t(k)][size_t(part)] =
          slotEngineFromSelector(kind, values[slotEngineParamIndex(kind, part)]);
    }
  }
}
```

In `loadParams`, replace the inactive-path loop that sets `currentEngine_` with `adoptEngines(clamped);`. In `applyPending`, replace its `currentEngine_` loop with `adoptEngines(pendingValues_);`.

Replace `engineStash`, `engine`, `paramName` and `formatParam` with:

```cpp
EngineStash Synth::engineStash() const {  // main thread
  EngineStash s{};
  const bool pending = hasPending();
  for (size_t i = 0; i < kStashSize; ++i) {
    s[i] = pending ? pendingStash_[i] : stash_[i].load(std::memory_order_relaxed);
  }
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const SlotKind kind = SlotKind(k);
    for (int part = 0; part < kNumParts; ++part) {
      const int e = engine(kind, part);
      for (int p = 0; p < int(slotParamCount(kind)); ++p) {
        s[slotStashIndex(kind, part, e, p)] = float(param(paramSpec(slotParamIndex(kind, part, p)).id));
      }
    }
  }
  return s;
}

int Synth::engine(SlotKind kind, int part) const {
  return slotEngineFromSelector(kind, param(paramSpec(slotEngineParamIndex(kind, part)).id));
}

PartEngines Synth::partEngines(int part) const {
  PartEngines e;
  e.source = engine(SlotKind::Source, part);
  e.filter = engine(SlotKind::Filter, part);
  e.shaper = engine(SlotKind::Shaper, part);
  return e;
}

const char* Synth::paramName(size_t index) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  return eg13::paramName(index, partEngines(part));
}

void Synth::formatParam(size_t index, double value, char* buf, size_t size) const {
  const int part = index < size_t(kNumParts) * kParamsPerPart ? int(index / kParamsPerPart) : 0;
  eg13::formatParam(index, value, buf, size, partEngines(part));
}
```

Replace `switchEngine` with:

```cpp
// Same threading as setParam. Saves the old engine's P values, loads the new engine's and queues
// them for the host.
void Synth::switchEngine(SlotKind kind, int part, int next) {
  int& current = currentEngine_[size_t(kind)][size_t(part)];
  const int previous = current;
  if (next == previous) return;
  for (int p = 0; p < int(slotParamCount(kind)); ++p) {
    const size_t index = slotParamIndex(kind, part, p);
    stash_[slotStashIndex(kind, part, previous, p)].store(load(index), std::memory_order_relaxed);
    const float value = stash_[slotStashIndex(kind, part, next, p)].load(std::memory_order_relaxed);
    values_[index].store(value, std::memory_order_relaxed);
    queueHostUpdate({paramSpec(index).id, value});
  }
  current = next;
  engineChanged_.store(true, std::memory_order_release);
}
```

Replace `routeInput` with these two functions:

```cpp
// The other part's output for this chunk, soft-limited (spec §2.3): A takes B's output delayed
// by exactly feedDelay_ samples (whatever the block sizes); B takes A's current chunk.
void Synth::feedOther(int part, int n) {
  for (int ch = 0; ch < 2; ++ch) {
    float* dst = otherIn_[part][ch].data();
    for (int i = 0; i < n; ++i) {
      const float feed = part == 0 ? feedRing_[ch][(feedPos_ + size_t(i)) % feedRing_[ch].size()]
                                   : partOut_[0][ch][size_t(i)];
      dst[i] = softLimit(feed);
    }
  }
}

// The part's Input by its Input source setting: External = plugin input; Other part = the
// soft-limited other-part feed; Both = their sum.
void Synth::routeInput(int part, const float* const* in, int n) {
  const int source = std::clamp(parts_[size_t(part)]->settings().inputSource, 0, 2);
  const bool external = (source == 0 || source == 2) && in != nullptr;
  const bool other = source >= 1;
  for (int ch = 0; ch < 2; ++ch) {
    float* dst = partIn_[part][ch].data();
    const float* feed = otherIn_[part][ch].data();
    for (int i = 0; i < n; ++i) dst[i] = (external ? in[ch][i] : 0.0f) + (other ? feed[i] : 0.0f);
  }
}
```

In `readPartSettings`, after `s.engine = ...;` add:

```cpp
  s.filterEngine = slotEngineFromSelector(SlotKind::Filter, load(filterEngineParamIndex(part)));
  s.shaperEngine = slotEngineFromSelector(SlotKind::Shaper, load(shaperEngineParamIndex(part)));
```

In `processChunk`, replace the per-part loop body (from `ParamValues source;` to `toBus2[p] = ...;`) with:

```cpp
    Part& part = *parts_[size_t(p)];
    SlotParams params;
    for (int i = 0; i < int(kSourceParamCount); ++i) params.source.p[i] = load(sourceParamIndex(p, i));
    for (int i = 0; i < int(kFilterParamCount); ++i) params.filter.p[i] = load(filterParamIndex(p, i));
    for (int i = 0; i < int(kShaperParamCount); ++i) params.shaper.p[i] = load(shaperParamIndex(p, i));
    feedOther(p, n);
    const bool hasInput = part.settings().inputSource >= 1 || in != nullptr;
    if (hasInput) routeInput(p, in, n);
    const float* partIn[2] = {partIn_[p][0].data(), partIn_[p][1].data()};
    const float* other[2] = {otherIn_[p][0].data(), otherIn_[p][1].data()};
    float* out[2] = {partOut_[p][0].data(), partOut_[p][1].data()};
    part.process(VoiceInputs{hasInput ? partIn : nullptr, other}, out, params, n);
    const int output = part.settings().output;
    heard[p] = output != 2;  // Other part only: feeds the other part, not heard
    toBus2[p] = bus2Out != nullptr && output == 1;
```

Add `#include "voice/voice.h"` to `synth.cpp`'s includes if `VoiceInputs` is not already visible through `part/part.h` (it is: `part.h` includes `voice/voice.h`).

- [ ] **Step 4a: Document the defaults and slot switching in the manual**

In `docs/manual/parts-and-routing.md`, change the "Engine switching" section's first paragraph to begin "Each engine of each slot (Source, Filter, Shaper) remembers its own settings in each part." and replace the "Default patches" section's list with:

````markdown
- **EG13:** both parts with the Macro Oscillator, Filter *Off* and Shaper *Off*. (The full init patch, with envelopes and a filter, comes with modulation in M4.)
- **EG13 FX:**
  - Part A plays the Input through **Audio Input** → **Analog Filter** (fully open) → Shaper *Off*, with Trigger set to *Always on*, so it processes audio with no MIDI.
  - Dry is 0 and the master level is 100 %.
````

In the "Input and Input source" section, after the soft-limiter sentence, add:

````markdown
The Ring Mod / Vocoder can also take the part's Input or the other part's output as its carrier or modulator, with the same delays and limiter.
````

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all tests pass, including every M2 Synth test (the EG13 FX pass-through tests now run with the Filter Off). Then the Docker command.

- [ ] **Step 6: Commit**

```bash
git add core/synth/synth.h core/synth/synth.cpp tests/test_synth.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Synth: Filter and Shaper stash and switching, modulator sources, EG13 FX filter"
```

---
### Task 14: State schema 4: Filter and Shaper settings, migration from schema 3

**Files:**
- Manual: `docs/manual/parts-and-routing.md` (same commit)
- Modify: `core/state/state.h`, `core/state/state.cpp`
- Test: `tests/test_state.cpp`

**Interfaces:**
- Consumes: `slotStashIndex`, `kStashSize`, `Synth::defaultStash`, `Synth::engine(SlotKind, int)` (Task 13); slot helpers (Task 12).
- Produces: `state::kSchemaVersion = 4`. Saved JSON gains `"filterSettings"` and `"shaperSettings"`, shaped like M2's `"engineSettings"` (`{part: {engine: [P values]}}`, 4 and 12 numbers). Documents older than schema 4 load with Filter and Shaper Off and their P values 0.

- [ ] **Step 1: Write the failing tests**

In `tests/test_state.cpp`, change every `"\"schemaVersion\":3"` that is searched for in a *saved* document to `"\"schemaVersion\":4"` (in "Saved state has decimal parameter IDs", "Schema 1 documents from M0 load unchanged" and "Saved state is schema 3 and carries every engine's settings", which you rename to "Saved state is schema 4 and carries every engine's settings"). Documents written *into* `store.load(...)` keep their schema numbers.

Append (add `#include <cmath>`, `#include <vector>` and `#include "engine/engine_registry.h"` if missing):

```cpp
namespace {
uint32_t filterTypeId(int part) { return paramSpec(filterEngineParamIndex(part)).id; }
uint32_t shaperTypeId(int part) { return paramSpec(shaperEngineParamIndex(part)).id; }
}  // namespace

TEST_CASE("Schema 3 projects load with Filter and Shaper Off and pass audio as before") {
  Synth fx(PluginKind::Effect);
  state::StateStore store;
  REQUIRE(store.load(fx, R"({"schemaVersion":3,"params":{"16711681":1.0}})") == state::LoadResult::Ok);
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(fx.engine(SlotKind::Filter, part) == int(FilterEngine::Off));
    REQUIRE(fx.engine(SlotKind::Shaper, part) == int(ShaperEngine::Off));
    for (int p = 0; p < int(kFilterParamCount); ++p) REQUIRE(fx.param(paramSpec(filterParamIndex(part, p)).id) == 0.0);
  }
  REQUIRE(fx.engine(SlotKind::Source, 0) == int(SourceEngine::AudioInput));
  // M2's EG13 FX passed the input at unity, aligned with Dry: still so.
  fx.prepare(44100.0, 256);
  const size_t latency = size_t(fx.latencySamples());
  std::vector<float> l(13230), r(13230), ol(13230), orr(13230);
  for (size_t i = 0; i < l.size(); ++i) {
    l[i] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(i) / 44100.0));
    r[i] = -l[i];
  }
  for (size_t done = 0; done < l.size(); done += 256) {
    const int n = int(std::min<size_t>(256, l.size() - done));
    const float* in[2] = {l.data() + done, r.data() + done};
    float* out[2] = {ol.data() + done, orr.data() + done};
    fx.process(in, out, nullptr, n);
  }
  for (size_t i = latency + 2205; i < l.size(); ++i) {
    REQUIRE(std::fabs(ol[i] - 2.0f * l[i - latency]) < 1e-4f);  // wet + Dry (Dry 1.0)
  }
}

TEST_CASE("A new EG13 FX starts with the Analog Filter, a schema 4 load keeps what it says") {
  Synth fx(PluginKind::Effect);
  REQUIRE(fx.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
  state::StateStore store;
  const std::string json = state::StateStore{}.save(fx);
  Synth again(PluginKind::Effect);
  again.setParam(filterTypeId(0), 0);
  REQUIRE(store.load(again, json) == state::LoadResult::Ok);
  REQUIRE(again.engine(SlotKind::Filter, 0) == int(FilterEngine::Analog));
}

TEST_CASE("Filter and Shaper settings round-trip with every engine's stash") {
  Synth a(PluginKind::Instrument);
  a.setParam(filterTypeId(0), double(FilterEngine::Analog));
  a.setParam(paramSpec(filterParamIndex(0, 0)).id, 0.3);
  a.setParam(filterTypeId(0), double(FilterEngine::StateVariable));  // stashes the Analog's 0.3
  a.setParam(shaperTypeId(1), double(ShaperEngine::Eq));
  a.setParam(paramSpec(shaperParamIndex(1, 1)).id, 0.8);
  a.setParam(shaperTypeId(1), double(ShaperEngine::RingMod));
  a.setParam(paramSpec(shaperParamIndex(1, 8)).id, 0.75);  // Warps Mode: frequency shifter
  const std::string json = state::StateStore{}.save(a);
  REQUIRE(json.find("\"filterSettings\"") != std::string::npos);
  REQUIRE(json.find("\"shaperSettings\"") != std::string::npos);

  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.engine(SlotKind::Filter, 0) == int(FilterEngine::StateVariable));
  REQUIRE(b.engine(SlotKind::Shaper, 1) == int(ShaperEngine::RingMod));
  REQUIRE(b.param(paramSpec(shaperParamIndex(1, 8)).id) == 0.75);
  const EngineStash stash = b.engineStash();
  const EngineStash original = a.engineStash();
  for (size_t i = 0; i < kStashSize; ++i) REQUIRE(stash[i] == original[i]);
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, int(FilterEngine::Analog), 0)] == 0.3f);
  REQUIRE(stash[slotStashIndex(SlotKind::Shaper, 1, int(ShaperEngine::Eq), 1)] == 0.8f);
}

TEST_CASE("filterSettings and shaperSettings arrays have 4 and 12 numbers") {
  const std::string json = state::StateStore{}.save(Synth(PluginKind::Instrument));
  auto firstListLength = [&](const char* key) {
    const size_t start = json.find('[', json.find(key));
    const size_t end = json.find(']', start);
    const std::string list = json.substr(start, end - start);
    return size_t(std::count(list.begin(), list.end(), ',')) + 1;
  };
  REQUIRE(firstListLength("\"filterSettings\"") == kFilterParamCount);
  REQUIRE(firstListLength("\"shaperSettings\"") == kShaperParamCount);
}

TEST_CASE("Malformed filterSettings and shaperSettings entries are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":4,"params":{},)"
      R"("filterSettings":{"0":{"1":[0.1,0.2,0.3],"2":[0.4,0.4,0.4,0.4],"9":[1,1,1,1]}},)"
      R"("shaperSettings":{"1":{"2":[0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5]}}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const EngineStash stash = s.engineStash();
  const EngineStash defaults = Synth::defaultStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, 2, 0)] == 0.4f);  // the valid entry
  REQUIRE(stash[slotStashIndex(SlotKind::Filter, 0, 1, 0)] == defaults[slotStashIndex(SlotKind::Filter, 0, 1, 0)]);
  REQUIRE(stash[slotStashIndex(SlotKind::Shaper, 1, 2, 0)] == defaults[slotStashIndex(SlotKind::Shaper, 1, 2, 0)]);
  REQUIRE(store.save(s).find("\"9\":[") == std::string::npos);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "schema|Schema|Settings|settings|EG13 FX starts"`
Expected: FAIL (schema still 3; no `filterSettings`; schema 3 FX loads with the Analog Filter).

- [ ] **Step 3: Set the schema version in `core/state/state.h`**

```cpp
constexpr int kSchemaVersion = 4;
```

- [ ] **Step 4: Replace `core/state/state.cpp`**

```cpp
#include "state/state.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>

#include <nlohmann/json.hpp>

#include "engine/engine_registry.h"
#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::state {

namespace {

bool parseId(const std::string& key, uint32_t* id) {
  if (key.empty() || key.size() > 10) return false;
  for (char c : key) {
    if (c < '0' || c > '9') return false;
  }
  const unsigned long long value = std::strtoull(key.c_str(), nullptr, 10);
  if (value > 0xFFFFFFFFull) return false;
  *id = uint32_t(value);
  return true;
}

// Schema 1 (M0) -> 2: the nine M0 parameters kept their IDs and ranges, so no
// transformation is needed. Schema 2 -> 3 (M2): Source engine selector. Schema 3 -> 4 (M3):
// Filter and Shaper types, Off for older documents.

// Source engineSettings arrays hold kSourceParamCount numbers. Early unreleased schema 3 builds
// wrote 8 (P1..P8); those load as a prefix, the rest keep the engine's defaults.
constexpr size_t kLegacyEngineSettingsSize = 8;

// Every slot's per-engine settings: {part: {engine: [P values]}}.
struct Section {
  SlotKind kind;
  const char* key;
};
const Section kSections[kNumSlotKinds] = {
    {SlotKind::Source, "engineSettings"},  // M2's key
    {SlotKind::Filter, "filterSettings"},
    {SlotKind::Shaper, "shaperSettings"},
};

bool isKnownTopLevel(const std::string& key) {
  if (key == "schemaVersion" || key == "params") return true;
  for (const Section& s : kSections) {
    if (key == s.key) return true;
  }
  return false;
}

// Reads one section into the stash. False if anything had to be dropped.
bool readSection(const nlohmann::json& root, const Section& section, EngineStash& stash) {
  const auto found = root.find(section.key);
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  const size_t count = slotParamCount(section.kind);
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_object()) {
      clean = false;
      continue;
    }
    for (auto engineIt = partIt.value().begin(); engineIt != partIt.value().end(); ++engineIt) {
      uint32_t engine = 0;
      const auto& list = engineIt.value();
      bool valid = parseId(engineIt.key(), &engine) && engine < uint32_t(slotEngineCount(section.kind)) &&
                   list.is_array() &&
                   (list.size() == count ||
                    (section.kind == SlotKind::Source && list.size() == kLegacyEngineSettingsSize));
      for (size_t p = 0; valid && p < list.size(); ++p) valid = list[p].is_number();
      if (!valid) {
        clean = false;
        continue;
      }
      for (size_t p = 0; p < list.size(); ++p) {
        const double v = list[p].get<double>();
        stash[slotStashIndex(section.kind, int(part), int(engine), int(p))] =
            float(std::clamp(std::isnan(v) ? 0.0 : v, 0.0, 1.0));
      }
    }
  }
  return clean;
}

}  // namespace

std::string StateStore::save(const Synth& synth) const {
  nlohmann::json root = nlohmann::json::parse(extraTopLevel_);
  nlohmann::json params = nlohmann::json::parse(extraParams_);
  for (size_t i = 0; i < kParamCount; ++i) {
    const uint32_t id = paramSpec(i).id;
    params[std::to_string(id)] = synth.param(id);
  }
  root["schemaVersion"] = kSchemaVersion;
  root["params"] = std::move(params);

  const EngineStash stash = synth.engineStash();
  for (const Section& section : kSections) {
    nlohmann::json engines = nlohmann::json::object();
    for (int part = 0; part < kNumParts; ++part) {
      nlohmann::json perPart = nlohmann::json::object();
      for (int e = 0; e < slotEngineCount(section.kind); ++e) {
        nlohmann::json values = nlohmann::json::array();
        for (int p = 0; p < int(slotParamCount(section.kind)); ++p) {
          values.push_back(stash[slotStashIndex(section.kind, part, e, p)]);
        }
        perPart[std::to_string(e)] = std::move(values);
      }
      engines[std::to_string(part)] = std::move(perPart);
    }
    root[section.key] = std::move(engines);
  }
  return root.dump();
}

LoadResult StateStore::load(Synth& synth, const std::string& json) {
  const nlohmann::json root = nlohmann::json::parse(json, nullptr, false);
  if (root.is_discarded() || !root.is_object()) return LoadResult::Invalid;

  const auto version = root.find("schemaVersion");
  const auto params = root.find("params");
  if (version == root.end() || !version->is_number_integer() || params == root.end() ||
      !params->is_object()) {
    return LoadResult::Invalid;
  }
  const long long schema = version->get<long long>();
  if (schema < 1) return LoadResult::Invalid;
  bool warnings = schema > kSchemaVersion;

  ParamSnapshot values = synth.defaultSnapshot();
  nlohmann::json extraParams = nlohmann::json::object();
  for (auto it = params->begin(); it != params->end(); ++it) {
    uint32_t id = 0;
    if (!parseId(it.key(), &id) || !it.value().is_number()) {
      warnings = true;
      continue;
    }
    const int index = paramIndex(id);
    if (index < 0) {
      extraParams[it.key()] = it.value();
      warnings = true;
      continue;
    }
    values[size_t(index)] = clampParam(size_t(index), it.value().get<double>());
  }

  if (schema < 3) {
    // Schema 2 had no engine selector: its P values belong to the Macro Oscillator.
    for (int part = 0; part < kNumParts; ++part) values[sourceEngineParamIndex(part)] = 0.0f;
  }
  if (schema < 4) {
    // Schema 3 (M2) had no Filter or Shaper: those projects keep their sound with both Off,
    // whose P values are unused.
    for (int part = 0; part < kNumParts; ++part) {
      values[filterEngineParamIndex(part)] = 0.0f;
      values[shaperEngineParamIndex(part)] = 0.0f;
      for (int p = 0; p < int(kFilterParamCount); ++p) values[filterParamIndex(part, p)] = 0.0f;
      for (int p = 0; p < int(kShaperParamCount); ++p) values[shaperParamIndex(part, p)] = 0.0f;
    }
  }

  EngineStash stash = Synth::defaultStash();
  for (const Section& section : kSections) {
    if (!readSection(root, section, stash)) warnings = true;
  }

  nlohmann::json extraTopLevel = nlohmann::json::object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!isKnownTopLevel(it.key())) {
      extraTopLevel[it.key()] = it.value();
      warnings = true;
    }
  }

  std::string topDump = extraTopLevel.dump();
  std::string paramsDump = extraParams.dump();
  synth.loadParams(values, &stash);
  extraTopLevel_ = std::move(topDump);
  extraParams_ = std::move(paramsDump);
  return warnings ? LoadResult::OkWithWarnings : LoadResult::Ok;
}

}  // namespace eg13::state
```

- [ ] **Step 4a: Document older projects in the manual**

Append to `docs/manual/parts-and-routing.md`:

````markdown
## Projects from earlier versions

Projects saved before Filters and Shapers existed open with Filter and Shaper *Off*, so they sound as they did. This applies even in EG13 FX, whose new default is the Analog Filter. Each engine's remembered settings are saved with the project.
````

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`
Expected: all tests pass, including M2's state tests (schema 2 and 3 documents now also load with Filter and Shaper Off, which `defaultStash()` agrees with, so "Schema 3 without engineSettings uses each engine's defaults" still holds).

- [ ] **Step 6: Commit**

```bash
git add core/state/state.h core/state/state.cpp tests/test_state.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "State schema 4: Filter and Shaper settings; older projects load with both Off"
```

---
### Task 15: Levels, latency and CPU reports; README; validators

**Files:**
- Manual: `docs/manual/latency.md`, `docs/manual/limitations.md` (same commit)
- Create: `tests/test_levels.cpp`
- Modify: `tests/test_benchmark.cpp`, `README.md`
- Test: the above; `scripts/validate.sh`; the Docker command

**Interfaces:**
- Consumes: everything above.
- Produces: printed level, latency, memory and CPU tables; README figures; a passing validator run.

- [ ] **Step 1: Write the level and latency report (`tests/test_levels.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "engine/engine_registry.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> saw(size_t n) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(110.0 * double(i) / kRate, 1.0) - 1.0);
  return x;
}

struct Level {
  double gainDb;
  float peak;
};

Level measure(ProcessorSlot& slot, const ParamValues& p) {
  const auto in = saw(48000);
  std::vector<float> l(in.size()), r(in.size());
  slot.noteOn(45.0f, 1.0f);  // A2, the saw's pitch, for the Ring Mod oscillator
  for (size_t done = 0; done < in.size(); done += 16) {
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {l.data() + done, r.data() + done};
    slot.process(i2, nullptr, o2, p, 16);
  }
  float peak = 0.0f;
  for (size_t i = 9600; i < l.size(); ++i) peak = std::max(peak, std::fabs(l[i]));
  return {20.0 * std::log10(test::rms(l, 9600, l.size()) / test::rms(in, 9600, in.size())), peak};
}

}  // namespace

TEST_CASE("Output levels of every Filter and Shaper at its defaults", "[levels]") {
  std::printf("Level of a 110 Hz saw (peak 0.5) through each engine at its defaults, 48 kHz:\n");
  for (int e = 0; e < kNumFilterEngines; ++e) {
    ProcessorSlot slot(makeAllFilters());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    const Level level = measure(slot, test::defaultParams(filterEngineInfo(e)));
    std::printf("  Filter %-22s %+6.2f dB  peak %.3f\n", filterEngineInfo(e).displayName, level.gainDb, double(level.peak));
    REQUIRE(std::fabs(level.gainDb) < 1.5);  // open filters do not jump the level
  }
  for (int e = 0; e < kNumShaperEngines; ++e) {
    ProcessorSlot slot(makeAllShapers());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    const Level level = measure(slot, test::defaultParams(shaperEngineInfo(e)));
    std::printf("  Shaper %-22s %+6.2f dB  peak %.3f\n", shaperEngineInfo(e).displayName, level.gainDb, double(level.peak));
    REQUIRE(level.peak <= 32768.0f / 10240.0f);  // Warps' 16-bit range, read back at EG13 scale
    if (e != int(ShaperEngine::RingMod)) REQUIRE(std::fabs(level.gainDb) < 0.5);  // Off, flat EQ
  }
  std::printf("  Ring Mod / Vocoder by Algorithm (other parameters at their defaults):\n");
  for (int step = 0; step <= 8; ++step) {
    ProcessorSlot slot(makeAllShapers());
    slot.prepare(kRate, 16);
    slot.setEngine(int(ShaperEngine::RingMod));
    slot.reset();
    ParamValues p = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
    p.p[0] = float(step) / 8.0f;
    const Level level = measure(slot, p);
    std::printf("    %d/8  %+6.2f dB  peak %.3f\n", step, level.gainDb, double(level.peak));
    REQUIRE(level.peak <= 32768.0f / 10240.0f);
  }
}

TEST_CASE("Reported latency per plugin and host rate", "[levels]") {
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0}) {
      Synth synth(kind);
      synth.prepare(rate, 512);
      std::printf("%-10s %6.0f Hz: %4d samples (%.2f ms)\n", kind == PluginKind::Effect ? "EG13 FX" : "EG13",
                  rate, synth.latencySamples(), 1000.0 * synth.latencySamples() / rate);
      REQUIRE(synth.latencySamples() > 0);
    }
  }
}
```

- [ ] **Step 2: Run it and keep the numbers**

Run: `cmake --build --preset mac --target eg13_tests && build/mac/tests/eg13_tests "[levels]" -s 2>&1 | grep -E "Filter|Shaper|/8|Hz:"` and `build/mac/tests/eg13_tests "Memory per part with every engine preallocated" -s 2>&1 | grep "Part memory"`.
Expected: both pass; keep the printed tables for Step 5 and the final report. If an open filter or the flat EQ is more than the stated dB away from unity, report the table as BLOCKED rather than changing defaults.

- [ ] **Step 3: Extend the CPU benchmark (`tests/test_benchmark.cpp`)**

Owner decision (2026-10-20): the 50 % budget holds for chains whose Source is mono; the stereo worst case is measured and printed for information. Mono sources: Macro Oscillator, Digital Oscillator, Resonator with Spread 0, and Audio Input with a mono input. The Modal Synth is always stereo.

Add `#include <cmath>` and append:

```cpp
namespace {

struct ShaperSetup {
  const char* name;
  int engine;
  float algorithm;  // Ring Mod P1
  int carrier;      // Ring Mod P5 option
  int modulator;    // Ring Mod P6 option
  float mode;       // Ring Mod P9
};

const ShaperSetup kShapers[] = {
    {"Off", 0, 0.0f, 0, 0, 0.0f},
    {"Ring Mod (int osc)", int(ShaperEngine::RingMod), 0.25f, 0, 0, 0.25f},
    {"Ring Mod (voice carrier)", int(ShaperEngine::RingMod), 0.25f, 1, 0, 0.25f},
    {"Vocoder", int(ShaperEngine::RingMod), 1.0f, 0, 0, 0.25f},
    {"Freq shifter", int(ShaperEngine::RingMod), 0.9f, 0, 0, 0.75f},
    {"EQ", int(ShaperEngine::Eq), 0.0f, 0, 0, 0.0f},
};

// Percent of one core for 2 x 8 voices of one Source -> Filter -> Shaper chain at 48 kHz,
// 128-sample blocks. stereo = false keeps the Source mono (identical input channels, Resonator
// Spread 0); stereo = true feeds different channels and keeps the engines' defaults.
double chainLoad(int source, int filter, const ShaperSetup& shaper, int seconds, bool stereo) {
  constexpr double kRate = 48000.0;
  constexpr int kBlock = 128;
  Synth synth(PluginKind::Instrument);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(paramSpec(sourceEngineParamIndex(part)).id, source);
    if (!stereo && source == int(SourceEngine::Resonator)) {
      synth.setParam(paramSpec(sourceParamIndex(part, 7)).id, 0.0);  // Spread 0: mono fold
    }
    synth.setParam(paramSpec(filterEngineParamIndex(part)).id, filter);
    synth.setParam(paramSpec(filterParamIndex(part, 0)).id, 0.6);
    synth.setParam(paramSpec(shaperEngineParamIndex(part)).id, shaper.engine);
    if (shaper.engine == int(ShaperEngine::RingMod)) {
      synth.setParam(paramSpec(shaperParamIndex(part, 0)).id, shaper.algorithm);
      synth.setParam(paramSpec(shaperParamIndex(part, 4)).id, WarpsEngine::optionValue(shaper.carrier));
      synth.setParam(paramSpec(shaperParamIndex(part, 5)).id, WarpsEngine::optionValue(shaper.modulator));
      synth.setParam(paramSpec(shaperParamIndex(part, 8)).id, shaper.mode);
    }
  }
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  std::vector<float> inL(kBlock), inR(kBlock), l(kBlock), r(kBlock);
  for (int i = 0; i < kBlock; ++i) {
    inL[size_t(i)] = 0.1f * float(std::sin(0.05 * i));
    inR[size_t(i)] = stereo ? 0.1f * float(std::cos(0.07 * i)) : inL[size_t(i)];
  }
  const float* input[2] = {inL.data(), inR.data()};
  float* out[2] = {l.data(), r.data()};
  synth.process(input, out, nullptr, kBlock);
  for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
  const int blocks = int(kRate) * seconds / kBlock;
  const auto start = std::chrono::steady_clock::now();
  for (int b = 0; b < blocks; ++b) {
    if (b % 375 == 0) {
      for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
    }
    synth.process(input, out, nullptr, kBlock);
  }
  const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  synth.setActive(false);
  return 100.0 * elapsed / double(seconds);
}

struct Chain {
  int source = 0, filter = 0, shaper = 0;
  double load = 0.0;
};

}  // namespace

TEST_CASE("CPU: Source x Filter x Shaper chains, 2 x 8 voices, mono sources within budget", "[.benchmark]") {
  Chain worst[2];  // [0] mono sources, [1] stereo
  for (int stereo = 0; stereo < 2; ++stereo) {
    for (int s = 0; s < kNumSourceEngines; ++s) {
      if (!stereo && s == int(SourceEngine::Modal)) continue;  // always stereo
      for (int f = 0; f < kNumFilterEngines; ++f) {
        for (int k = 0; k < int(sizeof(kShapers) / sizeof(kShapers[0])); ++k) {
          const double load = chainLoad(s, f, kShapers[k], 1, stereo != 0);
          std::printf("%-7s %-20s %-22s %-26s %6.2f%%\n", stereo ? "stereo" : "mono", sourceEngineInfo(s).displayName,
                      filterEngineInfo(f).displayName, kShapers[k].name, load);
          if (load > worst[stereo].load) worst[stereo] = {s, f, k, load};
        }
      }
    }
  }
  const char* const kinds[2] = {"mono-source", "stereo"};
  double confirmed[2] = {};
  for (int stereo = 0; stereo < 2; ++stereo) {
    const Chain& c = worst[stereo];
    confirmed[stereo] = chainLoad(c.source, c.filter, kShapers[c.shaper], 5, stereo != 0);
    std::printf("Heaviest %s chain: %s -> %s -> %s: %.2f%% of one core over 5 s%s\n", kinds[stereo],
                sourceEngineInfo(c.source).displayName, filterEngineInfo(c.filter).displayName,
                kShapers[c.shaper].name, confirmed[stereo],
                stereo ? " (for information; not held to the budget)" : " (budget 50%)");
  }
  REQUIRE(confirmed[0] < 50.0);
}
```

Add `#include "engine/warps_engine.h"` at the top of the file.

- [ ] **Step 4: Run the benchmark on the macOS build**

Run: `build/mac/tests/eg13_tests "[.benchmark]" -s 2>&1 | grep -E "%"` (Release build; takes about four minutes).
Expected: the full table and both "Heaviest ..." lines print, and the mono-source chain is below 50 %. Planning estimates the heaviest mono-source chain (Resonator or Macro Oscillator → Analog Filter → Vocoder) close to the budget: about 8 % source + 15 % Analog Filter (with Rack's flags) + 17 % Warps vocoder + the Warps wrapper's six conversion channels. If the REQUIRE fails, do not change DSP, flags or the budget: report BLOCKED with the table. The first lever to propose is converting the Warps side inputs only while a selector uses them (the wrapper resamples all four input channels today).

- [ ] **Step 5: Update `README.md`**

Replace the `**Status:**` paragraph with:

```markdown
**Status:** M3 (filter and shaper): two parts × 8 voices, each Source → Filter → Shaper. Sources: Macro Oscillator, Digital Oscillator, Modal Synth, Resonator, Audio Input. Filters: Analog Filter, Classic Filters (205 types), State Variable Filter. Shapers: Ring Mod / Vocoder (every algorithm, a frequency shifter, and a modulator from an internal oscillator, the Source's AUX, the input or the other part) and EQ. Filters and shapers run a second, right-channel instance only while their input is stereo. See `docs/superpowers/specs/2026-10-20-eg13-design.md` for the full design.
```

In "Engines", append:

```markdown
- **Analog Filter** — the VCV model of Ripples
- **Classic Filters** — sst-filters (Surge's filters)
- **State Variable Filter** — the SVF from the Eurorack DSP library
- **Ring Mod / Vocoder** — based on Warps
- **EQ** — the VCV model of Shelves
```

Replace the latency paragraph (it begins "EG13 reports about 3.4–4.6 ms") with:

```markdown
EG13 reports about 10.9 ms of latency at 48 kHz and EG13 FX about 16.5 ms: the Modal Synth's 32 kHz engine and the Ring Mod / Vocoder's 96 kHz engine set it, and every engine is padded to it so changing engines never shifts timing. Processed input lines up with Dry in EG13 FX; hosts compensate automatically.
```

These are the planning figures (instrument 221 + 300 samples, FX 491 + 300 samples at 48 kHz, with the Shaper slot's 270-sample Warps conversion path, measured during planning, plus its 30-sample block FIFO). If the 48 kHz lines printed in Step 2 differ, write those values (two decimals) instead.

Below it add a "CPU" paragraph: copy the two "Heaviest ..." lines that Step 4 printed, prefixed with `CPU, Apple M1, 48 kHz, 128-sample blocks:`, and the sentence "The 50 % budget applies to chains with a mono Source; stereo chains of the heaviest engines can exceed it."

- [ ] **Step 5a: Update the manual's latency and limitation pages**

In `docs/manual/latency.md`, replace the table and the sentence after it with the following. The figures are the planning figures: M2's source latencies plus the Shaper slot's 266 / 270 samples of conversion at 44.1 / 48 kHz and Warps' 60-sample block (28 / 30 host samples; at 96 kHz Warps needs no conversion, so only its 60-sample block). If a line printed in Step 2 differs, write the printed value instead.

````markdown
| Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| EG13 (note onsets aligned) | 446 samples (10.11 ms) | 521 samples (10.85 ms) | 501 samples (5.22 ms) |
| EG13 FX (input aligned with Dry) | 722 samples (16.37 ms) | 791 samples (16.48 ms) | 1041 samples (10.84 ms) |

Two engines set these figures: the Modal Synth's 32 kHz engine and the Ring Mod / Vocoder's 96 kHz engine, which runs in 60-sample blocks. Every Shaper, Off included, is delayed to match the Ring Mod / Vocoder, so changing the Shaper never moves the sound in time. This latency is the accepted cost for version 1 (owner decision, 2026-10-20). Filters add no latency.
````

Append to `docs/manual/limitations.md`:

````markdown
- **CPU with stereo chains:** sixteen voices of the heaviest chains stay within half of one Apple M1 core when the Source is mono. With a stereo Source (Modal Synth, Resonator with Spread, a stereo input), every Filter and Shaper runs twice, and the heaviest stereo chains can use more. See the README for the measured figures.
- **Classic Filters Type** changes restart the filter and can click on a sounding note.
````

- [ ] **Step 6: Validate the plugins and the Linux build**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass; clap-validator, the AU Info.plist check, `auval -v aumu EG13 Symb`, `auval -v aumf EGfx Symb` and pluginval pass ("All validators passed."). The parameter list is longer (104 parameters) but no ID changed.

Run the Docker command from Global Constraints.
Expected: GCC 13 builds with `-Werror` and every test passes.

Manual check in a host (Reaper or Logic): load EG13, choose Filter Analog Filter and play while sweeping its Frequency; change Filter type and Shaper type while holding a chord (no clicks, no timing jump); load EG13 FX on an audio track (Analog Filter on by default) and an M2 project (Filter and Shaper Off).

- [ ] **Step 7: Commit**

```bash
git add tests/test_levels.cpp tests/test_benchmark.cpp README.md docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "M3 reports: engine levels, latency and the Source x Filter x Shaper CPU matrix; README"
```

---

## Self-review notes

- **Spec coverage.** §2.1 submodules (Task 1); §2.2 preallocation per voice and no audio-thread allocation (Tasks 5–11, every engine and slot has an allocation test), memory reported (Task 11); §3.2 native rate for Warps with constant, padded latency (Tasks 2, 8, 10, 11, 13); §3.3 VCV knob mappings (Tasks 5, 8, 9, cited); §4 Filter rows (Tasks 5–7), Shaper rows with all Warps algorithms and the Carrier / Modulator selectors (Tasks 8, 11, 13), Off in both slots (Task 10); §5.1 stereo only when needed (Tasks 3, 5–10); §7.1 Filter P1–P4, Shaper P1–P12 after the amendment, type selectors, live names (Tasks 1, 12, 13); §7.2 per-engine settings per slot and migration (Tasks 13, 14); §9 NaN guards (Tasks 5–9, 11), unknown engine IDs fall back to Off (Task 10); §10 fidelity at the native rate: bit-exact for Classic Filters, SVF and Warps, 1e-5 for the two VCV models built with Rack's flags (owner decision; Tasks 5–9), CPU budget (Task 15), validators and CI (Tasks 1, 15).
- **Manual:** Task 1 backfills M2's behaviour; Tasks 5–15 each document their own choices in the same commit (Global Constraints).
- **Not in M3:** modulation of Filter P1, the instrument init patch and the Amp (M4); Part FX (M5); UI cards and the type menus (M6).
