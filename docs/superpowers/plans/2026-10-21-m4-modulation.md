# EG13 M4 (Modulation) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every part four mod slots (Envelope from Stages, LFO from Tides 2, Random / S&H from Marbles, Function Generator from Peaks; per-voice or per-part), a 12-route modulation matrix with fixed sources (velocity, release velocity, keytrack, gate, random, MPE pressure / slide / per-note bend, mod wheel, pitch bend, channel pressure, four CCs, transport phase), an Amp and a pan per voice, MPE zones, tempo sync, gain staging (−12 dB voice headroom, Source trims, master soft clipper) and the init patches of both plugins, with host parameters (mod engine, scope, P1–P16 per slot; route amounts 1–12), state schema 5 and CPU, level and memory reports.

**Architecture:** A `ModEngine` (spec §3.1) produces up to eight control outputs. `NativeModEngine` runs the upstream module at its own rate and block size and samples its newest output once per control tick; the tick is a fixed 16-sample grid per part, so modulation never depends on how the host slices its blocks. A `ModSlot` holds every mod engine for one slot instance (preallocated). `PartModulation` owns the 4 part-scope slots and 8 × 4 per-voice slots and evaluates the 12 routes (pure functions in `mod_routes.h`) into a `VoiceControl` per voice: modulated Source/Filter/Shaper parameters, pitch, Amp, pan, gains. The Voice applies the Amp (routed, or following the gate with 5 ms ramps), the pan, the Source trim and the Voice Gain. The Synth stashes mod-slot settings per engine exactly like the Source/Filter/Shaper slots, keeps routes and Custom envelope shapes as patch data (pending-snapshot pattern), routes MIDI/MPE/CLAP expressions, runs a beat clock for tempo sync and applies the master soft clipper.

**Tech Stack:** C++17, CMake + Ninja, Catch2 v3, upstream pichenettes/eurorack (Stages, Tides 2, Marbles, Peaks + stmlib, unmodified, at f4cfa76), nlohmann/json, CLAP 1.2.10, clap-wrapper v0.16.0.

**Spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md` (M4 row of §11; §4 Mod rows; §5.2 Amp; §5.3 parts; §6 Modulation, including the owner decisions for M4 dated 2026-10-20 and both init patches; §7.1 mod slot parameters and route amounts; §7.2 state; §9 error handling; §10 testing and the CPU budget).

**Branch:** `m4-modulation`, stacked on `m3-filter-shaper` (pushed, not merged; head `52e63ec`). Do not switch branches.

## Global Constraints

- EG13 is GPL-3.0. Never use "Mutable Instruments" or module names in the UI or product. Mod engines show traditional names: **Off**, **Envelope** (Stages), **LFO** (Tides 2), **Random / S&H** (Marbles), **Function Generator** (Peaks). Source code and docs may name the modules for provenance.
- Everything M4 adds is MIT (the four modules and stmlib, in the existing `third_party/eurorack` submodule) or EG13's own code. No new submodule, no VCV model (none of the four needs Rack's flags: they compile in `eg13_eurorack` like Plaits), so the `rack_flags` isolation rules of `core/engine/ripples_model.cpp` do not apply. VCV AudibleInstruments' `Stages.cpp`, `Tides2.cpp` and `Marbles.cpp` are read only as the reference for knob mappings (spec §3.3); nothing is copied from them except Marbles' loop-length table, which is GPL-3.0 like EG13.
- macOS 14+ deployment target, universal binary (arm64 + x86_64). C++17. `/core` builds and tests on macOS and Linux (CI: GCC 13, `-Werror` on `eg13_core`). Check Linux locally with:
  ```bash
  docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 bash -c "apt-get update -qq && apt-get install -y -qq cmake ninja-build g++ >/dev/null && cmake --preset linux >/dev/null && cmake --build --preset linux && ctest --preset linux"
  ```
- The audio thread never allocates, locks or waits. Allocation happens only in constructors and `prepare`. Prove it with `eg13::test::ScopedAllocationCounter`, reading `counter.count()` into a local before `REQUIRE`.
- `third_party/eurorack` stays unmodified. Marbles' scale table lives in `marbles/settings.cc`, which needs the module's flash storage and cannot be compiled: Task 5 copies those lines verbatim with `sed` into a generated header and says so in it. No `eurorack-patches` change is needed.
- Every TU that includes eurorack headers is compiled with `TEST` defined and sees `third_party/eurorack-patches` first on the include path (unchanged). The four modules' `resources.h` files define clashing `LUT_*` macros: each adapter `.cpp` includes only its own module's headers.
- Parameter IDs are permanent and append-only: `id = (part << 16) | (slot << 8) | index`. Existing IDs keep their meaning. New in M4: mod slot k (0..3) uses `params::Slot::Mod1 + k`: engine `index 0` (stepped, fixed range 0..31), P1..P16 `index 1..16`, Scope `index 17` (stepped 0..1); route amounts `id(part, Slot::Matrix, 1..12)` (−1..1); part settings `PartParam::MpeMode = 16`, `MpeBendRange = 17`, `CcA..CcD = 18..21`; master `MasterParam::VoiceGain = 4`, `SoftClip = 5`. Every stepped selector carries a comment stating its append-only migration rule.
- Engine registry indices are permanent. Mod: 0 Off, 1 Envelope, 2 LFO, 3 Random / S&H, 4 Function Generator. Selector values without an engine fall back to Off.
- Matrix source and destination codes (`core/mod/mod_routes.h`) are saved in projects: append-only, never renumbered; reserved gaps stay reserved; unknown codes are kept on resave and ignored.
- Stepped mod-engine P's are option lists read in `kModSelectorSteps` (16) fixed steps; steps past the last option read as the last option (the Ring Mod / Vocoder rule, `core/engine/warps_engine.h:27-34`).
- Native rates and blocks (from the firmware): Stages 31250 Hz / 8 (`stages/segment_generator.h:43`, `stages/io_buffer.h:40`), Tides 2 62500 Hz / 8 (`tides2/io_buffer.h:40,46`), Marbles 32000 Hz / 5 (`marbles/marbles.cc:63`, `marbles/io_buffer.h:40`), Peaks 48000 Hz / 4 (`peaks/peaks.cc:79`, `peaks/io_buffer.h:41`). Mod engines run at these rates whatever the host rate; their outputs are sampled once per 16-sample control tick (spec §6 "control rate every 16 host samples").
- Fidelity (spec §10): at host rate = native rate each mod adapter matches the bare module bit for bit. The reference drives the upstream classes directly in the test, written from the firmware's `Process()` loop, never by calling adapter code (M3 lesson: a reference that shares code with the model tests nothing).
- Tests that depend on the host rate run at 44.1, 48 and 96 kHz; the control-rate sampler is also tested at 176.4 kHz.
- Every user-facing behaviour choice is documented in `docs/manual/` in the same task that implements it, in plain words for musicians. Smoothing bullets go in `docs/manual/smoothing.md` before its "After a reset" paragraph.
- Commits are authored by `Westlee Latta <symbioticaudio@proton.me>` (use `git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit ...`) and end with:
  ```
  Co-Authored-By: <the model that wrote the change> <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01BfbDAN2RSSY1ZShBua2VNM
  ```

## Owner decisions this plan implements (spec §6, 2026-10-20)

1. **Mod slots have P1–P16** (spec §7.1).
2. **Output safety:** a soft clipper on the master output, transparent below about −3 dBFS, never hard-clips, switchable (master parameter *Soft Clip*). Per-voice headroom and per-engine trims come first.
3. **MPE in M4:** a per-part MPE mode (lower or upper zone); per-note pitch bend, pressure and slide (CC74) routed per voice.
4. **Tempo sync:** with the transport stopped, tempo-synced modulators (LFO, Random / S&H) free-run at the last host tempo (120 BPM if none); they phase-lock when the transport starts.
5. **Gain staging (M2 ruling I4):** −12 dB per-voice headroom with a migration for older projects, per-engine trims, Audio Input chord scaling, a review of the inter-part soft limiter, and velocity handling for ringing Resonator voices.

## Provisional rulings (flag for owner)

Each line: what was chosen — why — what it costs to change.

1. **Mod P layouts and shared host defaults:** one reset value per P position for every mod engine: P1 0, P2 0, P3–P5 50 %, P6 100 %, P7–P9 50 %, P10–P16 0; each engine's controls are placed so these values suit it (Envelope ADSR 1 ms / 0.5 s / 100 % / 0.5 s; LFO cycling at 2 Hz; Random at the VCV defaults except X Range Full; Function Generator ADSR); the LFO leaves P8–P9 and the Function Generator P10 unused to keep their selectors on reset-0 positions — CLAP has one default per parameter (M3 ruling I1) — moving a control later is a state migration.
2. **Scope defaults:** a fresh Envelope, LFO and Function Generator are per voice, Random / S&H per part (spec §4); a host reset of Scope gives per voice for every engine (EG13 FX Part A Mod 1: per part) — one host default per parameter — a one-line table change.
3. **Control-rate sampling:** mod engines run at their native rate; the newest native sample is taken once per 16-sample tick on a fixed per-part grid; gate edges reach an engine at its next native block (≤ 0.26 ms) — keeps bit-exact fidelity and makes modulation independent of host block slicing — changing the grid is local to `Part`.
4. **Pitch destination:** a route amount of ±100 % is ±24 semitones; the pitch changes once per tick and each Source glides it inside its own block (spec §6 asks for per-sample interpolation; the Source engines take one note per native block) — a constant in `mod_routes.h`.
5. **Matrix feedback rule:** every route into a mod-slot parameter reads mod outputs from the previous tick (16 samples late), whatever the slot order, so loops are bounded and order-free — a one-tick (0.33 ms) delay on mod-to-mod routes — making it same-tick would make results depend on slot order.
6. **Part-level routes:** routes into Part Level and into per-part slots read per-voice sources (velocity, per-voice mod slots, …) from the most recently started voice — there is no single voice at part level — another rule (e.g. average of voices) is local to `PartModulation`.
7. **Gates:** per-part instances see a gate that is high while any key of the part is held and triggers only when the first key goes down (single trigger); per-voice instances are reset when their voice starts from silence and retriggered (not reset) on legato or stolen notes — musical defaults with no spec text — a branch in `Part`.
8. **Always on and the Amp:** an Always-on part's Amp follows the voice's permanent gate (open); its per-voice mod engines see a gate that follows held keys (first key triggers), so an envelope routed to the Amp gates a drone per key — spec §5.3 says notes do not retrigger the voice; this keeps that and still allows keyed envelopes — a branch in `Part`.
9. **Amp details:** the "Amp gain" of spec §5.2 is the Voice Gain; routed Amp, pan, Source gain and Voice Gain move in straight lines across each tick; a voice is freed when its note is off and either its output after the Amp or the Shaper's input (plus the Shaper latency) has stayed below −90 dBFS for 50 ms — the M3 rule kept for Shapers that sound on their own — another freeing rule is local to `Voice`.
10. **Pitch bend in the init patch:** "pitch bend → pitch ±2 semitones" is the part's built-in Bend Range (default 2), not a route; Pitch Bend is still a matrix source — avoids double bend and keeps M1's Bend Range meaningful — the init patch could add a route instead.
11. **MPE zones without a UI:** per-part *MPE* (Off / Lower zone / Upper zone) and *MPE Bend Range* (default 48 st); a zone has 15 member channels, or 7 each (lower 2–8, upper 9–15) when both parts use different zones; MPE Configuration Messages are ignored; master-channel bend, pressure and CCs are part-wide; member-channel bend, pressure and CC74 are per note; other member-channel CCs are ignored; the same key held on two channels shares one voice — the smallest useful MPE — MCM support is a small parser later.
12. **CLAP note expressions:** Tuning → per-note bend, Pressure → pressure, Brightness → slide; Volume, Pan, Vibrato and Expression are ignored in M4 — they have no matrix source yet — adding sources is append-only.
13. **Release velocity:** CLAP note-off velocity or MIDI 0x80 velocity, latched at release; MIDI 0x90 with velocity 0 counts as 64; 0 before the key is released — the MIDI convention — a constant.
14. **MIDI-learnable CCs without a UI:** four stepped part parameters *CC A–D* (defaults 2 Breath, 4 Foot, 11 Expression, 16 General 1), editable in hosts now; a core learn call (`Synth::learnCc`) for M6 takes the next CC except 64, 120 and 123 — host parameters keep them editable before the UI — renaming or re-defaulting is a table edit.
15. **Routes are patch data** (source, destination, via) with host-visible amounts only (spec §7.1); a route amount's text follows its destination ("+0.50 st" for pitch, "+30%" otherwise, "-" for an empty route) — spec — making sources host parameters later would add IDs.
16. **Code space:** fixed sources 1–16 (17–31 reserved), mod outputs 32–63 (slot × 8 + output); destinations Pitch 1, Amp 2, Pan 3, Part Level 4, Source P 16–31, Filter P 32–47, Shaper P 48–63, Part FX P 64–79 (reserved for M5, accepted and ignored now), mod-slot P 80–143 — room to grow without renumbering — a migration if a range overflows.
17. **Keytrack:** the Note source is (note − 60) / 120, so 100 % into any Filter's cutoff (20 Hz·1000^p) tracks the keyboard exactly — follows from the shared cutoff curve (M3) — a constant.
18. **Transport phase:** the position within the current bar (0..1), free-running at the last tempo and time signature when stopped — "host transport phase" is otherwise undefined — a different period is a constant.
19. **Tempo sync mechanics:** the LFO derives its ramp directly from the beat position (no clock extraction), one cycle per beat at Frequency 50 %, ratios from Tides' own table; Random / S&H gets one clock pulse per beat and restarts when the transport starts or jumps — exact and deterministic — the hardware's clock extractor could be used instead.
20. **LFO range:** Low (0.125 Hz root) and Medium (2 Hz root) only; Tides' audio range is left out — v1 has no audio-rate modulation (spec §6) and it would alias at the control rate — appending "High" is append-only.
21. **Function Generator modes:** Envelope (Peaks' ADSR, full control mode), LFO, Tap LFO (notes are the taps; level fixed at the free LFO's), Random (the LFO's noise shape with Smoothness) — the spec's list; Peaks' drums and other functions are out — appending modes is append-only.
22. **Random / S&H mappings:** VCV's (no deja-vu deadband, Y follows X's spread/bias/steps, Y divider 1/4, pulse-width mean and spread 0); each instance gets its own seed (part, slot, voice), so voices and parts never repeat each other — VCV seeds every module alike — seeds are one function.
23. **Soft clipper curve:** y = x up to t = 10^(−3/20); above it y = t + (1 − t)·tanh((|x| − t)/(1 − t)) with the sign of x: slope 1 at the knee, never reaching full scale for finite input; per channel, on Main and Bus 2, after Dry — the simplest C¹ curve meeting the owner's description — one function.
24. **Voice Gain:** master *Voice Gain*, −12 dB (instrument default) or 0 dB (EG13 FX default, and every project saved before M4) — the FX usually runs one Always-on voice, and older projects keep their level — a default.
25. **Source trims** at the Source output, before the Filter: Macro Oscillator 0 dB, Digital Oscillator −3 dB, Modal Synth +12 dB, Resonator +12 dB, Audio Input 0 dB, so a single held note peaks within about 1 dB of the Macro Oscillator's (measured during planning: peaks 0.66, 0.97, 0.17, 0.17 at defaults) — trims change Filter/Shaper drive; trimming after the chain was the alternative — a table.
26. **Audio Input chord scaling:** each Audio Input voice is scaled by 1/N, N = the part's Audio Input voices whose gate is open, moving across each tick — the voices carry the same Input, so a chord would otherwise multiply it — release tails take the count at that moment.
27. **Resonator velocity:** the velocity gain moves over 5 ms instead of stepping, removing the click; older strums still take the newest velocity (true per-strum velocity needs an upstream patch to scale Rings' internal exciter) — deferred — one `eurorack-patches` file.
28. **Instrument init patch filter:** State Variable Filter, LP, cutoff 70 % (≈ 2.5 kHz), so Mod 2's +30 % opens it fully at the envelope's peak; this sets the instrument's Filter P1 reset value to 70 % (EG13 FX keeps 100 %) — the Analog Filter costs about three times the CPU for an init sound — one table entry.
29. **Init LFO rates:** the instrument's Mod 3 Frequency defaults to ≈ 5 Hz (vibrato) and EG13 FX's Mod 1 to ≈ 0.25 Hz (slow filter sweep), as per-slot host defaults — 2 Hz suits neither — table entries.
30. **Projects saved before M4:** load with mod slots empty, no routes, Voice Gain 0 dB and Soft Clip Off, so they keep their level; with no route to the Amp, notes now stop 5 ms after release (spec §5.2), which cuts the tails of ringing sources — the spec's Amp rule — a migration could add an envelope route instead.
31. **CPU budget:** the 50 % budget (spec §10) keeps measuring the voice chain without modulation, as in M3; modulation gets its own stated budget: at most 10 % of one M1 core for 2 × 8 voices with all four mod slots per voice running the costliest mod engine and 12 active routes per part. If it fails, F19-style, the pre-authorised lever is to skip mod slots no route reads (an unrouted slot changes nothing audible); if still over, report the figures and stop — M3's chain is already at 49–51.5 %, so one shared budget cannot hold (planning measurement: 64 instances of the costliest engine ≈ 4.6 %) — the owner may merge the budgets later.
32. **Mod output scaling:** LFO volts / 5 when cycling, / 8 for AD and AR, clamped to ±1; Random X/Y by voltage range (/2 Narrow, /5 Positive and Full), gates 0/1; Function Generator envelope /32767, LFO /20480 (its ±5 V of ±8 V); Envelope as is (0..1) — every output reads −1..1 or 0..1 — constants.
33. **Envelope Custom mode:** six segments whose types (Ramp / Step / Hold) and loop flags are patch data, default ADSR; segment k reads P(2k) and P(2k+1) as its primary and secondary, as the module's slider and pot — spec §7.1 makes shapes patch data — the UI edits them in M6.
34. **Changing a mod engine** is immediate (the new engine starts from reset); destinations smooth the jump as they smooth any change — mod outputs are control signals — a crossfade could be added in `ModSlot`.
35. **Slide:** CC74 on a non-member channel is a part-wide Slide; MPE member CC74 and CLAP Brightness are per note; the Slide source reads the larger — one source covers MPE and non-MPE controllers — a rule in `PartModulation`.

## Review Focus

1. **Host reset or automation of mod-slot P's and selectors while notes play** (a host double-clicks a Mod P, or automates Mod Engine): every engine gets a sensible value, nothing goes NaN, P values follow the engine and the host shows them. Pinned in Task 13 ("Mod slot host defaults suit every mod engine", "Switching a mod engine while notes sound swaps its P values and scope").
2. **Routes that loop** (Mod 1 → Mod 2 rate, Mod 2 → Mod 1 rate, a slot into its own P): bounded, deterministic, independent of slot order. Pinned in Task 9 ("A mod slot routed into its own P settles and stays bounded", "Routes into mod-slot P's read the previous tick whatever the slot order").
3. **Host block slicing and sample rates** (1-sample and odd blocks from sample-accurate automation; 44.1 / 48 / 96 / 176.4 kHz): LFO rates, envelope times and the Amp do not depend on the slicing or the rate. Pinned in Task 2 ("The sampler keeps native time at every host rate"), Task 11 ("Modulation does not depend on host block slicing") and Task 4 ("LFO frequency holds at 44.1, 48 and 96 kHz").
4. **An MPE controller on a part in MPE mode** (notes on member channels, bend and pressure per channel, master-channel bend, both parts in MPE): each expression reaches only its own voice; master messages reach the whole part. Pinned in Task 16 ("MPE member messages reach only their own voice; the master channel reaches the part", "Both parts in MPE split the member channels").
5. **Opening an M3 project or loading state while playing:** the level is unchanged (Voice Gain 0 dB, Soft Clip Off), mod slots empty, and a load mid-block applies params and routes together. Pinned in Task 14 ("Schema 4 projects load with no modulation at their old level", "Routes and parameters of a load apply in the same block").

## File Structure

```
cmake/eurorack.cmake                      + Stages, Tides 2, Marbles, Peaks sources
core/CMakeLists.txt                       new sources
core/engine/mod_engine.h|.cpp             ModEngine, ModContext, ModEngineInfo, EnvelopeShape,
                                          NativeModEngine (native-rate runner), OffModEngine
core/engine/stages_engine.h|.cpp          Envelope (Stages)
core/engine/tides_engine.h|.cpp           LFO (Tides 2)
core/engine/marbles_engine.h|.cpp         Random / S&H (Marbles)
core/engine/marbles_scales.h              generated: Marbles' six scales, verbatim
core/engine/peaks_engine.h|.cpp           Function Generator (Peaks)
core/engine/engine_registry.h|.cpp        ModEngineKind, modEngineInfo, modEngineFromSelector,
                                          sourceTrim
core/engine/rings_engine.h|.cpp           velocity de-click
core/mod/mod_routes.h|.cpp                source/destination codes, Route, RouteSet, sumRoutes
core/mod/mod_slot.h|.cpp                  ModSlot: every mod engine of one slot instance
core/mod/mod_factory.h|.cpp               makeModEngine, makeAllModEngines
core/mod/part_modulation.h|.cpp           per-voice and per-part slots, tick evaluation
core/mod/beat_clock.h                     Transport, BeatClock (tempo sync, transport phase)
core/mod/voice_control.h                  VoiceControl: one voice's controls for a tick
core/voice/slot_params.h                  SlotParams (moved out of voice.h)
core/util/soft_clip.h                     the master soft clipper
core/voice/voice.h|.cpp                   VoiceControl, Amp, pan, gains, beginTick/render
core/part/part.h|.cpp                     PartParams, modulation tick grid, expressions, MPE,
                                          trims, Audio Input chord scaling
core/synth/param_table.h|.cpp             v5 table: mod slots, scope, route amounts, MPE, CCs,
                                          Voice Gain, Soft Clip
core/synth/synth.h|.cpp                   mod stash/switching, patch data, init patches, MIDI/MPE,
                                          learn, transport, soft clipper
core/state/state.h|.cpp                   schema 5: mod settings, routes, envelope shapes
plugin/src/eg13_plugin.cpp                transport, note expressions, MPE dialect, pressure,
                                          release velocity
tests/test_mod_engines.h                  test mod engines (echo, step, NaN)
tests/synth_helpers.h                     test::m3Patch() for pre-M4 Synth tests
tests/test_mod_dsp_smoke.cpp              upstream modules run
tests/test_mod_engine.cpp                 NativeModEngine sampler
tests/test_stages_engine.cpp, test_tides_engine.cpp, test_marbles_engine.cpp,
tests/test_peaks_engine.cpp, test_mod_slot.cpp, test_mod_routes.cpp,
tests/test_part_modulation.cpp, test_beat_clock.cpp, test_soft_clip.cpp
tests/test_voice.cpp, test_part.cpp, test_synth.cpp, test_state.cpp, test_param_table.cpp,
tests/test_levels.cpp, test_benchmark.cpp      extended
docs/superpowers/specs/2026-10-20-eg13-design.md   §2.1, §5.2, §6, §7.1 amendments
docs/manual/modulation.md                 new: mod slots, engines, matrix, MPE, sync, gain
docs/manual/index.md, engines.md, parts-and-routing.md, smoothing.md, latency.md,
docs/manual/limitations.md                updated per task
README.md                                 status, engines, CPU, levels
```

---
### Task 1: Mod DSP in the build, spec amendments, the Modulation manual page

**Files:**
- Modify: `cmake/eurorack.cmake`, `docs/superpowers/specs/2026-10-20-eg13-design.md`, `docs/manual/index.md`
- Create: `docs/manual/modulation.md`
- Test: `tests/test_mod_dsp_smoke.cpp`

**Interfaces:**
- Consumes: M3 build (`eg13_eurorack`, `eg13_core`, `eg13_tests`).
- Produces: `eg13_eurorack` also compiles `stages/segment_generator.cc`, `stages/resources.cc`, `tides2/ramp/ramp_extractor.cc` (Stages needs it too), `tides2/poly_slope_generator.cc`, `tides2/resources.cc`, `marbles/random/*.cc` (six files), `marbles/ramp/ramp_extractor.cc`, `marbles/resources.cc`, `peaks/modulations/lfo.cc`, `peaks/modulations/multistage_envelope.cc`, `peaks/resources.cc`. `docs/manual/modulation.md` exists with an introduction; later tasks append sections to it.

- [ ] **Step 1: Write the failing smoke tests (`tests/test_mod_dsp_smoke.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "peaks/modulations/lfo.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"
#include "tides2/poly_slope_generator.h"

TEST_CASE("Upstream Stages runs an ADSR") {
  static stages::SegmentGenerator g;  // about 4 KB: keep it off the stack
  g.Init();
  const stages::segment::Configuration adsr[4] = {{stages::segment::TYPE_RAMP, false},
                                                  {stages::segment::TYPE_RAMP, false},
                                                  {stages::segment::TYPE_HOLD, true},
                                                  {stages::segment::TYPE_RAMP, false}};
  g.Configure(true, adsr, 4);
  g.set_segment_parameters(0, 0.2f, 0.5f);  // attack about 22 ms
  g.set_segment_parameters(1, 0.3f, 0.5f);  // decay about 72 ms
  g.set_segment_parameters(2, 0.6f, 0.5f);  // sustain level 0.6
  g.set_segment_parameters(3, 0.3f, 0.5f);  // release about 72 ms
  stmlib::GateFlags flags[8];
  stages::SegmentGenerator::Output out[8];
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  float held = 0.0f;
  float peak = 0.0f;
  for (int block = 0; block < 3906; ++block) {  // 1 s at 31250 Hz; gate high for the first half
    const bool gate = block < 1953;
    for (auto& f : flags) previous = f = stmlib::ExtractGateFlags(previous, gate);
    g.Process(flags, out, 8);
    peak = std::max(peak, out[7].value);
    if (block == 1950) held = out[7].value;
  }
  REQUIRE(peak > 0.99f);
  REQUIRE(std::fabs(held - 0.6f) < 0.01f);
  REQUIRE(out[7].value < 0.01f);
}

TEST_CASE("Upstream Tides 2 cycles at its frequency") {
  static tides::PolySlopeGenerator g;
  g.Init();
  const stmlib::GateFlags noGate[4] = {};
  tides::PolySlopeGenerator::OutputSample out[4];
  // Slope/phase output runs at half rate, as tides2/tides.cc:142-152 does: 4 samples per 8-sample
  // block at twice the frequency. 2 Hz for 1 s.
  const float frequency = 2.0f * 2.0f / 62500.0f;
  float lowest = 0.0f, highest = 0.0f, previous = 0.0f;
  int rising = 0;
  for (int block = 0; block < 62500 / 8; ++block) {
    g.Render(tides::RAMP_MODE_LOOPING, tides::OUTPUT_MODE_SLOPE_PHASE, tides::RANGE_CONTROL, frequency, 0.5f,
             0.5f, 0.5f, 0.5f, noGate, nullptr, out, 4);
    for (const auto& s : out) {
      const float v = s.channel[0];
      lowest = std::min(lowest, v);
      highest = std::max(highest, v);
      if (previous < 0.0f && v >= 0.0f) ++rising;
      previous = v;
    }
  }
  REQUIRE(lowest < -4.0f);  // +-5 V when cycling
  REQUIRE(highest > 4.0f);
  REQUIRE(rising >= 1);
  REQUIRE(rising <= 3);
}

TEST_CASE("Upstream Marbles makes gates and voltages") {
  static marbles::RandomGenerator generator;
  static marbles::RandomStream stream;
  static marbles::TGenerator t;
  static marbles::XYGenerator xy;
  generator.Init(1);
  stream.Init(&generator);
  t.Init(&stream, 32000.0f);
  xy.Init(&stream, 32000.0f);
  t.set_model(marbles::T_GENERATOR_MODEL_COMPLEMENTARY_BERNOULLI);
  t.set_range(marbles::T_GENERATOR_RANGE_1X);
  t.set_rate(0.0f);
  t.set_bias(0.5f);
  t.set_jitter(0.0f);
  t.set_deja_vu(0.0f);
  t.set_length(1);
  t.set_pulse_width_mean(0.0f);
  t.set_pulse_width_std(0.0f);
  marbles::GroupSettings x{};
  x.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  x.voltage_range = marbles::VOLTAGE_RANGE_FULL;
  x.spread = 0.5f;
  x.bias = 0.5f;
  x.steps = 0.5f;
  x.length = 1;
  x.ratio.p = 1;
  x.ratio.q = 1;
  marbles::GroupSettings y = x;
  y.ratio.q = 4;
  float master[5], external[5], slave0[5], slave1[5];
  marbles::Ramps ramps;
  ramps.master = master;
  ramps.external = external;
  ramps.slave[0] = slave0;
  ramps.slave[1] = slave1;
  bool gates[10];
  float volts[20];
  const stmlib::GateFlags clock[5] = {};
  int edges = 0;
  bool was = false;
  float lowest = 0.0f, highest = 0.0f;
  for (int block = 0; block < 2 * 32000 / 5; ++block) {  // 2 s
    t.Process(false, clock, ramps, gates, 5);
    xy.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, clock, ramps, volts, 5);
    for (int i = 0; i < 5; ++i) {
      const bool t2 = master[i] < 0.5f;
      if (t2 && !was) ++edges;
      was = t2;
      lowest = std::min(lowest, volts[4 * i]);
      highest = std::max(highest, volts[4 * i]);
    }
  }
  REQUIRE(edges >= 2);  // T2 is the clock itself: about 2 Hz at rate 0
  REQUIRE(lowest >= -5.01f);
  REQUIRE(highest <= 5.01f);
  REQUIRE(highest - lowest > 1.0f);
}

TEST_CASE("Upstream Peaks runs an LFO") {
  static peaks::Lfo lfo;
  lfo.Init();
  uint16_t knobs[4] = {32768, 6553, 32768, 32768};  // 2.2 Hz, sine, neutral shape, phase 0
  lfo.Configure(knobs, peaks::CONTROL_MODE_FULL);
  const peaks::GateFlags gates[4] = {};
  int16_t out[4];
  int lowest = 0, highest = 0;
  for (int block = 0; block < 48000 / 4; ++block) {
    lfo.Process(gates, out, 4);
    for (int16_t v : out) {
      lowest = std::min(lowest, int(v));
      highest = std::max(highest, int(v));
    }
  }
  REQUIRE(lowest < -15000);
  REQUIRE(highest > 15000);
}
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: link errors (undefined `stages::SegmentGenerator::Configure`, `tides::PolySlopeGenerator::...`, `marbles::TGenerator::Init`, `peaks::Lfo::Init`).

- [ ] **Step 3: Add the four modules to `cmake/eurorack.cmake`**

In the `add_library(eg13_eurorack STATIC ...)` source list, after `${EURORACK_DIR}/warps/resources.cc`, add:

```cmake
  ${EURORACK_DIR}/stages/segment_generator.cc
  ${EURORACK_DIR}/stages/resources.cc
  ${EURORACK_DIR}/tides2/ramp/ramp_extractor.cc
  ${EURORACK_DIR}/tides2/poly_slope_generator.cc
  ${EURORACK_DIR}/tides2/resources.cc
  ${EURORACK_DIR}/marbles/random/discrete_distribution_quantizer.cc
  ${EURORACK_DIR}/marbles/random/lag_processor.cc
  ${EURORACK_DIR}/marbles/random/output_channel.cc
  ${EURORACK_DIR}/marbles/random/quantizer.cc
  ${EURORACK_DIR}/marbles/random/t_generator.cc
  ${EURORACK_DIR}/marbles/random/x_y_generator.cc
  ${EURORACK_DIR}/marbles/ramp/ramp_extractor.cc
  ${EURORACK_DIR}/marbles/resources.cc
  ${EURORACK_DIR}/peaks/modulations/lfo.cc
  ${EURORACK_DIR}/peaks/modulations/multistage_envelope.cc
  ${EURORACK_DIR}/peaks/resources.cc
```

These are the modules' DSP files only: their `*.cc` entry points (`stages.cc`, `tides.cc`, `marbles.cc`, `peaks.cc`), drivers, UI and settings need the STM32 hardware and stay out. Stages needs `tides2/ramp/ramp_extractor.cc` (its `SegmentGenerator` owns a `tides::RampExtractor`, `stages/segment_generator.h:36,232`). Each module keeps its tables in its own namespace (`stages::`, `tides::`, `marbles::`, `peaks::`), so nothing clashes at link time.

- [ ] **Step 4: Run the smoke tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Upstream (Stages|Tides|Marbles|Peaks)"`
Expected: 4 tests pass. Then run the whole suite (`ctest --preset mac`): everything passes.

- [ ] **Step 5: Amend the spec**

In `docs/superpowers/specs/2026-10-20-eg13-design.md`:

§2.1, replace the line `   voice/ part/ mod/ params/ state/` with:

```
   voice/ part/ params/ state/
   mod/        mod slots, the matrix, the beat clock (M4)
```

§5.2, replace the paragraph that begins "The Amp is a destination in the matrix." with:

```markdown
The Amp is a destination in the matrix. If any route targets Amp, the Amp level is the sum of those routes (clamped 0..1) × the Voice Gain. If no route targets Amp, the Amp follows the gate with 5 ms attack and release ramps. A voice is freed when its note is off and either its output (after the Amp) or the Shaper's input (plus the Shaper slot latency, M3) has stayed below −90 dBFS for 50 ms. Gain staging (M4, from the M2 review): each voice is −12 dB (master *Voice Gain*; 0 dB in EG13 FX and in projects saved before M4), each Source has a trim that evens out their levels, and Audio Input voices are scaled by the number of open Audio Input voices so a chord does not multiply the Input.
```

§6, after the "**Rate:**" bullet, add:

```markdown
- **Matrix rules (M4 plan, 2026-10-21):** a route's contribution is source × amount × via (via absent = 1). Routes add in normalized space and clamp to 0..1; pitch is in semitones (±100 % = ±24 semitones), pan centres at 0. Routes into a mod slot's parameters read the mod outputs of the previous control tick (16 samples), so loops between slots are bounded and independent of slot order. Routes into Part Level and into per-part slots read per-voice sources from the most recently started voice. Source and destination codes are append-only.
- **Gates (M4 plan, 2026-10-21):** per-voice mod engines are reset when their voice starts from silence and retriggered by each new note on it; per-part engines see a gate that is high while any key of the part is held and triggers when the first key goes down. In an Always-on part the Amp's gate stays high and the per-voice mod engines' gate follows the held keys.
```

§7.1, replace the line `- Mod slots 1–4: engine, scope, P1–P16 (raised from 6, owner 2026-10-20: Marbles, Tides 2 and Stages' six segments need more than 6; unused slots show "(unused)")` with:

```markdown
- Mod slots 1–4: engine, scope, P1–P16 (raised from 6, owner 2026-10-20: Marbles, Tides 2 and Stages' six segments need more than 6; unused slots show "(unused)"). Scope is a stepped host parameter (per voice / per part) remembered per engine like the P's.
```

and replace the line `- Master: output level, Dry level, mode (Single / Layer / Split / Multi), split note` with:

```markdown
- Part settings added in M4 (stepped): MPE (off / lower zone / upper zone), MPE bend range, CC A–D (the four learnable controller numbers)
- Master: output level, Dry level, mode (Single / Layer / Split / Multi), split note, Voice Gain (−12 dB / 0 dB, M4), Soft Clip (on / off, M4)
```

- [ ] **Step 6: Start the Modulation manual page**

Create `docs/manual/modulation.md`:

````markdown
# Modulation

Each part has four **mod slots** and a **modulation matrix** of twelve routes. A mod slot holds a modulation engine (an envelope, an LFO, a random source or a function generator) or is empty. A route takes a **source** (a mod slot's output, or one of the fixed sources such as velocity or the mod wheel), scales it by an **amount** and sends it to a **destination**: any control of the Source, Filter or Shaper, the pitch, the Amp, the pan, the part level, or a control of another mod slot.

In the host, each part has the groups *Part A/Mod 1* … *Part A/Mod 4* (each with *Engine*, *Scope* and P1–P16) and *Part A/Matrix* (*Route 1* … *Route 12*, the amounts). Which source and destination each route uses is part of the patch: it is saved with the project, and the editor arrives with EG13's own interface. A route with no source or no destination does nothing and its amount shows "-".
````

In `docs/manual/index.md`, after the line for Engines, add:

```markdown
- [Modulation](modulation.md): mod slots, their engines, the matrix, MPE, tempo sync and gain staging.
```

- [ ] **Step 7: Commit**

```bash
git add cmake/eurorack.cmake tests/test_mod_dsp_smoke.cpp docs/superpowers/specs/2026-10-20-eg13-design.md docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Build Stages, Tides 2, Marbles and Peaks; spec amendments; Modulation manual page"
```

---
### Task 2: ModEngine interface and the native-rate sampler

**Files:**
- Create: `core/engine/mod_engine.h`, `core/engine/mod_engine.cpp`, `tests/test_mod_engines.h`
- Modify: `core/CMakeLists.txt` (add `engine/mod_engine.cpp`), `docs/manual/modulation.md`
- Test: `tests/test_mod_engine.cpp`

**Interfaces:**
- Consumes: `ParamValues`, `EngineInfo`, `EngineParamInfo`, `stepIndex` (`core/engine/voice_engine.h`, `core/engine/param_format.h`).
- Produces (all in namespace `eg13`, `core/engine/mod_engine.h`):
  - `constexpr int kMaxModOutputs = 8; constexpr int kModParamCount = 16; constexpr int kModSelectorSteps = 16;`
  - `int modOption(float normalized, int options); float modOptionValue(int option);`
  - `struct ModOutputInfo { const char* name; bool bipolar; };`
  - `struct ModEngineInfo { EngineInfo engine; int outputCount; const ModOutputInfo* outputs; bool perPartByDefault; };`
  - `struct EnvelopeShape { static constexpr int kMaxSegments = 6; enum Type : uint8_t { Ramp, Step, Hold }; uint8_t count; uint8_t type[6]; uint8_t loop[6]; };` with `operator==`, `operator!=`, `EnvelopeShape sanitized(const EnvelopeShape&)`.
  - `enum : uint8_t { kGateLow = 0, kGateHigh = 1, kGateRising = 2, kGateFalling = 4 };`
  - `struct ModContext { bool gate; bool retrigger; double beat; double beatsPerSample; bool beatJump; };`
  - `class ModEngine { virtual void prepare(double hostRate); virtual void reset(); virtual void process(const ModContext&, const ParamValues&, int n, float* out); virtual const ModEngineInfo& info() const; virtual void setShape(const EnvelopeShape&); virtual void setSeed(uint32_t); };`
  - `class NativeModEngine : public ModEngine { static constexpr int kMaxNativeBlock = 8; NativeModEngine(double nativeRate, int nativeBlock); double nativeRate() const; int nativeBlock() const; int64_t renderedSamples() const; protected: virtual void resetEngine() = 0; virtual void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative, bool beatJump, float (*block)[kMaxNativeBlock]) = 0; };`
  - `class OffModEngine final : public ModEngine { static const ModEngineInfo& staticInfo(); };` (id `"off"`, "Off", no params, no outputs).
  - `tests/test_mod_engines.h`: `eg13::test::CountingModEngine(double rate, int block)` with public `blocks`, `lastJump`, `lastGates[8]`, `lastBeats[8]`.

The sampler is the heart of the control rate (provisional ruling 3). Native time is computed from a host-sample counter (`hostSamples × nativeRate / hostRate`), never accumulated, so it cannot drift over a long session; a planning run showed an accumulated `position += n × ratio` losing one native sample per second at 44.1 kHz.

- [ ] **Step 1: Write the test engine (`tests/test_mod_engines.h`)**

```cpp
#pragma once

#include <cstdint>

#include "engine/mod_engine.h"

namespace eg13::test {

// A native-rate engine that writes, per native sample, its global index (output 0), its gate
// flags (output 1) and its beat position (output 2), and keeps the last block's inputs.
class CountingModEngine final : public NativeModEngine {
 public:
  CountingModEngine(double rate, int block) : NativeModEngine(rate, block) {}
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[3] = {{"Index", false}, {"Gate", false}, {"Beat", false}};
    static const ModEngineInfo kInfo = {{"counting", "Counting", 0, nullptr, false}, 3, kOutputs, false};
    return kInfo;
  }
  int blocks = 0;
  bool lastJump = false;
  uint8_t lastGates[kMaxNativeBlock] = {};
  double lastBeats[kMaxNativeBlock] = {};

 private:
  void resetEngine() override {
    index_ = 0;
    blocks = 0;
    lastJump = false;
  }
  void renderBlock(const ParamValues&, const uint8_t* gate, const double* beat, double, bool jump,
                   float (*block)[kMaxNativeBlock]) override {
    for (int i = 0; i < nativeBlock(); ++i) {
      block[0][i] = float(index_++);
      block[1][i] = float(gate[i]);
      block[2][i] = float(beat[i]);
      lastGates[i] = gate[i];
      lastBeats[i] = beat[i];
    }
    lastJump = jump;
    ++blocks;
  }
  int64_t index_ = 0;
};

}  // namespace eg13::test
```

- [ ] **Step 2: Write the failing tests (`tests/test_mod_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "alloc_counter.h"
#include "engine/mod_engine.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

float tick(NativeModEngine& e, const ModContext& ctx, int n) {
  float out[kMaxModOutputs];
  e.process(ctx, ParamValues{}, n, out);
  return out[0];
}

}  // namespace

TEST_CASE("At the native rate the sampler reports the last sample of each tick") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  for (int t = 0; t < 10; ++t) REQUIRE(tick(e, ctx, 16) == float(16 * t + 15));
  REQUIRE(e.renderedSamples() == 160);
  REQUIRE(e.blocks == 20);
}

TEST_CASE("Short ticks hold, then report the samples whose time has come") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  REQUIRE(tick(e, ctx, 3) == 2.0f);
  REQUIRE(e.renderedSamples() == 8);
  REQUIRE(tick(e, ctx, 3) == 5.0f);
  REQUIRE(e.renderedSamples() == 8);  // still inside the first block
  REQUIRE(tick(e, ctx, 3) == 8.0f);
  REQUIRE(e.renderedSamples() == 16);
}

TEST_CASE("The sampler keeps native time at every host rate") {
  struct Engine {
    double rate;
    int block;
  };
  for (const Engine engine : {Engine{31250.0, 8}, Engine{62500.0, 8}, Engine{32000.0, 5}, Engine{48000.0, 4}}) {
    for (double hostRate : {44100.0, 48000.0, 96000.0, 176400.0}) {
      test::CountingModEngine e(engine.rate, engine.block);
      e.prepare(hostRate);
      e.reset();
      const ModContext ctx;
      float last = 0.0f;
      const int total = int(hostRate);  // one second
      for (int done = 0; done < total; done += 16) last = tick(e, ctx, std::min(16, total - done));
      INFO("native " << engine.rate << " host " << hostRate);
      REQUIRE(last == float(engine.rate - 1.0));
      REQUIRE(e.renderedSamples() >= int64_t(engine.rate));
      REQUIRE(e.renderedSamples() < int64_t(engine.rate) + engine.block);
    }
  }
}

TEST_CASE("Gate edges reach the engine at its next block") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  ModContext ctx;
  tick(e, ctx, 8);
  for (uint8_t g : e.lastGates) REQUIRE(g == kGateLow);
  ctx.gate = true;
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  for (int i = 1; i < 8; ++i) REQUIRE(e.lastGates[i] == kGateHigh);
  ctx.retrigger = true;  // a new note while the gate is high
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  REQUIRE(e.lastGates[1] == kGateHigh);
  ctx.retrigger = false;
  ctx.gate = false;
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == kGateFalling);
  REQUIRE(e.lastGates[1] == kGateLow);
  ctx.retrigger = true;  // a note that started and ended within one tick
  tick(e, ctx, 8);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
  REQUIRE(e.lastGates[1] == kGateFalling);
  REQUIRE(e.lastGates[2] == kGateLow);
}

TEST_CASE("A retrigger waits for the next rendered block") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(48000.0);
  e.reset();
  ModContext ctx;
  ctx.gate = true;
  ctx.retrigger = true;
  tick(e, ctx, 1);  // 0.65 native samples: nothing rendered yet
  REQUIRE(e.blocks == 0);
  ctx.retrigger = false;
  tick(e, ctx, 1);
  REQUIRE(e.blocks == 1);
  REQUIRE(e.lastGates[0] == (kGateRising | kGateHigh));
}

TEST_CASE("Beat positions follow the host's tempo per native sample") {
  test::CountingModEngine e(32000.0, 5);
  e.prepare(48000.0);
  e.reset();
  ModContext ctx;
  ctx.beat = 10.0;
  ctx.beatsPerSample = 2.0 / 48000.0;  // 120 BPM
  ctx.beatJump = true;
  tick(e, ctx, 16);  // due: floor(16 * 2 / 3) = 10 native samples, two blocks
  REQUIRE(e.blocks == 2);
  REQUIRE_FALSE(e.lastJump);  // the jump went to the first block only
  const double perNative = 2.0 / 32000.0;
  for (int i = 0; i < 5; ++i) REQUIRE(std::fabs(e.lastBeats[i] - (10.0 + double(5 + i) * perNative)) < 1e-12);
}

TEST_CASE("Reset starts the sampler over") {
  test::CountingModEngine e(31250.0, 8);
  e.prepare(31250.0);
  e.reset();
  const ModContext ctx;
  for (int t = 0; t < 5; ++t) tick(e, ctx, 16);
  e.reset();
  REQUIRE(e.renderedSamples() == 0);
  REQUIRE(tick(e, ctx, 16) == 15.0f);
}

TEST_CASE("The Off mod engine outputs zeros") {
  OffModEngine off;
  float out[kMaxModOutputs];
  std::fill(std::begin(out), std::end(out), 1.0f);
  off.process(ModContext{}, ParamValues{}, 16, out);
  for (float v : out) REQUIRE(v == 0.0f);
  REQUIRE(off.info().outputCount == 0);
}

TEST_CASE("Envelope shapes are sanitized") {
  EnvelopeShape s;
  s.count = 0;
  s.type[1] = 9;
  s.loop[2] = 5;
  const EnvelopeShape clean = sanitized(s);
  REQUIRE(clean.count == 1);
  REQUIRE(clean.type[1] == EnvelopeShape::Ramp);
  REQUIRE(clean.loop[2] == 1);
  s.count = 40;
  REQUIRE(sanitized(s).count == EnvelopeShape::kMaxSegments);
  REQUIRE(sanitized(EnvelopeShape{}) == EnvelopeShape{});
}

TEST_CASE("Mod option steps are fixed") {
  REQUIRE(modOption(0.0f, 4) == 0);
  REQUIRE(modOption(modOptionValue(2), 4) == 2);
  REQUIRE(modOption(1.0f, 4) == 3);  // past the last option: the last option
  REQUIRE(modOption(modOptionValue(3), 3) == 2);
}

TEST_CASE("The sampler does not allocate") {
  test::CountingModEngine e(62500.0, 8);
  e.prepare(44100.0);
  ModContext ctx;
  float out[kMaxModOutputs];
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    e.reset();
    for (int t = 0; t < 1000; ++t) {
      ctx.gate = (t / 50) % 2 == 0;
      e.process(ctx, ParamValues{}, 1 + t % 16, out);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/mod_engine.h` not found).

- [ ] **Step 4: Write `core/engine/mod_engine.h`**

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "engine/param_format.h"
#include "engine/voice_engine.h"

namespace eg13 {

constexpr int kMaxModOutputs = 8;   // Marbles has seven (T1-T3, X1-X3, Y)
constexpr int kModParamCount = 16;  // P1..P16 of a mod slot (spec §7.1)

// Stepped mod-engine P's are append-only option lists read in kModSelectorSteps fixed steps, as
// the Ring Mod / Vocoder's selectors are (warps_engine.h): adding an option never changes what a
// saved value means. Steps past the last option read as the last option. RULE: when an option is
// appended, that version's state migration snaps every saved value above the old last option's
// step back to modOptionValue(old last), and a host default that relied on "past the end reads
// as the last option" is changed to modOptionValue(old last).
constexpr int kModSelectorSteps = 16;
inline int modOption(float normalized, int options) {
  return std::min(stepIndex(normalized, kModSelectorSteps), options - 1);
}
inline float modOptionValue(int option) { return (float(option) + 0.5f) / float(kModSelectorSteps); }

struct ModOutputInfo {
  const char* name;
  bool bipolar;  // -1..1; otherwise 0..1
};

// A mod engine's description: its parameters as an EngineInfo, so slot code names and formats
// them like any other slot's, plus its outputs.
struct ModEngineInfo {
  EngineInfo engine;
  int outputCount;
  const ModOutputInfo* outputs;
  bool perPartByDefault;  // the scope a fresh slot gives this engine (spec §4: Random per part)
};

// Patch data of the Envelope's Custom mode (spec §7.1: multi-segment shapes are patch data): the
// type and loop flag of each of up to six segments, as the module's buttons set them.
struct EnvelopeShape {
  static constexpr int kMaxSegments = 6;
  enum Type : uint8_t { Ramp = 0, Step = 1, Hold = 2 };
  uint8_t count = 4;  // the default is an ADSR: Ramp, Ramp, Hold (looping), Ramp
  uint8_t type[kMaxSegments] = {Ramp, Ramp, Hold, Ramp, Ramp, Ramp};
  uint8_t loop[kMaxSegments] = {0, 0, 1, 0, 0, 0};
};
bool operator==(const EnvelopeShape& a, const EnvelopeShape& b);
inline bool operator!=(const EnvelopeShape& a, const EnvelopeShape& b) { return !(a == b); }
// count 1..6; unknown types read as Ramp; loop flags 0/1; entries past count reset to defaults.
EnvelopeShape sanitized(const EnvelopeShape& shape);

// Gate flag bits, as stmlib/utils/gate_flags.h and peaks/gate_processor.h define them (each
// adapter checks this with a static_assert).
enum : uint8_t { kGateLow = 0, kGateHigh = 1, kGateRising = 2, kGateFalling = 4 };

// What a mod engine hears during one control tick.
struct ModContext {
  bool gate = false;       // high while the note (per-voice) or the part's keys (per-part) are held
  bool retrigger = false;  // a note started since the last tick: an edge even if the gate was high
  double beat = 0.0;       // beat position (quarter notes) at the start of the tick
  double beatsPerSample = 0.0;  // at the host rate
  bool beatJump = false;   // the transport started or jumped: tempo-synced engines lock to it
};

// A modulation source (spec §3.1): up to kMaxModOutputs control outputs.
class ModEngine {
 public:
  virtual ~ModEngine() = default;
  virtual void prepare(double hostRate) = 0;  // main thread; the only place that may allocate
  virtual void reset() = 0;                   // audio-thread safe
  // Advances n host samples and writes every output's value at the end of them into
  // out[0..kMaxModOutputs) (0 beyond the engine's outputCount).
  virtual void process(const ModContext& ctx, const ParamValues& p, int n, float* out) = 0;
  virtual const ModEngineInfo& info() const = 0;
  virtual void setShape(const EnvelopeShape&) {}  // Envelope's Custom mode; others ignore it
  virtual void setSeed(uint32_t) {}               // engines with their own random generator
};

// Runs a firmware module at its native rate and block size. Whole native blocks are rendered as
// host time reaches them (up to one block ahead); each process() reports the newest native
// sample whose time has come. Gate changes take effect at the first native sample of the next
// rendered block. At host rate = native rate with n a multiple of the block it is the bare
// module, sample for sample.
class NativeModEngine : public ModEngine {
 public:
  static constexpr int kMaxNativeBlock = 8;

  NativeModEngine(double nativeRate, int nativeBlock);
  void prepare(double hostRate) override;
  void reset() final;
  void process(const ModContext& ctx, const ParamValues& p, int n, float* out) final;

  double nativeRate() const { return nativeRate_; }
  int nativeBlock() const { return nativeBlock_; }
  int64_t renderedSamples() const { return rendered_; }  // tests

 protected:
  virtual void resetEngine() = 0;
  // One native block: gate[i] gate flags, beat[i] beat position of native sample i. Writes
  // block[k][i] for every output k < info().outputCount.
  virtual void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                           bool beatJump, float (*block)[kMaxNativeBlock]) = 0;

 private:
  double nativeRate_;
  int nativeBlock_;
  double ratio_ = 1.0;     // native samples per host sample
  int64_t hostSamples_ = 0;  // host samples elapsed since reset
  int64_t rendered_ = 0;   // native samples rendered
  bool high_ = false;      // the gate at the last rendered native sample
  bool retrigger_ = false;
  bool fall_ = false;      // a retrigger with the gate already low: fall on the next sample
  bool jump_ = false;
  float block_[kMaxModOutputs][kMaxNativeBlock] = {};
  float held_[kMaxModOutputs] = {};
};

// The empty mod slot.
class OffModEngine final : public ModEngine {
 public:
  static const ModEngineInfo& staticInfo();
  void prepare(double) override {}
  void reset() override {}
  void process(const ModContext&, const ParamValues&, int, float* out) override {
    std::fill(out, out + kMaxModOutputs, 0.0f);
  }
  const ModEngineInfo& info() const override { return staticInfo(); }
};

}  // namespace eg13
```

- [ ] **Step 5: Write `core/engine/mod_engine.cpp`**

```cpp
#include "engine/mod_engine.h"

#include <cmath>

namespace eg13 {

bool operator==(const EnvelopeShape& a, const EnvelopeShape& b) {
  if (a.count != b.count) return false;
  for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
    if (a.type[i] != b.type[i] || a.loop[i] != b.loop[i]) return false;
  }
  return true;
}

EnvelopeShape sanitized(const EnvelopeShape& shape) {
  EnvelopeShape s;
  s.count = uint8_t(std::clamp(int(shape.count), 1, EnvelopeShape::kMaxSegments));
  for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
    s.type[i] = shape.type[i] <= EnvelopeShape::Hold ? shape.type[i] : uint8_t(EnvelopeShape::Ramp);
    s.loop[i] = shape.loop[i] != 0 ? 1 : 0;
  }
  return s;
}

NativeModEngine::NativeModEngine(double nativeRate, int nativeBlock)
    : nativeRate_(nativeRate), nativeBlock_(std::clamp(nativeBlock, 1, kMaxNativeBlock)) {}

void NativeModEngine::prepare(double hostRate) {
  ratio_ = hostRate > 0.0 && nativeRate_ > 0.0 ? nativeRate_ / hostRate : 1.0;
}

void NativeModEngine::reset() {
  hostSamples_ = 0;
  rendered_ = 0;
  high_ = false;
  retrigger_ = false;
  fall_ = false;
  jump_ = false;
  for (auto& output : block_) std::fill(std::begin(output), std::end(output), 0.0f);
  std::fill(std::begin(held_), std::end(held_), 0.0f);
  resetEngine();
}

void NativeModEngine::process(const ModContext& ctx, const ParamValues& p, int n, float* out) {
  retrigger_ = retrigger_ || ctx.retrigger;
  jump_ = jump_ || ctx.beatJump;
  // Native time is computed from the host sample count, not accumulated, so it never drifts. The
  // epsilon absorbs the rounding of exact ratios (31250 / 31250, 32000 / 48000 * 3, ...).
  const double start = double(hostSamples_) * ratio_;
  hostSamples_ += std::max(n, 0);
  const int64_t due = int64_t(std::floor(double(hostSamples_) * ratio_ + 1e-6));
  const double beatsPerNative = ctx.beatsPerSample / ratio_;
  while (rendered_ < due) {
    uint8_t gate[kMaxNativeBlock];
    double beat[kMaxNativeBlock];
    for (int i = 0; i < nativeBlock_; ++i) {
      uint8_t flags;
      if (retrigger_) {  // a new note: rising edge, high
        flags = kGateRising | kGateHigh;
        high_ = true;
        retrigger_ = false;
        fall_ = !ctx.gate;  // already released within the tick: a one-sample trigger
      } else if (ctx.gate && !fall_) {
        flags = high_ ? kGateHigh : uint8_t(kGateRising | kGateHigh);
        high_ = true;
      } else {
        flags = high_ ? kGateFalling : kGateLow;
        high_ = false;
        fall_ = false;
      }
      gate[i] = flags;
      beat[i] = ctx.beat + (double(rendered_ + i) - start) * beatsPerNative;
    }
    renderBlock(p, gate, beat, beatsPerNative, jump_, block_);
    jump_ = false;
    rendered_ += nativeBlock_;
  }
  const int64_t index = due - 1 - (rendered_ - nativeBlock_);
  if (due > 0 && index >= 0 && index < nativeBlock_) {
    for (int k = 0; k < kMaxModOutputs; ++k) held_[k] = block_[k][index];
  }
  std::copy(std::begin(held_), std::end(held_), out);
}

const ModEngineInfo& OffModEngine::staticInfo() {
  static const ModEngineInfo kInfo = {{"off", "Off", 0, nullptr, false}, 0, nullptr, false};
  return kInfo;
}

}  // namespace eg13
```

Add `engine/mod_engine.cpp` to the `add_library(eg13_core STATIC ...)` list in `core/CMakeLists.txt`, after `engine/warps_engine.cpp`.

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "sampler|Gate edges|retrigger waits|Beat positions|Off mod engine|Envelope shapes are|Mod option steps"`
Expected: all pass. Then the full suite.

- [ ] **Step 7: Document how modulation runs**

Append to `docs/manual/modulation.md`:

````markdown
## How modulation runs

Each modulation engine runs at its module's own rate, as on the hardware (Envelope 31.25 kHz, LFO 62.5 kHz, Random / S&H 32 kHz, Function Generator 48 kHz), whatever your project's sample rate, so its times and rates are the module's. EG13 reads every engine's outputs once every 16 samples (a third of a millisecond at 48 kHz), works out the routes, and moves the results smoothly across the next 16 samples (see [Smoothing](smoothing.md)). A key press reaches a modulation engine within a quarter of a millisecond.
````

- [ ] **Step 8: Commit**

```bash
git add core/engine/mod_engine.h core/engine/mod_engine.cpp core/CMakeLists.txt tests/test_mod_engines.h tests/test_mod_engine.cpp docs/manual/modulation.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "ModEngine interface and the native-rate control sampler"
```

---
### Task 3: Envelope (Stages)

**Files:**
- Create: `core/engine/stages_engine.h`, `core/engine/stages_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/stages_engine.cpp`), `docs/manual/modulation.md`, `docs/manual/smoothing.md`
- Test: `tests/test_stages_engine.cpp`

**Interfaces:**
- Consumes: `NativeModEngine`, `ModEngineInfo`, `EnvelopeShape`, `sanitized`, `modOption`, `modOptionValue`, `kGate*` (Task 2); `formatPercent01`, `clamp01` (existing).
- Produces: `class StagesEngine final : public NativeModEngine` ("Envelope", id `"stages"`, 13 P's, 1 unipolar output "Envelope", per voice by default): `static constexpr double kNativeRate = 31250.0; static constexpr int kNativeBlock = 8; enum Mode { Adsr, Ad, Looping, Custom }; static constexpr int kNumModes = 4; static const ModEngineInfo& staticInfo(); static int modeIndex(float); static float segmentSeconds(float primary); static EnvelopeShape modeShape(int mode, const EnvelopeShape& custom); void setShape(const EnvelopeShape&) override;`. P1 Mode, P2 Attack, P3 Attack Shape, P4 Decay, P5 Decay Shape, P6 Sustain, P7 Sustain Time, P8 Release, P9 Release Shape, P10 Seg 5 A, P11 Seg 5 B, P12 Seg 6 A, P13 Seg 6 B; defaults 0, 0, .5, .5, .5, 1, .5, .5, .5, 0, 0, 0, 0 (the shared host defaults, provisional ruling 1).

The defaults matter beyond this engine: they equal the mod slots' shared host defaults (Task 13), so a host reset and a fresh engine agree.

- [ ] **Step 1: Write the failing tests (`tests/test_stages_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/stages_engine.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

constexpr int kSamples = 40000;  // 1.28 s at 31250 Hz, a multiple of 16

ParamValues defaults() { return test::defaultParams(StagesEngine::staticInfo().engine); }

// Gate high on native samples [160, 20000): both ends are multiples of 16.
std::vector<bool> gatePattern() {
  std::vector<bool> g(kSamples, false);
  for (int i = 160; i < 20000; ++i) g[size_t(i)] = true;
  return g;
}

// The module's loop for one gated group (stages/stages.cc Process, with the parameter reads of
// VCV's Stages.cpp stepBlock): Configure once, then per 8-sample block set every segment's
// slider (primary) and pot (secondary) and Process.
std::vector<float> reference(const stages::segment::Configuration* config, int count, const ParamValues& p,
                             const std::vector<bool>& gate) {
  static stages::SegmentGenerator g;
  g.Init();
  g.Configure(true, config, count);
  std::vector<float> out(gate.size());
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  stages::SegmentGenerator::Output o[8];
  for (size_t b = 0; b < gate.size(); b += 8) {
    stmlib::GateFlags flags[8];
    for (size_t i = 0; i < 8; ++i) previous = flags[i] = stmlib::ExtractGateFlags(previous, gate[b + i]);
    for (int k = 0; k < count; ++k) g.set_segment_parameters(k, p.p[1 + 2 * k], p.p[2 + 2 * k]);
    g.Process(flags, o, 8);
    for (size_t i = 0; i < 8; ++i) out[b + i] = o[i].value;
  }
  return out;
}

// The adapter at the native rate in ticks of n samples; out[t] is the value after tick t.
std::vector<float> render(StagesEngine& e, const ParamValues& p, const std::vector<bool>& gate, int n) {
  std::vector<float> out(gate.size() / size_t(n));
  for (size_t t = 0; t < out.size(); ++t) {
    ModContext ctx;
    ctx.gate = gate[t * size_t(n)];
    float o[kMaxModOutputs];
    e.process(ctx, p, n, o);
    out[t] = o[0];
  }
  return out;
}

ParamValues shaped(int mode) {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(mode);
  const float values[12] = {0.2f, 0.3f, 0.35f, 0.7f, 0.6f, 0.4f, 0.3f, 0.6f, 0.25f, 0.5f, 0.45f, 0.2f};
  for (int i = 0; i < 12; ++i) p.p[1 + i] = values[i];
  return p;
}

void requireMatches(StagesEngine& e, const ParamValues& p, const stages::segment::Configuration* config, int count) {
  const auto gate = gatePattern();
  const auto ref = reference(config, count, p, gate);
  for (int n : {8, 16}) {
    e.reset();
    const auto out = render(e, p, gate, n);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("tick " << t << " of " << n);
      REQUIRE(out[t] == ref[(t + 1) * size_t(n) - 1]);
    }
  }
}

}  // namespace

TEST_CASE("Envelope describes itself") {
  const ModEngineInfo& info = StagesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "stages");
  REQUIRE(std::string(info.engine.displayName) == "Envelope");
  REQUIRE(info.engine.paramCount == 13);
  REQUIRE(info.outputCount == 1);
  REQUIRE_FALSE(info.outputs[0].bipolar);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[13] = {"Mode", "Attack", "Attack Shape", "Decay", "Decay Shape", "Sustain", "Sustain Time",
                                 "Release", "Release Shape", "Seg 5 A", "Seg 5 B", "Seg 6 A", "Seg 6 B"};
  for (int i = 0; i < 13; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  char buf[32];
  const char* const modes[4] = {"ADSR", "AD", "Looping", "Custom"};
  for (int m = 0; m < 4; ++m) {
    info.engine.params[0].format(modOptionValue(m), buf, sizeof(buf));
    REQUIRE(std::string(buf) == modes[m]);
  }
  info.engine.params[0].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Custom");  // steps past the last option read as the last
  info.engine.params[1].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1 ms");
  info.engine.params[3].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "505 ms");
  info.engine.params[7].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "16.02 s");
}

TEST_CASE("Envelope matches Stages' segment generator sample for sample") {
  using stages::segment::TYPE_HOLD;
  using stages::segment::TYPE_RAMP;
  StagesEngine e;
  e.prepare(StagesEngine::kNativeRate);
  SECTION("ADSR") {
    const stages::segment::Configuration c[4] = {{TYPE_RAMP, false}, {TYPE_RAMP, false}, {TYPE_HOLD, true}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Adsr), c, 4);
  }
  SECTION("AD") {
    const stages::segment::Configuration c[2] = {{TYPE_RAMP, false}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Ad), c, 2);
  }
  SECTION("Looping") {
    const stages::segment::Configuration c[2] = {{TYPE_RAMP, true}, {TYPE_RAMP, true}};
    requireMatches(e, shaped(StagesEngine::Looping), c, 2);
  }
  SECTION("Custom: ramp, step, hold, ramp, step, ramp") {
    EnvelopeShape s;
    s.count = 6;
    const uint8_t types[6] = {EnvelopeShape::Ramp, EnvelopeShape::Step, EnvelopeShape::Hold,
                              EnvelopeShape::Ramp, EnvelopeShape::Step, EnvelopeShape::Ramp};
    for (int i = 0; i < 6; ++i) {
      s.type[i] = types[i];
      s.loop[i] = i == 2 || i == 3 ? 1 : 0;
    }
    e.setShape(s);
    using stages::segment::TYPE_STEP;
    const stages::segment::Configuration c[6] = {{TYPE_RAMP, false}, {TYPE_STEP, false}, {TYPE_HOLD, true},
                                                 {TYPE_RAMP, true},  {TYPE_STEP, false}, {TYPE_RAMP, false}};
    requireMatches(e, shaped(StagesEngine::Custom), c, 6);
  }
}

TEST_CASE("Envelope sustains at its level and releases to zero") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[5] = 0.6f;  // Sustain
  p.p[7] = 0.3f;  // Release, about 72 ms
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);  // 1 s held
  REQUIRE(std::fabs(out[0] - 0.6f) < 1e-3f);
  ctx.gate = false;
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] < 1e-4f);
}

TEST_CASE("Envelope times hold at 44.1, 48 and 96 kHz") {
  auto attackSeconds = [](double rate) {
    StagesEngine e;
    e.prepare(rate);
    e.reset();
    ParamValues p = defaults();
    p.p[0] = modOptionValue(StagesEngine::Ad);
    p.p[1] = 0.3f;  // attack about 72 ms
    ModContext ctx;
    ctx.gate = true;
    float out[kMaxModOutputs] = {};
    int samples = 0;
    while (out[0] < 0.999f && samples < int(rate)) {
      e.process(ctx, p, 16, out);
      samples += 16;
    }
    return double(samples) / rate;
  };
  const double expected = double(StagesEngine::segmentSeconds(0.3f));
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("rate " << rate);
    REQUIRE(std::fabs(attackSeconds(rate) - expected) < 16.0 / rate + 8.0 / StagesEngine::kNativeRate);
  }
}

TEST_CASE("Changing the Envelope mode while held keeps the output in range") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[5] = 0.5f;
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 3000; ++t) e.process(ctx, p, 16, out);
  for (int mode : {StagesEngine::Looping, StagesEngine::Ad, StagesEngine::Custom, StagesEngine::Adsr}) {
    p.p[0] = modOptionValue(mode);
    for (int t = 0; t < 2000; ++t) {
      e.process(ctx, p, 16, out);
      REQUIRE(out[0] >= 0.0f);
      REQUIRE(out[0] <= 1.0f);
    }
  }
}

TEST_CASE("Envelope survives NaN and does not allocate") {
  StagesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    REQUIRE(std::isfinite(out[0]));
  }
  ParamValues p = defaults();
  EnvelopeShape custom;
  custom.count = 3;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 2000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.retrigger = t % 300 == 0;
      p.p[0] = modOptionValue(t / 500);
      if (t == 1600) e.setShape(custom);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

The reference is the module's own loop, written from `stages/stages.cc` and VCV's `Stages.cpp` parameter reads, driving `stages::SegmentGenerator` directly; it never calls `StagesEngine`. The gate changes only on multiples of 16 native samples, where the adapter's blocks start, so the reference sees the same flags.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/stages_engine.h` not found).

- [ ] **Step 3: Write `core/engine/stages_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Stages' segment generator (stages/segment_generator.h), shown as "Envelope". One group of up
// to six segments with the gate patched into its first segment, as on the module. Runs at the
// module's 31.25 kHz in its 8-sample blocks. One output, 0..1.
//
// P1 Mode: ADSR, AD, Looping, Custom (append-only, kModSelectorSteps). Segment k (0..5) reads
// P(2 + 2k) as its primary (the slider: a Ramp's time, a Hold's or Step's level) and P(3 + 2k) as
// its secondary (the pot: a Ramp's shape, a Hold's time, a Step's portamento), as VCV's Stages
// reads its sliders and pots (AudibleInstruments Stages.cpp:226-227). ADSR plays segments 1-4,
// AD and Looping segments 1-2, Custom the segments of its EnvelopeShape (patch data).
class StagesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 31250.0;  // stages/segment_generator.h:43
  static constexpr int kNativeBlock = 8;          // stages/io_buffer.h:40
  enum Mode { Adsr = 0, Ad = 1, Looping = 2, Custom = 3 };
  static constexpr int kNumModes = 4;

  StagesEngine();
  ~StagesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();
  void setShape(const EnvelopeShape& shape) override { custom_ = sanitized(shape); }

  static int modeIndex(float normalized);  // P1
  // A Ramp segment's duration at this primary value: 1 / (lut_env_frequency[p * 2048] * 31250).
  static float segmentSeconds(float primary);
  // The segments each mode plays; Custom plays `custom`.
  static EnvelopeShape modeShape(int mode, const EnvelopeShape& custom);

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  EnvelopeShape custom_;
  EnvelopeShape configured_;
  bool configure_ = true;  // Configure() before the next block
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/stages_engine.cpp`**

```cpp
#include "engine/stages_engine.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "stages/resources.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace eg13 {

static_assert(kGateHigh == stmlib::GATE_FLAG_HIGH && kGateRising == stmlib::GATE_FLAG_RISING &&
                  kGateFalling == stmlib::GATE_FLAG_FALLING,
              "EG13's gate flags are stmlib's");

namespace {

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[StagesEngine::kNumModes] = {"ADSR", "AD", "Looping", "Custom"};

void formatMode(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kModeNames[StagesEngine::modeIndex(v)]);
}

void formatTime(float v, char* buf, size_t size) {
  const float seconds = StagesEngine::segmentSeconds(v);
  if (seconds < 1.0f) {
    std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
  } else {
    std::snprintf(buf, size, "%.2f s", double(seconds));
  }
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): an ADSR
// with a 1 ms attack, 0.5 s decay, full sustain and 0.5 s release, linear shapes (0.5).
const EngineParamInfo kParams[13] = {
    {"Mode", 0.0f, formatMode},
    {"Attack", 0.0f, formatTime},
    {"Attack Shape", 0.5f, formatPercent01},
    {"Decay", 0.5f, formatTime},
    {"Decay Shape", 0.5f, formatPercent01},
    {"Sustain", 1.0f, formatPercent01},
    {"Sustain Time", 0.5f, formatTime},
    {"Release", 0.5f, formatTime},
    {"Release Shape", 0.5f, formatPercent01},
    {"Seg 5 A", 0.0f, formatPercent01},
    {"Seg 5 B", 0.0f, formatPercent01},
    {"Seg 6 A", 0.0f, formatPercent01},
    {"Seg 6 B", 0.0f, formatPercent01},
};

const ModOutputInfo kOutputs[1] = {{"Envelope", false}};

const ModEngineInfo kInfo = {{"stages", "Envelope", 13, kParams, false}, 1, kOutputs, false};

stages::segment::Type segmentType(uint8_t type) {
  return type == EnvelopeShape::Step   ? stages::segment::TYPE_STEP
         : type == EnvelopeShape::Hold ? stages::segment::TYPE_HOLD
                                       : stages::segment::TYPE_RAMP;
}

EnvelopeShape preset(int count, std::initializer_list<uint8_t> types, std::initializer_list<uint8_t> loops) {
  EnvelopeShape s;
  s.count = uint8_t(count);
  int i = 0;
  for (uint8_t t : types) s.type[i++] = t;
  i = 0;
  for (uint8_t l : loops) s.loop[i++] = l;
  return s;
}

}  // namespace

struct StagesEngine::Impl {
  stages::SegmentGenerator generator;
  stages::SegmentGenerator::Output out[kNativeBlock];
};

StagesEngine::StagesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  impl_->generator.Init();
}

StagesEngine::~StagesEngine() = default;

const ModEngineInfo& StagesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& StagesEngine::info() const { return kInfo; }

int StagesEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

float StagesEngine::segmentSeconds(float primary) {
  // SegmentGenerator::RateToFrequency (segment_generator.cc:132-136).
  const int i = int(clamp01(primary) * 2048.0f);
  return 1.0f / (stages::lut_env_frequency[i] * float(kNativeRate));
}

EnvelopeShape StagesEngine::modeShape(int mode, const EnvelopeShape& custom) {
  using S = EnvelopeShape;
  switch (mode) {
    case Adsr: return preset(4, {S::Ramp, S::Ramp, S::Hold, S::Ramp}, {0, 0, 1, 0});
    case Ad: return preset(2, {S::Ramp, S::Ramp}, {0, 0});
    case Looping: return preset(2, {S::Ramp, S::Ramp}, {1, 1});
    default: return sanitized(custom);
  }
}

void StagesEngine::resetEngine() {
  impl_->generator.Init();
  configure_ = true;
}

void StagesEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double*, double, bool,
                               float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const EnvelopeShape shape = modeShape(modeIndex(p.p[0]), custom_);
  if (configure_ || shape != configured_) {
    // As the module does when its segment types, loops or groups change (VCV Stages.cpp
    // stepBlock): Configure() keeps the generator's state, so playing continues from wherever it
    // is in the new layout.
    stages::segment::Configuration config[EnvelopeShape::kMaxSegments];
    for (int i = 0; i < shape.count; ++i) config[i] = {segmentType(shape.type[i]), shape.loop[i] != 0};
    s.generator.Configure(true, config, shape.count);
    configured_ = shape;
    configure_ = false;
  }
  for (int i = 0; i < shape.count; ++i) {
    s.generator.set_segment_parameters(i, clamp01(p.p[1 + 2 * i]), clamp01(p.p[2 + 2 * i]));
  }
  s.generator.Process(gate, s.out, kNativeBlock);
  for (int i = 0; i < kNativeBlock; ++i) block[0][i] = s.out[i].value;
}

}  // namespace eg13
```

Add `engine/stages_engine.cpp` to `core/CMakeLists.txt` after `engine/mod_engine.cpp`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Envelope"`
Expected: all pass (a planning build of exactly this code passed them, with 46 533 assertions). Then the full suite.

- [ ] **Step 6: Document the Envelope**

Append to `docs/manual/modulation.md`:

````markdown
## Mod engines

### Envelope (based on Stages)

An envelope made of up to six segments, as one gated group of the module. One output, from 0 to 100 %.

- **Mode**: *ADSR* (Attack, Decay, Sustain, Release), *AD* (Attack then Decay; it always runs to the end, even if you let go early), *Looping* (Attack and Decay repeat for as long as the voice plays: a simple LFO with shapeable slopes) or *Custom*.
- **Attack**, **Decay**, **Release** are times from 1 ms to 16 s (half-way is about half a second). **Sustain** is a level. Each **Shape** bends its segment: 50 % is a straight line; lower and higher values curve it one way or the other.
- **Custom** plays up to six segments whose kinds (*Ramp*, *Step* or *Hold*) and loops are part of the patch, as you would set them with the module's buttons; the default custom shape is the ADSR. Each segment has two controls, in order: Attack and Attack Shape are segment 1's, Decay and Decay Shape segment 2's, Sustain and Sustain Time segment 3's, Release and Release Shape segment 4's, then Seg 5 A/B and Seg 6 A/B. As on the module, a Ramp's first control is its time and the second its shape; a Hold's first is its level and the second its time; a Step's first is its level and the second its glide. (EG13's own editor for Custom shapes arrives with the interface.)
- With a single segment, Custom plays the module's single-segment functions, as one Stages channel with its gate patched does: a Ramp is a decay envelope (looping: an LFO tapped by the notes), a Step samples and holds its level on each note (with glide), a Hold plays a pulse at its level for the time its second control sets (looping: a gate that lets each note through with a probability set by its second control).
- A new note on the voice restarts the attack from wherever the envelope is, so fast repeated notes do not click.
- Changing Mode while the envelope runs continues from wherever it is in the new layout, as on the module.
- Defaults: ADSR, 1 ms attack, 0.5 s decay, full sustain, 0.5 s release, straight segments.
````

In `docs/manual/smoothing.md`, insert before the paragraph that begins "After a reset":

```markdown
- **Envelope:** no smoothing, as on the module: its controls are read every 8 samples of its own 31.25 kHz (a quarter of a millisecond). Mode switches at once.
```

- [ ] **Step 7: Commit**

```bash
git add core/engine/stages_engine.h core/engine/stages_engine.cpp core/CMakeLists.txt tests/test_stages_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Envelope: Stages' segment generator with ADSR, AD, Looping and Custom modes"
```

---
### Task 4: LFO (Tides 2) with tempo sync

**Files:**
- Create: `core/engine/tides_engine.h`, `core/engine/tides_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/tides_engine.cpp`), `docs/manual/modulation.md`, `docs/manual/smoothing.md`
- Test: `tests/test_tides_engine.cpp`

**Interfaces:**
- Consumes: `NativeModEngine`, `ModContext` (`beat`, `beatsPerSample`), `modOption`, `modOptionValue` (Task 2).
- Produces: `class TidesEngine final : public NativeModEngine` ("LFO", id `"tides"`, 11 P's, 4 bipolar outputs "Out 1".."Out 4", per voice by default): `static constexpr double kNativeRate = 62500.0; static constexpr int kNativeBlock = 8; enum Mode { Cycle, Ad, Ar }; enum Output { Phase, Gates, Amplitude, Frequency }; enum Range { Medium, Low }; static int modeIndex(float); static bool syncOn(float); static float transposition(float); static int outputIndex(float); static int rangeIndex(float); static float rootHz(int range); static float frequencyFor(double hz, int range);`. P1 Mode, P2 Sync, P3 Frequency, P4 Shape, P5 Slope, P6 Shift, P7 Smoothness, P8–P9 "(unused)", P10 Output, P11 Range; defaults 0, 0, .5, .5, .5, 1, .5, .5, .5, 0, 0.

Tempo sync (owner decision 4, provisional ruling 19): with Sync on, the LFO's ramp is computed from the beat position the Part passes in `ModContext` — `fract(beat × ratio)`, with the ratio picked from Tides' own clocked-mode table by Frequency — so it is phase-locked while the transport plays and free-runs at the last tempo while it is stopped (the beat clock, Task 15, keeps advancing). No clock extraction is needed.

- [ ] **Step 1: Write the failing tests (`tests/test_tides_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/tides_engine.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"
#include "test_helpers.h"
#include "tides2/poly_slope_generator.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(TidesEngine::staticInfo().engine); }

// tides2/tides.cc Process for one 8-sample block with TRIG patched and CLOCK unpatched, written
// from the firmware: the range root and transposition give the frequency, the slope/phase and
// frequency output modes run at half rate on the even gate samples, and a change of output mode
// resets the generator.
class Reference {
 public:
  Reference() { g_.Init(); }
  void block(tides::RampMode mode, tides::OutputMode output, float rootHz, float transposition, float slope,
             float shape, float smoothness, float shift, const stmlib::GateFlags* gate, float (*out)[8]) {
    const bool half = output >= tides::OUTPUT_MODE_SLOPE_PHASE;
    size_t size = 8;
    stmlib::GateFlags flags[8];
    std::copy(gate, gate + 8, flags);
    float frequency = rootHz / 62500.0f * stmlib::SemitonesToRatio(transposition);
    if (half) {
      size = 4;
      for (size_t i = 0; i < 4; ++i) flags[i] = flags[2 * i];
      frequency *= 2.0f;
    }
    if (first_ || output != previous_) {
      g_.Reset();
      previous_ = output;
      first_ = false;
    }
    tides::PolySlopeGenerator::OutputSample o[8];
    g_.Render(mode, output, tides::RANGE_CONTROL, frequency, slope, shape, smoothness, shift, flags, nullptr, o, size);
    for (size_t i = 0; i < 8; ++i) {
      for (int k = 0; k < 4; ++k) out[k][i] = o[half ? i / 2 : i].channel[k];
    }
  }

 private:
  tides::PolySlopeGenerator g_;
  tides::OutputMode previous_ = tides::OUTPUT_MODE_GATES;
  bool first_ = true;
};

struct Setup {
  int mode;
  tides::RampMode rampMode;
  int output;
  tides::OutputMode outputMode;
};

void requireMatches(const Setup& setup) {
  TidesEngine e;
  e.prepare(TidesEngine::kNativeRate);
  e.reset();
  ParamValues p = defaults();
  p.p[0] = modOptionValue(setup.mode);
  p.p[2] = 0.62f;  // +11.5 semitones
  p.p[3] = 0.3f;   // shape
  p.p[4] = 0.7f;   // slope
  p.p[5] = 0.4f;   // shift
  p.p[6] = 0.35f;  // smoothness (low-pass)
  p.p[9] = modOptionValue(setup.output);
  Reference ref;
  stmlib::GateFlags previous = stmlib::GATE_FLAG_LOW;
  for (int b = 0; b < 8000; ++b) {  // about 1 s
    const bool gate = (b / 600) % 2 == 1;
    ModContext ctx;
    ctx.gate = gate;
    float out[kMaxModOutputs];
    e.process(ctx, p, 8, out);
    stmlib::GateFlags flags[8];
    for (auto& f : flags) previous = f = stmlib::ExtractGateFlags(previous, gate);
    float expected[4][8];
    ref.block(setup.rampMode, setup.outputMode, 2.0f, TidesEngine::transposition(0.62f), 0.7f, 0.3f, 0.35f, 0.4f,
              flags, expected);
    const float scale = setup.rampMode == tides::RAMP_MODE_LOOPING ? 0.2f : 0.125f;
    for (int k = 0; k < 4; ++k) {
      INFO("block " << b << " output " << k);
      REQUIRE(out[k] == std::clamp(expected[k][7] * scale, -1.0f, 1.0f));
    }
  }
}

// Rising zero crossings of output 1 over `seconds` of free running at `rate`.
int cycles(double rate, double seconds, const ParamValues& p) {
  TidesEngine e;
  e.prepare(rate);
  e.reset();
  const ModContext ctx;
  float out[kMaxModOutputs];
  float previous = 0.0f;
  int rising = 0;
  const int total = int(rate * seconds);
  for (int done = 0; done < total; done += 16) {
    e.process(ctx, p, std::min(16, total - done), out);
    if (previous < 0.0f && out[0] >= 0.0f) ++rising;
    previous = out[0];
  }
  return rising;
}

}  // namespace

TEST_CASE("LFO describes itself") {
  const ModEngineInfo& info = TidesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "tides");
  REQUIRE(std::string(info.engine.displayName) == "LFO");
  REQUIRE(info.engine.paramCount == 11);
  REQUIRE(info.outputCount == 4);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[11] = {"Mode", "Sync", "Frequency", "Shape", "Slope", "Shift", "Smoothness",
                                 "(unused)", "(unused)", "Output", "Range"};
  for (int i = 0; i < 11; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  REQUIRE(info.engine.params[7].format == nullptr);
  char buf[32];
  info.engine.params[0].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Cycle");
  info.engine.params[9].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Phase");
  info.engine.params[10].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Medium");
  info.engine.params[2].format(0.75f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+24.0 st");
  REQUIRE(std::fabs(TidesEngine::frequencyFor(5.0, TidesEngine::Medium) - 0.66524f) < 1e-4f);
}

TEST_CASE("LFO matches Tides 2's slope generator sample for sample") {
  SECTION("Cycle, phase outputs (half rate)") {
    requireMatches({TidesEngine::Cycle, tides::RAMP_MODE_LOOPING, TidesEngine::Phase, tides::OUTPUT_MODE_SLOPE_PHASE});
  }
  SECTION("Cycle, gates outputs (full rate)") {
    requireMatches({TidesEngine::Cycle, tides::RAMP_MODE_LOOPING, TidesEngine::Gates, tides::OUTPUT_MODE_GATES});
  }
  SECTION("AD, amplitude outputs") {
    requireMatches({TidesEngine::Ad, tides::RAMP_MODE_AD, TidesEngine::Amplitude, tides::OUTPUT_MODE_AMPLITUDE});
  }
  SECTION("AR, frequency outputs") {
    requireMatches({TidesEngine::Ar, tides::RAMP_MODE_AR, TidesEngine::Frequency, tides::OUTPUT_MODE_FREQUENCY});
  }
}

TEST_CASE("LFO frequency holds at 44.1, 48 and 96 kHz") {
  const ParamValues p = defaults();  // 2 Hz
  for (double rate : {44100.0, 48000.0, 96000.0}) {
    INFO("rate " << rate);
    const int n = cycles(rate, 10.0, p);
    REQUIRE(n >= 19);
    REQUIRE(n <= 21);
  }
  ParamValues low = p;
  low.p[10] = modOptionValue(TidesEngine::Low);
  low.p[2] = TidesEngine::frequencyFor(1.0, TidesEngine::Low);
  const int n = cycles(48000.0, 10.0, low);
  REQUIRE(n >= 9);
  REQUIRE(n <= 11);
}

TEST_CASE("LFO outputs stay in range and the phase outputs are a quarter cycle apart") {
  TidesEngine e;
  e.prepare(48000.0);
  e.reset();
  const ParamValues p = defaults();  // 2 Hz: a cycle is 1500 ticks of 16 samples
  const ModContext ctx;
  float out[kMaxModOutputs];
  std::vector<float> out1, out2;
  for (int t = 0; t < 3000; ++t) {
    e.process(ctx, p, 16, out);
    for (int k = 0; k < 4; ++k) {
      REQUIRE(out[k] >= -1.0f);
      REQUIRE(out[k] <= 1.0f);
    }
    out1.push_back(out[0]);
    out2.push_back(out[1]);
  }
  REQUIRE(*std::min_element(out1.begin(), out1.end()) < -0.9f);
  REQUIRE(*std::max_element(out1.begin(), out1.end()) > 0.9f);
  // Shift 100 %: output 2 plays output 1's wave a quarter cycle (375 ticks) later.
  for (size_t t = 500; t < 3000; ++t) REQUIRE(std::fabs(out2[t] - out1[t - 375]) < 0.01f);
}

TEST_CASE("A new note restarts the LFO's cycle") {
  // Two LFOs that have run for different times, retriggered on the same tick, play the same
  // cycle from there (to within the native block the edge lands in: 8 samples at 62.5 kHz).
  auto afterRetrigger = [](int warmUpTicks) {
    TidesEngine e;
    e.prepare(48000.0);
    e.reset();
    const ParamValues p = defaults();
    ModContext ctx;
    ctx.gate = true;
    float out[kMaxModOutputs];
    for (int t = 0; t < warmUpTicks; ++t) e.process(ctx, p, 16, out);
    ctx.retrigger = true;
    e.process(ctx, p, 16, out);
    ctx.retrigger = false;
    std::vector<float> values;
    for (int t = 0; t < 50; ++t) {
      e.process(ctx, p, 16, out);
      values.push_back(out[0]);
    }
    return values;
  };
  const auto a = afterRetrigger(100);
  const auto b = afterRetrigger(777);
  for (size_t t = 0; t < a.size(); ++t) REQUIRE(std::fabs(a[t] - b[t]) < 0.005f);
}

TEST_CASE("A synced LFO locks to the beat") {
  ParamValues p = defaults();
  p.p[1] = modOptionValue(1);  // Sync on; Frequency 50 %: one cycle per beat
  const double rate = 48000.0;
  const double bps = 2.0 / rate;  // 120 BPM
  TidesEngine a, b;
  a.prepare(rate);
  b.prepare(rate);
  a.reset();
  float oa[kMaxModOutputs], ob[kMaxModOutputs];
  int rising = 0;
  float previous = 0.0f;
  for (int t = 0; t < 12000; ++t) {  // 4 s: 8 beats
    ModContext ctx;
    ctx.beat = 3.25 + double(t) * 16.0 * bps;
    ctx.beatsPerSample = bps;
    a.process(ctx, p, 16, oa);
    if (previous < 0.0f && oa[0] >= 0.0f) ++rising;
    previous = oa[0];
    if (t == 6000) b.reset();  // b starts two beats late, mid-cycle
    if (t >= 6000) {
      b.process(ctx, p, 16, ob);
      if (t >= 6010) REQUIRE(std::fabs(oa[0] - ob[0]) < 1e-5f);
    }
  }
  REQUIRE(rising >= 7);
  REQUIRE(rising <= 9);
}

TEST_CASE("LFO survives NaN and does not allocate") {
  TidesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    for (int k = 0; k < 4; ++k) REQUIRE(std::isfinite(out[k]));
  }
  ParamValues p = defaults();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 2000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.beat = double(t) * 0.01;
      ctx.beatsPerSample = 0.01 / 16.0;
      p.p[0] = modOptionValue(t / 700);
      p.p[1] = modOptionValue((t / 300) % 2);
      p.p[9] = modOptionValue((t / 250) % 4);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

`Reference` is written from `tides2/tides.cc`'s `Process()` (TRIG patched, CLOCK unpatched) and drives `tides::PolySlopeGenerator` directly. The output scaling it applies (/5 cycling, /8 otherwise, clamped) is the documented normalisation (provisional ruling 32), restated in the test rather than taken from the adapter. A retriggered LFO is compared with another retriggered LFO, not with a freshly reset one: after `Init()` Tides' frequency interpolator starts at 0.01 cycles per sample (`poly_slope_generator.h` `Init`), which advances a fresh generator's first block by about 1 % of a cycle (found in planning).

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/tides_engine.h` not found).

- [ ] **Step 3: Write `core/engine/tides_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Tides 2's poly slope generator (tides2/poly_slope_generator.h), shown as "LFO". Runs at the
// module's 62.5 kHz in 8-sample blocks, its slope/phase and frequency output modes at half rate
// as the firmware does (tides2/tides.cc:145-171). Four outputs.
//
// P1 Mode (Cycle, AD, AR), P2 Sync (Off, On), P3 Frequency (-48..+48 semitones around the range's
// root, VCV Tides2.cpp:107; with Sync on, a ratio to the beat), P4 Shape, P5 Slope, P6 Shift,
// P7 Smoothness, P8 and P9 unused, P10 Output (Phase, Gates, Amplitude, Frequency), P11 Range
// (Medium, Low). The stepped lists are EG13's own orders (append-only, kModSelectorSteps), so
// each default is the first option and a host reset of 0 selects it.
//
// The note gate is patched into TRIG (a new note restarts a cycle), except with Sync on: then
// the ramp comes from the beat position instead of TRIG and CLOCK, as the firmware's clocked
// mode does with TRIG unpatched (tides.cc:219). EG13's gate is complete at every native
// sample; the module reads TRIG every other sample (tides.cc:165-178), which only matters at
// half rate, where EG13 also reads every other sample.
class TidesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 62500.0;  // tides2/io_buffer.h:46
  static constexpr int kNativeBlock = 8;          // tides2/io_buffer.h:40
  enum Mode { Cycle = 0, Ad = 1, Ar = 2 };
  static constexpr int kNumModes = 3;
  enum Output { Phase = 0, Gates = 1, Amplitude = 2, Frequency = 3 };
  static constexpr int kNumOutputModes = 4;
  enum Range { Medium = 0, Low = 1 };
  static constexpr int kNumRanges = 2;

  TidesEngine();
  ~TidesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();

  static int modeIndex(float normalized);    // P1
  static bool syncOn(float normalized);      // P2
  static float transposition(float normalized);  // P3: -48..+48 semitones
  static int outputIndex(float normalized);  // P10
  static int rangeIndex(float normalized);   // P11
  static float rootHz(int range);            // tides.cc:102-106: 2 Hz Medium, 0.125 Hz Low
  static float frequencyFor(double hz, int range);  // the P3 value that plays hz (free running)

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/tides_engine.cpp`**

```cpp
#include "engine/tides_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "stmlib/dsp/hysteresis_quantizer.h"
#include "stmlib/dsp/units.h"
#include "stmlib/utils/gate_flags.h"
#include "tides2/poly_slope_generator.h"

namespace eg13 {

static_assert(kGateHigh == stmlib::GATE_FLAG_HIGH && kGateRising == stmlib::GATE_FLAG_RISING &&
                  kGateFalling == stmlib::GATE_FLAG_FALLING,
              "EG13's gate flags are stmlib's");

namespace {

// The clocked mode's ratios, tides2/tides.cc:108-129 (a file that cannot be compiled here).
const tides::Ratio kRatios[20] = {
    {0.0625f, 16}, {0.125f, 8}, {0.1666666f, 6}, {0.25f, 4}, {0.3333333f, 3}, {0.5f, 2}, {0.6666666f, 3},
    {0.75f, 4},    {0.8f, 5},   {1.0f, 1},       {1.0f, 1},  {1.25f, 4},      {1.3333333f, 3}, {1.5f, 2},
    {2.0f, 1},     {3.0f, 1},   {4.0f, 1},       {6.0f, 1},  {8.0f, 1},       {16.0f, 1},
};

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[TidesEngine::kNumModes] = {"Cycle", "AD", "AR"};
const tides::RampMode kModes[TidesEngine::kNumModes] = {tides::RAMP_MODE_LOOPING, tides::RAMP_MODE_AD,
                                                       tides::RAMP_MODE_AR};
const char* const kOutputNames[TidesEngine::kNumOutputModes] = {"Phase", "Gates", "Amplitude", "Frequency"};
const tides::OutputMode kOutputs[TidesEngine::kNumOutputModes] = {
    tides::OUTPUT_MODE_SLOPE_PHASE, tides::OUTPUT_MODE_GATES, tides::OUTPUT_MODE_AMPLITUDE,
    tides::OUTPUT_MODE_FREQUENCY};
const char* const kRangeNames[TidesEngine::kNumRanges] = {"Medium", "Low"};

void formatMode(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kModeNames[TidesEngine::modeIndex(v)]); }
void formatSync(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", TidesEngine::syncOn(v) ? "On" : "Off"); }
void formatOutput(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kOutputNames[TidesEngine::outputIndex(v)]);
}
void formatRange(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kRangeNames[TidesEngine::rangeIndex(v)]); }
void formatFrequency(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.1f st", double(TidesEngine::transposition(v)));
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): cycling
// at 2 Hz (Medium, 0 st), the phase outputs a quarter cycle apart (Shift 100 %), neutral shape,
// slope and smoothness.
const EngineParamInfo kParams[11] = {
    {"Mode", 0.0f, formatMode},
    {"Sync", 0.0f, formatSync},
    {"Frequency", 0.5f, formatFrequency},
    {"Shape", 0.5f, formatPercent01},
    {"Slope", 0.5f, formatPercent01},
    {"Shift", 1.0f, formatPercent01},
    {"Smoothness", 0.5f, formatPercent01},
    {"(unused)", 0.5f, nullptr},
    {"(unused)", 0.5f, nullptr},
    {"Output", 0.0f, formatOutput},
    {"Range", 0.0f, formatRange},
};

const ModOutputInfo kOutputInfo[4] = {{"Out 1", true}, {"Out 2", true}, {"Out 3", true}, {"Out 4", true}};

const ModEngineInfo kInfo = {{"tides", "LFO", 11, kParams, false}, 4, kOutputInfo, false};

}  // namespace

struct TidesEngine::Impl {
  tides::PolySlopeGenerator generator;
  stmlib::HysteresisQuantizer2 ratioQuantizer;
  tides::PolySlopeGenerator::OutputSample out[kNativeBlock];
  stmlib::GateFlags noGate[kNativeBlock] = {};
  int previousOutput = -1;
};

TidesEngine::TidesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

TidesEngine::~TidesEngine() = default;

const ModEngineInfo& TidesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& TidesEngine::info() const { return kInfo; }

int TidesEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

bool TidesEngine::syncOn(float normalized) { return modOption(normalized, 2) == 1; }

float TidesEngine::transposition(float normalized) { return (clamp01(normalized) - 0.5f) * 96.0f; }

int TidesEngine::outputIndex(float normalized) { return modOption(normalized, kNumOutputModes); }

int TidesEngine::rangeIndex(float normalized) { return modOption(normalized, kNumRanges); }

float TidesEngine::rootHz(int range) { return range == Low ? 0.125f : 2.0f; }

float TidesEngine::frequencyFor(double hz, int range) {
  const double semitones = 12.0 * std::log2(hz / double(rootHz(range)));
  return clamp01(float(0.5 + semitones / 96.0));
}

void TidesEngine::resetEngine() {
  impl_->generator.Init();
  impl_->ratioQuantizer.Init(20, 0.05f, false);  // tides.cc:265
  impl_->previousOutput = -1;
}

void TidesEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                              bool, float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const tides::RampMode mode = kModes[modeIndex(p.p[0])];
  const bool sync = syncOn(p.p[1]);
  const float transpose = transposition(p.p[2]);
  const tides::OutputMode output = kOutputs[outputIndex(p.p[9])];
  const bool half = output >= tides::OUTPUT_MODE_SLOPE_PHASE;  // tides.cc:145
  const int size = half ? kNativeBlock / 2 : kNativeBlock;
  stmlib::GateFlags flags[kNativeBlock];
  for (int i = 0; i < size; ++i) flags[i] = half ? gate[2 * i] : gate[i];  // tides.cc:165-171

  float ramp[kNativeBlock];
  float frequency;
  if (sync) {
    // The clocked mode's ratio (tides.cc:186-187) applied to the beat position: one cycle per beat
    // at ratio 1, phase-locked to the host's beats.
    const tides::Ratio& r = s.ratioQuantizer.Lookup(kRatios, 0.5f + transpose * 0.0105f);
    for (int i = 0; i < size; ++i) {
      const double b = beat[half ? 2 * i : i] * double(r.ratio);
      ramp[i] = float(b - std::floor(b));
    }
    frequency = float(beatsPerNative * double(r.ratio)) * (half ? 2.0f : 1.0f);
  } else {
    frequency = rootHz(rangeIndex(p.p[10])) / float(kNativeRate) * stmlib::SemitonesToRatio(transpose);
    if (half) frequency *= 2.0f;  // tides.cc:197-200
  }
  if (int(output) != s.previousOutput) {  // tides.cc:204-207
    s.generator.Reset();
    s.previousOutput = int(output);
  }
  s.generator.Render(mode, output, tides::RANGE_CONTROL, frequency, clamp01(p.p[4]), clamp01(p.p[3]),
                     clamp01(p.p[6]), clamp01(p.p[5]), sync ? s.noGate : flags, sync ? ramp : nullptr, s.out,
                     size_t(size));
  // Volts to EG13's -1..1: +-5 V when cycling, 0..8 V for AD and AR (poly_slope_generator.h Fold
  // and Scale); the end-of-attack and end-of-release gates (8 V) clamp to 1.
  const float scale = mode == tides::RAMP_MODE_LOOPING ? 0.2f : 0.125f;
  for (int i = 0; i < kNativeBlock; ++i) {
    const tides::PolySlopeGenerator::OutputSample& o = s.out[half ? i / 2 : i];
    for (int k = 0; k < 4; ++k) block[k][i] = std::clamp(o.channel[k] * scale, -1.0f, 1.0f);
  }
}

}  // namespace eg13
```

Add `engine/tides_engine.cpp` to `core/CMakeLists.txt` after `engine/stages_engine.cpp`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "LFO"`
Expected: all pass. Then the full suite.

- [ ] **Step 6: Document the LFO**

Append to `docs/manual/modulation.md`:

````markdown
### LFO (based on Tides 2)

A slope generator with four outputs.

- **Mode**: *Cycle* (an LFO), *AD* (each note plays one attack-decay slope) or *AR* (rises while the note is held, falls when it is released).
- **Frequency**: ±4 octaves around the **Range**'s centre: *Medium* centres on 2 Hz, *Low* on 0.125 Hz (one cycle every 8 seconds). The module's audio range is not offered: EG13 reads modulation every 16 samples, too slowly for audio-rate modulation.
- **Shape**, **Slope** and **Smoothness** shape the wave as on the module: Slope moves the peak (a ramp down, a triangle at 50 %, a ramp up); Shape morphs the curve; Smoothness below 50 % rounds it off, above 50 % folds it.
- **Output** chooses what the four outputs carry, as the module's output mode button does: *Phase* (default: the same wave on all four, each later than the previous by an amount set by **Shift**; at the default 100 % they are a quarter cycle apart), *Gates* (out 1 the wave scaled by Shift, out 2 the raw wave, outs 3 and 4 end-of-attack and end-of-release gates), *Amplitude* (Shift pans the wave across the outputs) or *Frequency* (Shift picks frequency ratios between the outputs).
- Each new note on the voice restarts the cycle (per-voice scope). A per-part LFO restarts when you start playing after all keys were up.
- **Sync** *On* locks the LFO to the host's tempo: Frequency then picks a ratio from Tides' table (from one cycle every 16 beats to 16 cycles per beat; 50 % is one cycle per beat) and the cycle lines up with the song position. With the transport stopped it keeps running at the last tempo the host gave (120 BPM if none); when the transport starts it jumps into line with the song. With Sync on, notes do not restart the cycle.
- In Cycle mode the outputs swing from −100 % to +100 %; in AD and AR they run from 0 to 100 %.
- Defaults: Cycle at 2 Hz, Phase outputs a quarter cycle apart, Sync off.
````

In `docs/manual/smoothing.md`, insert before "After a reset":

```markdown
- **LFO:** Tides glides Frequency, Shape, Slope, Smoothness and Shift itself across each of its 8-sample blocks (an eighth of a millisecond). Mode, Output, Range and Sync switch at once.
```

- [ ] **Step 7: Commit**

```bash
git add core/engine/tides_engine.h core/engine/tides_engine.cpp core/CMakeLists.txt tests/test_tides_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "LFO: Tides 2's slope generator with tempo sync from the beat position"
```

---
### Task 5: Random / S&H (Marbles) with tempo sync

**Files:**
- Create: `core/engine/marbles_engine.h`, `core/engine/marbles_engine.cpp`, `core/engine/marbles_scales.h` (generated)
- Modify: `core/CMakeLists.txt` (add `engine/marbles_engine.cpp`), `docs/manual/modulation.md`, `docs/manual/smoothing.md`
- Test: `tests/test_marbles_engine.cpp`

**Interfaces:**
- Consumes: `NativeModEngine`, `ModContext`, `modOption`, `modOptionValue` (Task 2).
- Produces: `class MarblesEngine final : public NativeModEngine` ("Random / S&H", id `"marbles"`, 16 P's, outputs T1, T2, T3 (unipolar gates), X1, X2, X3, Y (bipolar), **per part by default**): `static constexpr double kNativeRate = 32000.0; static constexpr int kNativeBlock = 5; static int lengthSteps(float); static int tRange(float); void setSeed(uint32_t) override;` (the seed takes effect at the next `reset()`). `core/engine/marbles_scales.h`: `eg13::marbles_scales::preset_scales[6]`. P1 T Model, P2 T Deja Vu, P3 T Rate, P4 T Bias, P5 Deja Vu, P6 X Range, P7 X Spread, P8 X Bias, P9 X Steps, P10 T Range, P11 T Jitter, P12 Length, P13 X Mode, P14 X Scale, P15 X Deja Vu, P16 Sync; defaults 0, 0, .5, .5, .5, 1, .5, .5, .5, 0, 0, 0, 0, 0, 0, 0.

- [ ] **Step 1: Generate the scale table from the firmware**

Marbles keeps its six scales in `marbles/settings.cc:39-154`, a file that needs the module's flash storage. Copy those lines verbatim:

```bash
{
  echo '#pragma once'
  echo ''
  echo '// GENERATED by docs/superpowers/plans/2026-10-21-m4-modulation.md, Task 5: the six Marbles'
  echo '// scales, copied verbatim from third_party/eurorack/marbles/settings.cc:39-154 (MIT). That file'
  echo "// cannot be compiled here: it needs the module's flash storage (stmlib/system/storage.h)."
  echo ''
  echo '#include "marbles/random/quantizer.h"'
  echo ''
  echo 'namespace eg13::marbles_scales {'
  echo ''
  echo 'using marbles::Scale;'
  echo ''
  sed -n '39,154p' third_party/eurorack/marbles/settings.cc
  echo ''
  echo '}  // namespace eg13::marbles_scales'
} > core/engine/marbles_scales.h
```

Check: `sed -n '13p;$p' core/engine/marbles_scales.h` prints `const Scale preset_scales[6] = {` and `}  // namespace eg13::marbles_scales`, and the line before the closing namespace (after a blank line) is `};`. If not, the upstream file moved: stop and report BLOCKED.

- [ ] **Step 2: Write the failing tests (`tests/test_marbles_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/marbles_engine.h"
#include "engine/marbles_scales.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(MarblesEngine::staticInfo().engine); }

// A setting away from the defaults: clusters, both deja vu loops on, bump, minor, positive range.
ParamValues busy() {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(1);   // T Model: Clusters
  p.p[1] = modOptionValue(1);   // T Deja Vu on
  p.p[2] = 0.7f;                // T Rate: +40 % (24 semitones)
  p.p[3] = 0.6f;                // T Bias
  p.p[4] = 0.7f;                // Deja Vu
  p.p[5] = modOptionValue(1);   // X Range: Positive
  p.p[6] = 0.3f;                // X Spread
  p.p[7] = 0.6f;                // X Bias
  p.p[8] = 0.8f;                // X Steps
  p.p[9] = modOptionValue(2);   // T Range: 4x
  p.p[10] = 0.2f;               // T Jitter
  p.p[11] = 0.4f;               // Length: round(14) -> 4 steps
  p.p[12] = modOptionValue(1);  // X Mode: Bump
  p.p[13] = modOptionValue(1);  // X Scale: Minor
  p.p[14] = modOptionValue(1);  // X Deja Vu on
  return p;
}

// VCV Marbles' stepBlock (AudibleInstruments Marbles.cpp:440-534) with nothing patched, written
// from that file: the same settings every 5-sample block, T on its internal clock, X/Y clocked
// by T.
class Reference {
 public:
  explicit Reference(uint32_t seed) {
    generator_.Init(seed);
    stream_.Init(&generator_);
    t_.Init(&stream_, 32000.0f);
    xy_.Init(&stream_, 32000.0f);
    for (int i = 0; i < 6; ++i) xy_.LoadScale(i, marbles_scales::preset_scales[i]);
  }
  // out[k]: the 7 outputs of the block's last sample, in volts and gates.
  void block(float out[7]) {
    t_.set_model(marbles::T_GENERATOR_MODEL_CLUSTERS);
    t_.set_range(marbles::T_GENERATOR_RANGE_4X);
    t_.set_rate(60.0f * 0.4f);
    t_.set_bias(0.6f);
    t_.set_jitter(0.2f);
    t_.set_deja_vu(0.7f);
    t_.set_length(4);
    t_.set_pulse_width_mean(0.0f);
    t_.set_pulse_width_std(0.0f);
    float master[5], external[5], slave0[5], slave1[5];
    marbles::Ramps ramps;
    ramps.master = master;
    ramps.external = external;
    ramps.slave[0] = slave0;
    ramps.slave[1] = slave1;
    const stmlib::GateFlags clock[5] = {};
    bool gates[10];
    t_.Process(false, clock, ramps, gates, 5);
    marbles::GroupSettings x;
    x.control_mode = marbles::CONTROL_MODE_BUMP;
    x.voltage_range = marbles::VOLTAGE_RANGE_POSITIVE;
    x.register_mode = false;
    x.register_value = 0.0f;
    x.spread = 0.3f;
    x.bias = 0.6f;
    x.steps = 0.8f;
    x.deja_vu = 0.7f;
    x.scale_index = 1;
    x.length = 4;
    x.ratio.p = 1;
    x.ratio.q = 1;
    marbles::GroupSettings y = x;
    y.control_mode = marbles::CONTROL_MODE_IDENTICAL;
    y.deja_vu = 0.0f;
    y.length = 1;
    y.ratio.q = 4;
    float volts[20];
    xy_.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, clock, ramps, volts, 5);
    out[0] = gates[8] ? 1.0f : 0.0f;
    out[1] = master[4] < 0.5f ? 1.0f : 0.0f;
    out[2] = gates[9] ? 1.0f : 0.0f;
    for (int k = 0; k < 4; ++k) out[3 + k] = volts[16 + k];
  }

 private:
  marbles::RandomGenerator generator_;
  marbles::RandomStream stream_;
  marbles::TGenerator t_;
  marbles::XYGenerator xy_;
};

std::vector<float> run(uint32_t seed, int ticks) {
  MarblesEngine e;
  e.setSeed(seed);
  e.prepare(48000.0);
  e.reset();
  const ParamValues p = defaults();
  const ModContext ctx;
  std::vector<float> x1;
  float out[kMaxModOutputs];
  for (int t = 0; t < ticks; ++t) {
    e.process(ctx, p, 16, out);
    x1.push_back(out[3]);
  }
  return x1;
}

}  // namespace

TEST_CASE("Random / S&H describes itself") {
  const ModEngineInfo& info = MarblesEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "marbles");
  REQUIRE(std::string(info.engine.displayName) == "Random / S&H");
  REQUIRE(info.engine.paramCount == 16);
  REQUIRE(info.outputCount == 7);
  REQUIRE(info.perPartByDefault);
  const char* const outputs[7] = {"T1", "T2", "T3", "X1", "X2", "X3", "Y"};
  for (int k = 0; k < 7; ++k) REQUIRE(std::string(info.outputs[k].name) == outputs[k]);
  char buf[32];
  info.engine.params[5].format(1.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Full");
  info.engine.params[9].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1x");
  info.engine.params[13].format(modOptionValue(3), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Pelog");
  info.engine.params[11].format(0.0f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "1 steps");
  REQUIRE(MarblesEngine::lengthSteps(1.0f) == 16);
  REQUIRE(MarblesEngine::lengthSteps(0.4f) == 4);
}

TEST_CASE("Random / S&H matches Marbles' generators sample for sample") {
  MarblesEngine e;
  e.setSeed(7);
  e.prepare(MarblesEngine::kNativeRate);
  e.reset();
  const ParamValues p = busy();
  Reference ref(7);
  const ModContext ctx;
  int changes = 0;
  float previous = 0.0f;
  for (int b = 0; b < 64000; ++b) {  // 10 s
    float out[kMaxModOutputs];
    e.process(ctx, p, 5, out);
    float expected[7];
    ref.block(expected);
    INFO("block " << b);
    for (int k = 0; k < 3; ++k) REQUIRE(out[k] == expected[k]);
    for (int k = 3; k < 7; ++k) REQUIRE(out[k] == std::clamp(expected[k] * 0.2f, -1.0f, 1.0f));
    if (out[3] != previous) ++changes;
    previous = out[3];
  }
  REQUIRE(changes > 20);  // X1 really moves
}

TEST_CASE("Each Random / S&H seed plays its own sequence") {
  REQUIRE(run(3, 6000) == run(3, 6000));
  REQUIRE(run(3, 6000) != run(4, 6000));
}

TEST_CASE("Random / S&H X outputs follow X Range") {
  for (int range = 0; range < 3; ++range) {
    MarblesEngine e;
    e.prepare(48000.0);
    e.reset();
    ParamValues p = defaults();
    p.p[5] = modOptionValue(range);
    p.p[6] = 0.9f;  // wide spread
    const ModContext ctx;
    float out[kMaxModOutputs];
    float lowest = 1.0f, highest = -1.0f;
    for (int t = 0; t < 30000; ++t) {
      e.process(ctx, p, 16, out);
      lowest = std::min(lowest, out[3]);
      highest = std::max(highest, out[3]);
    }
    INFO("range " << range);
    REQUIRE(highest <= 1.0f);
    REQUIRE(highest > 0.5f);
    if (range == 2) {
      REQUIRE(lowest < -0.5f);
    } else {
      REQUIRE(lowest >= 0.0f);
    }
  }
}

TEST_CASE("A synced Random / S&H follows the beat") {
  MarblesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[15] = modOptionValue(1);  // Sync on; T Rate 50 %, 1x: the clock itself
  const double bps = 2.0 / 48000.0;  // 120 BPM
  float out[kMaxModOutputs];
  bool was = false;
  int edges = 0;
  for (int t = 0; t < 24000; ++t) {  // 8 s: 16 beats
    ModContext ctx;
    ctx.beat = double(t) * 16.0 * bps;
    ctx.beatsPerSample = bps;
    ctx.beatJump = t == 0;
    e.process(ctx, p, 16, out);
    const bool t2 = out[1] > 0.5f;
    if (t >= 3000 && t2 && !was) ++edges;  // after the clock has locked (2 beats)
    was = t2;
  }
  REQUIRE(edges >= 13);
  REQUIRE(edges <= 15);
}

TEST_CASE("Random / S&H survives NaN and does not allocate") {
  MarblesEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 2000; ++t) {
    e.process(ctx, bad, 16, out);
    for (int k = 0; k < 7; ++k) REQUIRE(std::isfinite(out[k]));
  }
  ParamValues p = busy();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 3000; ++t) {
      ctx.beat = double(t) * 0.01;
      ctx.beatsPerSample = 0.01 / 16.0;
      ctx.beatJump = t % 1000 == 0;
      p.p[15] = modOptionValue((t / 700) % 2);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

`Reference` is VCV Marbles' `stepBlock` rewritten with the settings of `busy()` as literal values (clusters, 4x, rate 60 × 0.4, …); it drives `marbles::TGenerator` and `marbles::XYGenerator` directly and shares only the scale data with the adapter.

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/marbles_engine.h` not found).

- [ ] **Step 4: Write `core/engine/marbles_engine.h`**

```cpp
#pragma once

#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Marbles' T and X/Y random generators (marbles/random/t_generator.h, x_y_generator.h), shown as
// "Random / S&H". Runs at the module's 32 kHz in its 5-sample blocks with VCV's knob mappings
// (AudibleInstruments Marbles.cpp:440-534). Seven outputs: T1, T2, T3 (gates, 0 or 1), X1, X2, X3,
// Y (voltages scaled by X Range: 0..1 for Narrow and Positive, -1..1 for Full). Per part by
// default (spec §4).
//
// P1 T Model, P2 T Deja Vu, P3 T Rate, P4 T Bias, P5 Deja Vu, P6 X Range, P7 X Spread, P8 X Bias,
// P9 X Steps, P10 T Range, P11 T Jitter, P12 Length, P13 X Mode, P14 X Scale, P15 X Deja Vu,
// P16 Sync. Stepped lists are append-only (kModSelectorSteps). The order puts every control
// whose shared host default is 0 on an option list's first entry, and X Range (host default
// 100 %) on its last entry, Full.
class MarblesEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 32000.0;  // marbles/marbles.cc:63
  static constexpr int kNativeBlock = 5;          // marbles/io_buffer.h:40
  static constexpr int kNumTModels = 3;   // the front panel's: coin toss, clusters, drums
  static constexpr int kNumTRanges = 3;   // EG13 order: 1x, 1/4x, 4x
  static constexpr int kNumXModes = 3;    // identical, bump, tilt
  static constexpr int kNumXRanges = 3;   // narrow (0..2 V), positive (0..5 V), full (+-5 V)
  static constexpr int kNumScales = 6;

  MarblesEngine();
  ~MarblesEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();
  void setSeed(uint32_t seed) override { seed_ = seed; }  // applied at the next reset()

  static int lengthSteps(float normalized);  // P12: VCV's loop_length table (Marbles.cpp:445-455)
  static int tRange(float normalized);       // P10 -> marbles::TGeneratorRange

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  uint32_t seed_ = 1;
};

}  // namespace eg13
```

- [ ] **Step 5: Write `core/engine/marbles_engine.cpp`**

```cpp
#include "engine/marbles_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/marbles_scales.h"
#include "marbles/random/random_generator.h"
#include "marbles/random/random_stream.h"
#include "marbles/random/t_generator.h"
#include "marbles/random/x_y_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace eg13 {

static_assert(kGateHigh == stmlib::GATE_FLAG_HIGH && kGateRising == stmlib::GATE_FLAG_RISING &&
                  kGateFalling == stmlib::GATE_FLAG_FALLING,
              "EG13's gate flags are stmlib's");

namespace {

// VCV's deja-vu loop lengths (AudibleInstruments Marbles.cpp:445-454, GPL-3.0 like EG13).
const int kLoopLength[36] = {1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5,
                             5, 6, 6, 6, 7, 7, 8, 8, 8, 10, 10, 12, 12, 12, 14, 14, 16, 16};

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kTModels[MarblesEngine::kNumTModels] = {"Coin Toss", "Clusters", "Drums"};
const char* const kTRanges[MarblesEngine::kNumTRanges] = {"1x", "1/4x", "4x"};
const marbles::TGeneratorRange kTRangeValues[MarblesEngine::kNumTRanges] = {
    marbles::T_GENERATOR_RANGE_1X, marbles::T_GENERATOR_RANGE_0_25X, marbles::T_GENERATOR_RANGE_4X};
const char* const kXModes[MarblesEngine::kNumXModes] = {"Identical", "Bump", "Tilt"};
const char* const kXRanges[MarblesEngine::kNumXRanges] = {"Narrow", "Positive", "Full"};
const char* const kScales[MarblesEngine::kNumScales] = {"Major", "Minor", "Pentatonic", "Pelog", "Raag Bhairav",
                                                        "Raag Shri"};
const char* const kOffOn[2] = {"Off", "On"};

void formatTModel(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kTModels[modOption(v, 3)]); }
void formatTRange(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kTRanges[modOption(v, 3)]); }
void formatXMode(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kXModes[modOption(v, 3)]); }
void formatXRange(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kXRanges[modOption(v, 3)]); }
void formatScale(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kScales[modOption(v, 6)]); }
void formatOffOn(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kOffOn[modOption(v, 2)]); }
void formatRate(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+d%%", int(std::lround((2.0f * clamp01(v) - 1.0f) * 100.0f)));
}
void formatLength(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%d steps", MarblesEngine::lengthSteps(v));
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): VCV's
// defaults (Marbles.cpp:230-242, 269-279), except X Range Full (bipolar) instead of Positive.
const EngineParamInfo kParams[16] = {
    {"T Model", 0.0f, formatTModel},
    {"T Deja Vu", 0.0f, formatOffOn},
    {"T Rate", 0.5f, formatRate},
    {"T Bias", 0.5f, formatPercent01},
    {"Deja Vu", 0.5f, formatPercent01},
    {"X Range", 1.0f, formatXRange},
    {"X Spread", 0.5f, formatPercent01},
    {"X Bias", 0.5f, formatPercent01},
    {"X Steps", 0.5f, formatPercent01},
    {"T Range", 0.0f, formatTRange},
    {"T Jitter", 0.0f, formatPercent01},
    {"Length", 0.0f, formatLength},
    {"X Mode", 0.0f, formatXMode},
    {"X Scale", 0.0f, formatScale},
    {"X Deja Vu", 0.0f, formatOffOn},
    {"Sync", 0.0f, formatOffOn},
};

const ModOutputInfo kOutputs[7] = {{"T1", false}, {"T2", false}, {"T3", false}, {"X1", true},
                                   {"X2", true},  {"X3", true},  {"Y", true}};

const ModEngineInfo kInfo = {{"marbles", "Random / S&H", 16, kParams, false}, 7, kOutputs, true};

}  // namespace

struct MarblesEngine::Impl {
  marbles::RandomGenerator generator;
  marbles::RandomStream stream;
  marbles::TGenerator t;
  marbles::XYGenerator xy;
  float rampMaster[kNativeBlock];
  float rampExternal[kNativeBlock];
  float rampSlave[2][kNativeBlock];
  bool gates[2 * kNativeBlock];
  float voltages[4 * kNativeBlock];
  stmlib::GateFlags clock = stmlib::GATE_FLAG_LOW;
};

MarblesEngine::MarblesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

MarblesEngine::~MarblesEngine() = default;

const ModEngineInfo& MarblesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& MarblesEngine::info() const { return kInfo; }

int MarblesEngine::lengthSteps(float normalized) {
  return kLoopLength[std::lround(clamp01(normalized) * 35.0f)];
}

int MarblesEngine::tRange(float normalized) { return int(kTRangeValues[modOption(normalized, kNumTRanges)]); }

// As VCV's Marbles does on reset and on a rate change (Marbles.cpp:262-264, 288-298): seed the
// generator, then initialise both generators at the running rate and load the six scales.
void MarblesEngine::resetEngine() {
  Impl& s = *impl_;
  s.generator.Init(seed_);
  s.stream.Init(&s.generator);
  s.t.Init(&s.stream, float(kNativeRate));
  s.xy.Init(&s.stream, float(kNativeRate));
  for (int i = 0; i < kNumScales; ++i) s.xy.LoadScale(i, marbles_scales::preset_scales[i]);
  s.clock = stmlib::GATE_FLAG_LOW;
}

void MarblesEngine::renderBlock(const ParamValues& p, const uint8_t*, const double* beat, double, bool beatJump,
                                float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const bool sync = modOption(p.p[15], 2) == 1;
  const float dejaVu = clamp01(p.p[4]);
  const int length = lengthSteps(p.p[11]);
  const int xRange = modOption(p.p[5], kNumXRanges);

  // T section (Marbles.cpp:458-476).
  s.t.set_model(marbles::TGeneratorModel(modOption(p.p[0], kNumTModels)));
  s.t.set_range(marbles::TGeneratorRange(tRange(p.p[9])));
  s.t.set_rate(60.0f * (2.0f * clamp01(p.p[2]) - 1.0f));
  s.t.set_bias(clamp01(p.p[3]));
  s.t.set_jitter(clamp01(p.p[10]));
  s.t.set_deja_vu(modOption(p.p[1], 2) == 1 ? dejaVu : 0.0f);
  s.t.set_length(length);
  s.t.set_pulse_width_mean(0.0f);
  s.t.set_pulse_width_std(0.0f);
  marbles::Ramps ramps;
  ramps.master = s.rampMaster;
  ramps.external = s.rampExternal;
  ramps.slave[0] = s.rampSlave[0];
  ramps.slave[1] = s.rampSlave[1];
  // Sync: one clock pulse per beat into T's clock input, high for the first half of each beat.
  stmlib::GateFlags clock[kNativeBlock];
  for (int i = 0; i < kNativeBlock; ++i) {
    s.clock = clock[i] = stmlib::ExtractGateFlags(s.clock, beat[i] - std::floor(beat[i]) < 0.5);
  }
  bool tReset = sync && beatJump;  // the transport started or jumped: start the pattern again
  s.t.Process(sync, &tReset, clock, ramps, s.gates, kNativeBlock);

  // X and Y sections (Marbles.cpp:480-534): X clocked by T, Y following X's settings.
  marbles::GroupSettings x;
  x.control_mode = marbles::ControlMode(modOption(p.p[12], kNumXModes));
  x.voltage_range = marbles::VoltageRange(xRange);
  x.register_mode = false;
  x.register_value = 0.0f;
  x.spread = clamp01(p.p[6]);
  x.bias = clamp01(p.p[7]);
  x.steps = clamp01(p.p[8]);
  x.deja_vu = modOption(p.p[14], 2) == 1 ? dejaVu : 0.0f;
  x.scale_index = modOption(p.p[13], kNumScales);
  x.length = length;
  x.ratio.p = 1;
  x.ratio.q = 1;
  marbles::GroupSettings y = x;
  y.control_mode = marbles::CONTROL_MODE_IDENTICAL;
  y.deja_vu = 0.0f;
  y.length = 1;
  y.ratio.p = 1;  // VCV's default Y divider (y_divider_index 8, Marbles.cpp:278, 517-531)
  y.ratio.q = 4;
  bool xyReset = sync && beatJump;
  s.xy.Process(marbles::CLOCK_SOURCE_INTERNAL_T1_T2_T3, x, y, &xyReset, clock, ramps, s.voltages, kNativeBlock);

  // Volts to EG13's range by X Range (x_y_generator.cc:134-145): 0..2 V, 0..5 V, -5..+5 V.
  const float scale = xRange == marbles::VOLTAGE_RANGE_NARROW ? 0.5f : 0.2f;
  for (int i = 0; i < kNativeBlock; ++i) {
    block[0][i] = s.gates[2 * i] ? 1.0f : 0.0f;
    block[1][i] = s.rampMaster[i] < 0.5f ? 1.0f : 0.0f;  // T2 is the master clock (Marbles.cpp:420)
    block[2][i] = s.gates[2 * i + 1] ? 1.0f : 0.0f;
    for (int k = 0; k < 4; ++k) block[3 + k][i] = std::clamp(s.voltages[4 * i + k] * scale, -1.0f, 1.0f);
  }
}

}  // namespace eg13
```

Add `engine/marbles_engine.cpp` to `core/CMakeLists.txt` after `engine/tides_engine.cpp`.

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Random / S&H"`
Expected: all pass. Then the full suite.

- [ ] **Step 7: Document Random / S&H**

Append to `docs/manual/modulation.md`:

````markdown
### Random / S&H (based on Marbles)

Random gates and random voltages, with the module's controls (the knob mappings follow VCV's version of the module). Seven outputs: **T1**, **T2**, **T3** (gates: 0 or 100 %) and **X1**, **X2**, **X3**, **Y** (random values).

- The **T** section makes gates: **T Rate** sets the clock (T2 is the clock itself), **T Model** (*Coin Toss*, *Clusters*, *Drums*) how T1 and T3 share the beats, **T Bias** which of them wins more often, **T Jitter** how loose the timing is, **T Range** (*1x*, *1/4x*, *4x*) the clock's range.
- The **X** section makes values on each T1/T3 gate: **X Spread** the width of the distribution, **X Bias** where it leans, **X Steps** from smooth (low) through random values to notes of **X Scale** (high: *Major*, *Minor*, *Pentatonic*, *Pelog*, *Raag Bhairav*, *Raag Shri*), **X Mode** (*Identical*, *Bump*, *Tilt*) how the three X outputs differ. **Y** is a slower value (one for every four X values) with the same spread, bias and steps.
- **X Range**: *Narrow* and *Positive* give 0 to 100 % (Narrow is the module's 0–2 V, Positive its 0–5 V, both scaled to full range), *Full* gives −100 % to +100 % (default).
- **Deja Vu** loops the random sequences instead of always drawing new ones; **T Deja Vu** and **X Deja Vu** switch it on for each section and **Length** sets the loop's length (1 to 16 steps).
- **Sync** *On* clocks the T section from the host's tempo, one beat per clock, and T Rate then multiplies or divides the beat. It restarts its pattern when the transport starts or jumps; with the transport stopped it keeps the last tempo (120 BPM if none). It takes about two beats to lock.
- A new slot with Random / S&H is per part, so every voice hears the same random values; set Scope to per voice for a different sequence on each voice. Every voice and slot has its own random sequence.
- Defaults: VCV's, except X Range *Full*.
````

In `docs/manual/smoothing.md`, insert before "After a reset":

```markdown
- **Random / S&H:** no smoothing, as on the module: controls are read every 5 samples of its own 32 kHz. X Steps, below its middle, smooths the random values themselves.
```

- [ ] **Step 8: Commit**

```bash
git add core/engine/marbles_engine.h core/engine/marbles_engine.cpp core/engine/marbles_scales.h core/CMakeLists.txt tests/test_marbles_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Random / S&H: Marbles' T and X/Y generators with VCV's mappings and tempo sync"
```

---
### Task 6: Function Generator (Peaks)

**Files:**
- Create: `core/engine/peaks_engine.h`, `core/engine/peaks_engine.cpp`
- Modify: `core/CMakeLists.txt` (add `engine/peaks_engine.cpp`), `docs/manual/modulation.md`, `docs/manual/smoothing.md`
- Test: `tests/test_peaks_engine.cpp`

**Interfaces:**
- Consumes: `NativeModEngine`, `modOption`, `modOptionValue` (Task 2).
- Produces: `class PeaksEngine final : public NativeModEngine` ("Function Generator", id `"peaks"`, 11 P's, 1 output "Out", per voice by default): `static constexpr double kNativeRate = 48000.0; static constexpr int kNativeBlock = 4; enum Mode { Envelope, Lfo, TapLfo, Random }; static constexpr int kNumWaveforms = 5; static constexpr uint16_t kLfoLevel = 40960; static int modeIndex(float); static int waveformIndex(float); static uint16_t knob(float); static uint16_t waveformValue(int); static void knobs(int mode, const ParamValues& p, uint16_t out[4]);`. P1 Mode, P2 Attack, P3 Rate, P4 Decay, P5 Wave Shape, P6 Sustain, P7 Phase, P8 Release, P9 Smoothness, P10 "(unused)", P11 Waveform; defaults 0, 0, .5, .5, .5, 1, .5, .5, .5, 0, 0.

Peaks' `Lfo::Init()` does not reset the LFO's phase (`peaks/modulations/lfo.cc:45-55`), and its constructors leave members uninitialised; the firmware's processors are zeroed globals. The adapter zeroes both processors before `Init()`, as M3's adapters do for upstream objects with uninitialised members (found in planning: without it a reset LFO resumed its old phase).

- [ ] **Step 1: Write the failing tests (`tests/test_peaks_engine.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/peaks_engine.h"
#include "peaks/gate_processor.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "stmlib/utils/random.h"
#include "test_helpers.h"

using namespace eg13;

namespace {

ParamValues defaults() { return test::defaultParams(PeaksEngine::staticInfo().engine); }

bool gateAt(int block) { return (block / 2000) % 2 == 1; }  // 4-sample blocks: 1/6 s on, 1/6 s off

// The adapter at 48 kHz in ticks of n samples (n a multiple of 4); out[t] after tick t.
std::vector<float> render(const ParamValues& p, int n, int ticks) {
  PeaksEngine e;
  e.prepare(PeaksEngine::kNativeRate);
  e.reset();
  std::vector<float> out;
  for (int t = 0; t < ticks; ++t) {
    ModContext ctx;
    ctx.gate = gateAt(t * n / 4);
    float o[kMaxModOutputs];
    e.process(ctx, p, n, o);
    out.push_back(o[0]);
  }
  return out;
}

// peaks/peaks.cc Process for one channel in full control mode, written from the firmware: the
// pots reach the processor through Configure(), then 4-sample blocks of Process(). Returns the
// last sample of every block.
template <typename Processor>
std::vector<int16_t> reference(Processor& processor, const uint16_t knobs[4], int blocks) {
  std::vector<int16_t> out;
  peaks::GateFlags previous = peaks::GATE_FLAG_LOW;
  uint16_t k[4];
  for (int b = 0; b < blocks; ++b) {
    std::copy(knobs, knobs + 4, k);
    processor.Configure(k, peaks::CONTROL_MODE_FULL);
    peaks::GateFlags flags[4];
    for (auto& f : flags) previous = f = peaks::ExtractGateFlags(previous, gateAt(b));
    int16_t o[4];
    processor.Process(flags, o, 4);
    out.push_back(o[3]);
  }
  return out;
}

}  // namespace

TEST_CASE("Function Generator describes itself") {
  const ModEngineInfo& info = PeaksEngine::staticInfo();
  REQUIRE(std::string(info.engine.id) == "peaks");
  REQUIRE(std::string(info.engine.displayName) == "Function Generator");
  REQUIRE(info.engine.paramCount == 11);
  REQUIRE(info.outputCount == 1);
  REQUIRE_FALSE(info.perPartByDefault);
  const char* const names[11] = {"Mode",    "Attack", "Rate",    "Decay",      "Wave Shape", "Sustain",
                                 "Phase",   "Release", "Smoothness", "(unused)", "Waveform"};
  for (int i = 0; i < 11; ++i) REQUIRE(std::string(info.engine.params[i].name) == names[i]);
  char buf[32];
  info.engine.params[0].format(modOptionValue(2), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Tap LFO");
  info.engine.params[10].format(modOptionValue(4), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Noise");
  info.engine.params[2].format(0.5f, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "2.24 Hz");
  for (int w = 0; w < PeaksEngine::kNumWaveforms; ++w) {
    REQUIRE((uint32_t(PeaksEngine::waveformValue(w)) * peaks::LFO_SHAPE_LAST >> 16) == uint32_t(w));
  }
  REQUIRE(PeaksEngine::knob(1.0f) == 65535);
}

TEST_CASE("Function Generator's Envelope matches Peaks sample for sample") {
  ParamValues p = defaults();
  p.p[1] = 0.3f;
  p.p[3] = 0.45f;
  p.p[5] = 0.6f;
  p.p[7] = 0.4f;
  uint16_t k[4];
  PeaksEngine::knobs(PeaksEngine::Envelope, p, k);
  static peaks::MultistageEnvelope env;  // the firmware's processors are zeroed globals
  std::memset(static_cast<void*>(&env), 0, sizeof(env));
  env.Init();
  const auto ref = reference(env, k, 24000);  // 2 s
  for (int n : {4, 16}) {
    const auto out = render(p, n, 24000 * 4 / n);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("tick " << t << " of " << n);
      REQUIRE(out[t] == float(ref[(t + 1) * size_t(n) / 4 - 1]) / 32767.0f);
    }
  }
}

TEST_CASE("Function Generator's LFO matches Peaks sample for sample") {
  for (int waveform = 0; waveform < 4; ++waveform) {
    ParamValues p = defaults();
    p.p[0] = modOptionValue(PeaksEngine::Lfo);
    p.p[2] = 0.6f;
    p.p[4] = 0.3f;
    p.p[6] = 0.7f;
    p.p[10] = modOptionValue(waveform);
    uint16_t k[4];
    PeaksEngine::knobs(PeaksEngine::Lfo, p, k);
    static peaks::Lfo lfo;
    std::memset(static_cast<void*>(&lfo), 0, sizeof(lfo));
    lfo.Init();
    const auto ref = reference(lfo, k, 24000);
    const auto out = render(p, 4, 24000);
    for (size_t t = 0; t < out.size(); ++t) {
      INFO("waveform " << waveform << " block " << t);
      REQUIRE(out[t] == std::clamp(float(ref[t]) / 20480.0f, -1.0f, 1.0f));
    }
  }
}

TEST_CASE("Function Generator's Random matches Peaks' noise LFO") {
  ParamValues p = defaults();
  p.p[0] = modOptionValue(PeaksEngine::Random);
  p.p[2] = 0.7f;
  p.p[8] = 0.8f;
  stmlib::Random::Seed(42);
  const auto out = render(p, 4, 24000);
  uint16_t k[4];
  PeaksEngine::knobs(PeaksEngine::Random, p, k);
  static peaks::Lfo lfo;
  std::memset(static_cast<void*>(&lfo), 0, sizeof(lfo));
  lfo.Init();
  stmlib::Random::Seed(42);
  const auto ref = reference(lfo, k, 24000);
  for (size_t t = 0; t < out.size(); ++t) REQUIRE(out[t] == std::clamp(float(ref[t]) / 20480.0f, -1.0f, 1.0f));
  REQUIRE(*std::max_element(out.begin(), out.end()) - *std::min_element(out.begin(), out.end()) > 0.5f);
}

TEST_CASE("Function Generator's Tap LFO follows the notes' tempo") {
  PeaksEngine e;
  e.prepare(48000.0);
  e.reset();
  ParamValues p = defaults();
  p.p[0] = modOptionValue(PeaksEngine::TapLfo);
  p.p[10] = modOptionValue(1);  // triangle
  float out[kMaxModOutputs];
  float previous = 0.0f;
  int rising = 0;
  for (int t = 0; t < 24000; ++t) {  // 8 s with a tap every half second (120 BPM)
    ModContext ctx;
    ctx.retrigger = t % 1500 == 0;
    ctx.gate = t % 1500 < 100;
    e.process(ctx, p, 16, out);
    if (t >= 6000 && previous < 0.0f && out[0] >= 0.0f) ++rising;
    previous = out[0];
  }
  REQUIRE(rising >= 11);  // 6 s at 2 Hz after the taps have been learnt
  REQUIRE(rising <= 13);
}

TEST_CASE("Function Generator's Envelope sustains fully by default and releases") {
  PeaksEngine e;
  e.prepare(44100.0);
  e.reset();
  const ParamValues p = defaults();
  ModContext ctx;
  ctx.gate = true;
  float out[kMaxModOutputs];
  for (int t = 0; t < 4000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] > 0.999f);
  ctx.gate = false;
  for (int t = 0; t < 8000; ++t) e.process(ctx, p, 16, out);
  REQUIRE(out[0] < 0.001f);
}

TEST_CASE("Function Generator survives NaN and does not allocate") {
  PeaksEngine e;
  e.prepare(96000.0);
  e.reset();
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  ModContext ctx;
  float out[kMaxModOutputs];
  for (int t = 0; t < 500; ++t) {
    e.process(ctx, bad, 16, out);
    REQUIRE(std::isfinite(out[0]));
  }
  ParamValues p = defaults();
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 4000; ++t) {
      ctx.gate = (t / 100) % 2 == 0;
      ctx.retrigger = t % 100 == 0;
      p.p[0] = modOptionValue((t / 500) % 4);
      e.process(ctx, p, 1 + t % 16, out);
    }
    e.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}

TEST_CASE("A reset Function Generator starts from the same state every time") {
  PeaksEngine e;
  e.prepare(48000.0);
  ParamValues p = defaults();
  p.p[0] = modOptionValue(PeaksEngine::Lfo);
  auto run = [&]() {
    e.reset();
    std::vector<float> values;
    float out[kMaxModOutputs];
    for (int t = 0; t < 1000; ++t) {
      e.process(ModContext{}, p, 16, out);
      values.push_back(out[0]);
    }
    return values;
  };
  const auto first = run();
  REQUIRE(run() == first);
}
```

`reference()` is `peaks/peaks.cc`'s `Process()` for one channel (pots through `Configure()`, then 4-sample `Process()` blocks), driving `peaks::MultistageEnvelope` and `peaks::Lfo` directly. It gets its pot values from `PeaksEngine::knobs()`: that function is the documented P-to-pot mapping, checked separately in "describes itself" (waveform values decode to their waveform). The noise LFO uses stmlib's shared random generator, so the test reseeds it before each run.

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`engine/peaks_engine.h` not found).

- [ ] **Step 3: Write `core/engine/peaks_engine.h`**

```cpp
#pragma once

#include <cstdint>
#include <memory>

#include "engine/mod_engine.h"

namespace eg13 {

// Peaks' envelope and LFO processors (peaks/modulations/multistage_envelope.h, lfo.h), shown as
// "Function Generator". One channel of the module in its full (expert) control mode, at its
// 48 kHz in 4-sample blocks. One output: 0..1 for the envelope, -1..1 for the LFOs.
//
// P1 Mode (Envelope, LFO, Tap LFO, Random), P2 Attack, P3 Rate, P4 Decay, P5 Wave Shape,
// P6 Sustain, P7 Phase, P8 Release, P9 Smoothness, P10 unused, P11 Waveform (Sine, Triangle,
// Square, Steps, Noise). Each mode keeps its own controls, at the same positions as the
// Envelope engine's ADSR (Attack P2, Decay P4, Sustain P6, Release P8), so one set of routes
// suits both. Stepped lists are append-only (kModSelectorSteps).
class PeaksEngine final : public NativeModEngine {
 public:
  static constexpr double kNativeRate = 48000.0;  // peaks/peaks.cc:79
  static constexpr int kNativeBlock = 4;          // peaks/io_buffer.h:41
  enum Mode { Envelope = 0, Lfo = 1, TapLfo = 2, Random = 3 };
  static constexpr int kNumModes = 4;
  static constexpr int kNumWaveforms = 5;  // peaks::LfoShape order
  static constexpr uint16_t kLfoLevel = 40960;  // the free LFO's level (lfo.h Configure)

  PeaksEngine();
  ~PeaksEngine() override;

  const ModEngineInfo& info() const override;
  static const ModEngineInfo& staticInfo();

  static int modeIndex(float normalized);      // P1
  static int waveformIndex(float normalized);  // P11
  static uint16_t knob(float normalized);      // 0..1 -> the module's 0..65535 pot value
  static uint16_t waveformValue(int waveform); // the pot value set_shape_integer reads as it
  // The four pot values Configure() gets in this mode (processors.h CopyParameters).
  static void knobs(int mode, const ParamValues& p, uint16_t out[4]);

 private:
  void resetEngine() override;
  void renderBlock(const ParamValues& p, const uint8_t* gate, const double* beat, double beatsPerNative,
                   bool beatJump, float (*block)[kMaxNativeBlock]) override;

  struct Impl;
  std::unique_ptr<Impl> impl_;
  int mode_ = -1;
};

}  // namespace eg13
```

- [ ] **Step 4: Write `core/engine/peaks_engine.cpp`**

```cpp
#include "engine/peaks_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "peaks/gate_processor.h"
#include "peaks/modulations/lfo.h"
#include "peaks/modulations/multistage_envelope.h"
#include "peaks/resources.h"

namespace eg13 {

static_assert(kGateHigh == peaks::GATE_FLAG_HIGH && kGateRising == peaks::GATE_FLAG_RISING &&
                  kGateFalling == peaks::GATE_FLAG_FALLING,
              "EG13's gate flags are Peaks'");

namespace {

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[PeaksEngine::kNumModes] = {"Envelope", "LFO", "Tap LFO", "Random"};
const char* const kWaveformNames[PeaksEngine::kNumWaveforms] = {"Sine", "Triangle", "Square", "Steps", "Noise"};

void formatMode(float v, char* buf, size_t size) { std::snprintf(buf, size, "%s", kModeNames[PeaksEngine::modeIndex(v)]); }
void formatWaveform(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kWaveformNames[PeaksEngine::waveformIndex(v)]);
}
void formatEnvelopeTime(float v, char* buf, size_t size) {
  // MultistageEnvelope::Process: lut_env_increments[time >> 8] per sample over a 2^32 phase.
  const double seconds = 4294967296.0 / double(peaks::lut_env_increments[PeaksEngine::knob(v) >> 8]) / 48000.0;
  if (seconds < 1.0) {
    std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0)));
  } else {
    std::snprintf(buf, size, "%.2f s", seconds);
  }
}
void formatRate(float v, char* buf, size_t size) {
  // Lfo::Process: lut_lfo_increments[rate >> 8] per sample over a 2^32 phase.
  const double hz = double(peaks::lut_lfo_increments[PeaksEngine::knob(v) >> 8]) * 48000.0 / 4294967296.0;
  std::snprintf(buf, size, hz < 10.0 ? "%.2f Hz" : "%.1f Hz", hz);
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): an ADSR
// with the fastest attack, about 0.4 s decay and release and full sustain; the LFOs at about
// 2.2 Hz, sine, neutral shape and phase.
const EngineParamInfo kParams[11] = {
    {"Mode", 0.0f, formatMode},
    {"Attack", 0.0f, formatEnvelopeTime},
    {"Rate", 0.5f, formatRate},
    {"Decay", 0.5f, formatEnvelopeTime},
    {"Wave Shape", 0.5f, formatPercent01},
    {"Sustain", 1.0f, formatPercent01},
    {"Phase", 0.5f, formatPercent01},
    {"Release", 0.5f, formatEnvelopeTime},
    {"Smoothness", 0.5f, formatPercent01},
    {"(unused)", 0.0f, nullptr},
    {"Waveform", 0.0f, formatWaveform},
};

const ModOutputInfo kOutputs[1] = {{"Out", true}};

const ModEngineInfo kInfo = {{"peaks", "Function Generator", 11, kParams, false}, 1, kOutputs, false};

}  // namespace

struct PeaksEngine::Impl {
  peaks::MultistageEnvelope envelope;
  peaks::Lfo lfo;
  int16_t out[kNativeBlock];
};

PeaksEngine::PeaksEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  resetEngine();
}

PeaksEngine::~PeaksEngine() = default;

const ModEngineInfo& PeaksEngine::staticInfo() { return kInfo; }

const ModEngineInfo& PeaksEngine::info() const { return kInfo; }

int PeaksEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

int PeaksEngine::waveformIndex(float normalized) { return modOption(normalized, kNumWaveforms); }

uint16_t PeaksEngine::knob(float normalized) { return uint16_t(std::lround(clamp01(normalized) * 65535.0f)); }

uint16_t PeaksEngine::waveformValue(int waveform) {
  // Lfo::set_shape_integer: shape = value * LFO_SHAPE_LAST >> 16. The centre of each fifth.
  return uint16_t((uint32_t(waveform) * 65536u + 32768u) / uint32_t(kNumWaveforms));
}

void PeaksEngine::knobs(int mode, const ParamValues& p, uint16_t out[4]) {
  switch (mode) {
    case Envelope:  // set_adsr(attack, decay, sustain >> 1, release)
      out[0] = knob(p.p[1]);
      out[1] = knob(p.p[3]);
      out[2] = knob(p.p[5]);
      out[3] = knob(p.p[7]);
      return;
    case TapLfo:  // synced: level, shape, parameter, phase
      out[0] = kLfoLevel;
      out[1] = waveformValue(waveformIndex(p.p[10]));
      out[2] = knob(p.p[4]);
      out[3] = knob(p.p[6]);
      return;
    case Random:  // the noise shape: rate, shape, smoothness, phase 0
      out[0] = knob(p.p[2]);
      out[1] = waveformValue(peaks::LFO_SHAPE_NOISE);
      out[2] = knob(p.p[8]);
      out[3] = 32768;
      return;
    default:  // free LFO: rate, shape, parameter, phase
      out[0] = knob(p.p[2]);
      out[1] = waveformValue(waveformIndex(p.p[10]));
      out[2] = knob(p.p[4]);
      out[3] = knob(p.p[6]);
      return;
  }
}

// As Processors::Init leaves a channel (peaks/processors.cc:60-82): both processors initialised;
// the first block selects the mode as set_function does. Lfo::Init() leaves the phase (and other
// members) as they were: the firmware's processors are globals, zeroed at power-on, so the
// adapter zeroes them first and a reset starts from the same state every time.
void PeaksEngine::resetEngine() {
  std::memset(static_cast<void*>(&impl_->envelope), 0, sizeof(impl_->envelope));
  std::memset(static_cast<void*>(&impl_->lfo), 0, sizeof(impl_->lfo));
  impl_->envelope.Init();
  impl_->lfo.Init();
  mode_ = -1;
}

void PeaksEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double*, double, bool,
                              float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const int mode = modeIndex(p.p[0]);
  if (mode != mode_) {  // Processors::set_function (peaks/processors.h:111-119)
    s.lfo.set_sync(mode == TapLfo);
    if (mode == Envelope) {
      s.envelope.Init();
    } else if (mode != TapLfo) {
      s.lfo.Init();
    }
    mode_ = mode;
  }
  uint16_t k[4];
  knobs(mode, p, k);
  if (mode == Envelope) {
    s.envelope.Configure(k, peaks::CONTROL_MODE_FULL);
    s.envelope.Process(gate, s.out, kNativeBlock);
    for (int i = 0; i < kNativeBlock; ++i) block[0][i] = float(s.out[i]) / 32767.0f;
  } else {
    s.lfo.Configure(k, peaks::CONTROL_MODE_FULL);
    s.lfo.Process(gate, s.out, kNativeBlock);
    // The LFO's level 40960 swings +-20480 (lfo.h set_level, lfo.cc Process): the module's +-5 V.
    for (int i = 0; i < kNativeBlock; ++i) block[0][i] = std::clamp(float(s.out[i]) / 20480.0f, -1.0f, 1.0f);
  }
}

}  // namespace eg13
```

Add `engine/peaks_engine.cpp` to `core/CMakeLists.txt` after `engine/marbles_engine.cpp`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Function Generator"`
Expected: all pass. Then the full suite.

- [ ] **Step 6: Document the Function Generator**

Append to `docs/manual/modulation.md`:

````markdown
### Function Generator (based on Peaks)

One channel of the module in its full-control mode. One output.

- **Mode**: *Envelope* (an ADSR: **Attack**, **Decay**, **Sustain**, **Release**, from 0 to 100 %), *LFO* (**Rate** from 0.03 Hz to 155 Hz; **Waveform** *Sine*, *Triangle*, *Square*, *Steps* or *Noise*; **Wave Shape** folds the sine, skews the triangle, sets the square's width or the number of steps; **Phase** is where a new note restarts the cycle), *Tap LFO* (the notes you play are the taps: the LFO follows their tempo, with Waveform, Wave Shape and Phase as in LFO mode) or *Random* (a random wave at **Rate**; **Smoothness** goes from stepped, through straight lines, to smooth curves).
- Each mode keeps its own controls, so switching modes and back restores them. Attack, Decay, Sustain and Release sit at the same positions as the Envelope engine's, so routes into them suit both.
- A new note restarts the envelope, and the LFO at its Phase (per-voice scope).
- The envelope runs from 0 to 100 %; the LFOs swing from −100 % to +100 % (the module's ±5 V).
- Defaults: Envelope with the fastest attack, about 0.4 s decay and release, full sustain; LFO at about 2.2 Hz, sine.
````

In `docs/manual/smoothing.md`, insert before "After a reset":

```markdown
- **Function Generator:** no smoothing, as on the module: controls are read every 4 samples of its own 48 kHz. Mode and Waveform switch at once.
```

- [ ] **Step 7: Commit**

```bash
git add core/engine/peaks_engine.h core/engine/peaks_engine.cpp core/CMakeLists.txt tests/test_peaks_engine.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Function Generator: Peaks' envelope, LFO, tap LFO and random"
```

---
### Task 7: Mod engine registry, factory and ModSlot

**Files:**
- Create: `core/mod/mod_factory.h`, `core/mod/mod_factory.cpp`, `core/mod/mod_slot.h`, `core/mod/mod_slot.cpp`
- Modify: `core/engine/engine_registry.h`, `core/engine/engine_registry.cpp`, `core/CMakeLists.txt`, `tests/test_mod_engines.h`
- Test: `tests/test_mod_slot.cpp`

**Interfaces:**
- Consumes: `ModEngine`, `OffModEngine`, `EnvelopeShape`, `ModContext` (Task 2); `StagesEngine`, `TidesEngine`, `MarblesEngine`, `PeaksEngine` (Tasks 3–6).
- Produces:
  - `core/engine/engine_registry.h`: `enum class ModEngineKind : int { Off = 0, Envelope = 1, Lfo = 2, Random = 3, FunctionGenerator = 4 }; constexpr int kNumModEngines = 5; constexpr int kModEngineSelectorMax = 31; int modEngineFromSelector(double value); const ModEngineInfo& modEngineInfo(int engine);` (unknown → Off).
  - `core/mod/mod_factory.h`: `using ModEngineFactory = std::vector<std::unique_ptr<ModEngine>> (*)(); std::unique_ptr<ModEngine> makeModEngine(int engine); std::vector<std::unique_ptr<ModEngine>> makeAllModEngines();`
  - `core/mod/mod_slot.h`: `class ModSlot { explicit ModSlot(std::vector<std::unique_ptr<ModEngine>>); void prepare(double hostRate); void setSeed(uint32_t); void reset(); void restart(); void setEngine(int); void process(const ModContext&, const ParamValues&, const EnvelopeShape&, int n); int engine() const; int engineCount() const; const ModEngineInfo& info() const; const float* outputs() const; size_t nanResets() const; };`
  - `tests/test_mod_engines.h`: `eg13::test::NanModEngine` (NaN on output 0 at its first tick ever, then 0.25; public `ticks`, `resets`).

- [ ] **Step 1: Add the NaN test engine to `tests/test_mod_engines.h`**

Add `#include <limits>` after `#include <cstdint>`, and before the closing `}  // namespace eg13::test`:

```cpp
// Outputs NaN on output 0 for the first tick after construction (a one-off glitch that survives
// reset()), then 0.25. Counts its resets.
class NanModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override { ++resets; }
  void process(const ModContext&, const ParamValues&, int, float* out) override {
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = 0.0f;
    out[0] = ticks++ == 0 ? std::numeric_limits<float>::quiet_NaN() : 0.25f;
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[1] = {{"Out", false}};
    static const ModEngineInfo kInfo = {{"nan", "NaN", 0, nullptr, false}, 1, kOutputs, false};
    return kInfo;
  }
  int ticks = 0;
  int resets = 0;
};
```

- [ ] **Step 2: Write the failing tests (`tests/test_mod_slot.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "engine/engine_registry.h"
#include "mod/mod_factory.h"
#include "mod/mod_slot.h"
#include "test_helpers.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

std::vector<float> lfoRun(ModSlot& slot, int ticks) {
  const ParamValues p = test::defaultParams(modEngineInfo(int(ModEngineKind::Lfo)).engine);
  std::vector<float> values;
  for (int t = 0; t < ticks; ++t) {
    slot.process(ModContext{}, p, EnvelopeShape{}, 16);
    values.push_back(slot.outputs()[0]);
  }
  return values;
}

}  // namespace

TEST_CASE("The mod engine registry names every engine and falls back to Off") {
  const char* const ids[kNumModEngines] = {"off", "stages", "tides", "marbles", "peaks"};
  const char* const names[kNumModEngines] = {"Off", "Envelope", "LFO", "Random / S&H", "Function Generator"};
  for (int e = 0; e < kNumModEngines; ++e) {
    REQUIRE(std::string(modEngineInfo(e).engine.id) == ids[e]);
    REQUIRE(std::string(modEngineInfo(e).engine.displayName) == names[e]);
    REQUIRE(modEngineInfo(e).engine.paramCount <= kModParamCount);
    REQUIRE(modEngineInfo(e).outputCount <= kMaxModOutputs);
    REQUIRE(std::string(makeModEngine(e)->info().engine.id) == ids[e]);
  }
  REQUIRE(modEngineFromSelector(3.0) == 3);
  REQUIRE(modEngineFromSelector(9.0) == 0);
  REQUIRE(modEngineFromSelector(std::nan("")) == 0);
  REQUIRE(std::string(modEngineInfo(17).engine.id) == "off");
  REQUIRE(makeAllModEngines().size() == size_t(kNumModEngines));
  REQUIRE(modEngineInfo(int(ModEngineKind::Random)).perPartByDefault);
  REQUIRE_FALSE(modEngineInfo(int(ModEngineKind::Lfo)).perPartByDefault);
}

TEST_CASE("A mod slot starts Off and outputs zeros") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  REQUIRE(slot.engine() == 0);
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  for (int k = 0; k < kMaxModOutputs; ++k) REQUIRE(slot.outputs()[k] == 0.0f);
}

TEST_CASE("Changing a slot's engine starts the new engine from reset") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  slot.setEngine(int(ModEngineKind::Lfo));
  const auto fresh = lfoRun(slot, 200);
  slot.setEngine(int(ModEngineKind::Envelope));
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  slot.setEngine(int(ModEngineKind::Lfo));
  REQUIRE(lfoRun(slot, 200) == fresh);
  slot.setEngine(99);
  REQUIRE(slot.engine() == kNumModEngines - 1);
}

TEST_CASE("A slot whose engine outputs NaN resets it and reads zero") {
  std::vector<std::unique_ptr<ModEngine>> engines;
  engines.push_back(std::make_unique<OffModEngine>());
  engines.push_back(std::make_unique<test::NanModEngine>());
  auto* nan = static_cast<test::NanModEngine*>(engines[1].get());
  ModSlot slot(std::move(engines));
  slot.prepare(48000.0);
  slot.setEngine(1);
  const int resetsBefore = nan->resets;
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  REQUIRE(slot.outputs()[0] == 0.0f);
  REQUIRE(slot.nanResets() == 1);
  REQUIRE(nan->resets == resetsBefore + 1);
  slot.process(ModContext{}, ParamValues{}, EnvelopeShape{}, 16);
  REQUIRE(slot.outputs()[0] == 0.25f);
}

TEST_CASE("The slot passes the envelope shape to its engine") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(48000.0);
  slot.setEngine(int(ModEngineKind::Envelope));
  ParamValues p = test::defaultParams(modEngineInfo(int(ModEngineKind::Envelope)).engine);
  p.p[0] = modOptionValue(3);  // Custom
  // One looping Step segment with the gate patched: the module's sample and hold, which takes
  // its level when the gate rises (segment_generator.cc process_fn_table_).
  EnvelopeShape sampleAndHold;
  sampleAndHold.count = 1;
  sampleAndHold.type[0] = EnvelopeShape::Step;
  sampleAndHold.loop[0] = 1;
  p.p[1] = 0.4f;  // the level to sample
  p.p[2] = 0.0f;  // no glide
  ModContext ctx;
  ctx.gate = true;
  for (int t = 0; t < 100; ++t) slot.process(ctx, p, sampleAndHold, 16);
  REQUIRE(std::fabs(slot.outputs()[0] - 0.4f) < 1e-3f);
}

TEST_CASE("Mod slots switch and process without allocating") {
  ModSlot slot(makeAllModEngines());
  slot.prepare(44100.0);
  ParamValues p;
  for (float& v : p.p) v = 0.3f;
  ModContext ctx;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 5000; ++t) {
      if (t % 250 == 0) slot.setEngine((t / 250) % kNumModEngines);
      ctx.gate = (t / 60) % 2 == 0;
      slot.process(ctx, p, EnvelopeShape{}, 1 + t % 16);
    }
    slot.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`mod/mod_factory.h` not found).

- [ ] **Step 4: Extend the registry**

In `core/engine/engine_registry.h`, add `#include "engine/mod_engine.h"` before `#include "engine/voice_engine.h"`, and after `const EngineInfo& shaperEngineInfo(int engine);  // unknown -> Off's` add:

```cpp

// Mod engine ids. Saved in projects: never renumber; new engines are appended.
enum class ModEngineKind : int { Off = 0, Envelope = 1, Lfo = 2, Random = 3, FunctionGenerator = 4 };
constexpr int kNumModEngines = 5;
constexpr int kModEngineSelectorMax = 31;  // fixed host range so adding engines changes nothing

int modEngineFromSelector(double value);         // unknown values -> Off
const ModEngineInfo& modEngineInfo(int engine);  // unknown -> Off's
```

In `core/engine/engine_registry.cpp`, add the includes `engine/marbles_engine.h`, `engine/peaks_engine.h`, `engine/stages_engine.h`, `engine/tides_engine.h` (alphabetically among the existing ones), and before the closing `}  // namespace eg13`:

```cpp
int modEngineFromSelector(double value) { return fromSelector(value, kNumModEngines); }

const ModEngineInfo& modEngineInfo(int engine) {
  switch (ModEngineKind(engine)) {
    case ModEngineKind::Envelope: return StagesEngine::staticInfo();
    case ModEngineKind::Lfo: return TidesEngine::staticInfo();
    case ModEngineKind::Random: return MarblesEngine::staticInfo();
    case ModEngineKind::FunctionGenerator: return PeaksEngine::staticInfo();
    case ModEngineKind::Off: break;
  }
  return OffModEngine::staticInfo();
}
```

- [ ] **Step 5: Write `core/mod/mod_factory.h` and `core/mod/mod_factory.cpp`**

```cpp
#pragma once

#include <memory>
#include <vector>

#include "engine/mod_engine.h"

namespace eg13 {

// Every mod engine of one slot instance, index = ModEngineKind.
using ModEngineFactory = std::vector<std::unique_ptr<ModEngine>> (*)();

std::unique_ptr<ModEngine> makeModEngine(int engine);  // index = ModEngineKind; unknown -> Off
std::vector<std::unique_ptr<ModEngine>> makeAllModEngines();

}  // namespace eg13
```

```cpp
#include "mod/mod_factory.h"

#include "engine/engine_registry.h"
#include "engine/marbles_engine.h"
#include "engine/peaks_engine.h"
#include "engine/stages_engine.h"
#include "engine/tides_engine.h"

namespace eg13 {

std::unique_ptr<ModEngine> makeModEngine(int engine) {
  switch (ModEngineKind(engine)) {
    case ModEngineKind::Envelope: return std::make_unique<StagesEngine>();
    case ModEngineKind::Lfo: return std::make_unique<TidesEngine>();
    case ModEngineKind::Random: return std::make_unique<MarblesEngine>();
    case ModEngineKind::FunctionGenerator: return std::make_unique<PeaksEngine>();
    case ModEngineKind::Off: break;
  }
  return std::make_unique<OffModEngine>();
}

std::vector<std::unique_ptr<ModEngine>> makeAllModEngines() {
  std::vector<std::unique_ptr<ModEngine>> all;
  for (int e = 0; e < kNumModEngines; ++e) all.push_back(makeModEngine(e));
  return all;
}

}  // namespace eg13
```

- [ ] **Step 6: Write `core/mod/mod_slot.h` and `core/mod/mod_slot.cpp`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/mod_engine.h"

namespace eg13 {

// One mod slot instance (a part's, or one voice's): every mod engine, preallocated, so changing
// the engine is a pointer swap (spec §2.2). A change of engine is immediate: the new engine
// starts from reset (provisional ruling 34).
class ModSlot {
 public:
  explicit ModSlot(std::vector<std::unique_ptr<ModEngine>> engines);  // index = engine; empty -> Off only

  void prepare(double hostRate);  // main thread: every engine
  void setSeed(uint32_t seed);    // main thread, before reset(): every engine
  void reset();                   // every engine; outputs 0
  void restart();                 // the current engine only (its voice starts from silence)
  void setEngine(int engine);     // audio thread; clamps; a change resets the new engine

  // One tick of n host samples with the current engine. A non-finite output resets the engine
  // and reads 0 for the tick (spec §9).
  void process(const ModContext& ctx, const ParamValues& p, const EnvelopeShape& shape, int n);

  int engine() const { return current_; }
  int engineCount() const { return int(engines_.size()); }
  const ModEngineInfo& info() const { return engines_[size_t(current_)]->info(); }
  const float* outputs() const { return out_; }  // kMaxModOutputs values
  size_t nanResets() const { return nanResets_; }

 private:
  std::vector<std::unique_ptr<ModEngine>> engines_;
  int current_ = 0;
  float out_[kMaxModOutputs] = {};
  size_t nanResets_ = 0;
};

}  // namespace eg13
```

```cpp
#include "mod/mod_slot.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eg13 {

ModSlot::ModSlot(std::vector<std::unique_ptr<ModEngine>> engines) : engines_(std::move(engines)) {
  if (engines_.empty()) engines_.push_back(std::make_unique<OffModEngine>());
}

void ModSlot::prepare(double hostRate) {
  for (auto& e : engines_) e->prepare(hostRate);
  reset();
}

void ModSlot::setSeed(uint32_t seed) {
  for (auto& e : engines_) e->setSeed(seed);
}

void ModSlot::reset() {
  for (auto& e : engines_) e->reset();
  std::fill(std::begin(out_), std::end(out_), 0.0f);
}

void ModSlot::restart() {
  engines_[size_t(current_)]->reset();
  std::fill(std::begin(out_), std::end(out_), 0.0f);
}

void ModSlot::setEngine(int engine) {
  engine = std::clamp(engine, 0, engineCount() - 1);
  if (engine == current_) return;
  current_ = engine;
  restart();
}

void ModSlot::process(const ModContext& ctx, const ParamValues& p, const EnvelopeShape& shape, int n) {
  ModEngine& e = *engines_[size_t(current_)];
  e.setShape(shape);
  e.process(ctx, p, n, out_);
  for (float v : out_) {
    if (!std::isfinite(v)) {
      e.reset();
      std::fill(std::begin(out_), std::end(out_), 0.0f);
      ++nanResets_;
      return;
    }
  }
}

}  // namespace eg13
```

Add `mod/mod_factory.cpp` and `mod/mod_slot.cpp` to the `add_library(eg13_core STATIC ...)` list in `core/CMakeLists.txt` (after `part/part.cpp`). The include root `core/` already serves `mod/...`.

- [ ] **Step 7: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "mod engine registry|mod slot|slot's engine|slot whose engine|envelope shape to|Mod slots switch"`
Expected: all pass. Then the full suite.

- [ ] **Step 8: Commit**

```bash
git add core/engine/engine_registry.h core/engine/engine_registry.cpp core/mod core/CMakeLists.txt tests/test_mod_engines.h tests/test_mod_slot.cpp
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Mod engine registry, factory and ModSlot with NaN recovery"
```

---
### Task 8: Matrix codes and route arithmetic

**Files:**
- Create: `core/mod/mod_routes.h`, `core/mod/mod_routes.cpp`
- Modify: `core/CMakeLists.txt` (add `mod/mod_routes.cpp`), `docs/manual/modulation.md`
- Test: `tests/test_mod_routes.cpp`

**Interfaces:**
- Consumes: `kMaxModOutputs`, `kModParamCount` (Task 2); `clamp01`.
- Produces (`core/mod/mod_routes.h`, namespace `eg13`):
  - `constexpr int kNumModSlots = 4; constexpr int kNumRoutes = 12; constexpr float kPitchRangeSemitones = 24.0f;`
  - `enum ModSourceCode : uint16_t { kSrcNone = 0, kSrcVelocity = 1, kSrcReleaseVelocity = 2, kSrcNote = 3, kSrcGate = 4, kSrcRandom = 5, kSrcPressure = 6, kSrcSlide = 7, kSrcNoteBend = 8, kSrcModWheel = 9, kSrcPitchBend = 10, kSrcChannelPressure = 11, kSrcCcA = 12, kSrcCcB = 13, kSrcCcC = 14, kSrcCcD = 15, kSrcTransportPhase = 16, kSrcMod = 32 }; constexpr int kNumSources = 64; constexpr uint16_t modOutputSource(int slot, int output);`
  - `enum ModDestinationCode : uint16_t { kDestNone = 0, kDestPitch = 1, kDestAmp = 2, kDestPan = 3, kDestPartLevel = 4, kDestSourceP = 16, kDestFilterP = 32, kDestShaperP = 48, kDestPartFxP = 64, kDestModP = 80 }; constexpr int kNumDestinations = 144; constexpr uint16_t modParamDestination(int slot, int p);`
  - `enum class DestinationKind { None, ModParam, Voice, Part }; DestinationKind destinationKind(uint16_t);`
  - `struct Route { uint16_t source, destination, via; };` (`==`, `!=`), `struct RouteSet { Route route[12]; float amount[12]; };`
  - `bool routeActive(const Route&); struct RouteSums { float value[144]; bool touched[144]; }; void sumRoutes(const RouteSet&, const float* source, DestinationKind, RouteSums*); float modulated(float base, const RouteSums&, uint16_t d);`
  - `void sourceName(uint16_t, char*, size_t); void destinationName(uint16_t, char*, size_t);`

These codes are saved in every project (provisional ruling 16): the test pins every value.

- [ ] **Step 1: Write the failing tests (`tests/test_mod_routes.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>

#include "mod/mod_routes.h"

using namespace eg13;

namespace {

std::string sourceText(uint16_t s) {
  char buf[32];
  sourceName(s, buf, sizeof(buf));
  return buf;
}

std::string destinationText(uint16_t d) {
  char buf[32];
  destinationName(d, buf, sizeof(buf));
  return buf;
}

}  // namespace

TEST_CASE("Matrix codes are fixed") {
  // Saved in projects: these values never change.
  REQUIRE(kSrcVelocity == 1);
  REQUIRE(kSrcReleaseVelocity == 2);
  REQUIRE(kSrcNote == 3);
  REQUIRE(kSrcGate == 4);
  REQUIRE(kSrcRandom == 5);
  REQUIRE(kSrcPressure == 6);
  REQUIRE(kSrcSlide == 7);
  REQUIRE(kSrcNoteBend == 8);
  REQUIRE(kSrcModWheel == 9);
  REQUIRE(kSrcPitchBend == 10);
  REQUIRE(kSrcChannelPressure == 11);
  REQUIRE(kSrcCcA == 12);
  REQUIRE(kSrcCcD == 15);
  REQUIRE(kSrcTransportPhase == 16);
  REQUIRE(modOutputSource(0, 0) == 32);
  REQUIRE(modOutputSource(3, 7) == 63);
  REQUIRE(kNumSources == 64);
  REQUIRE(kDestPitch == 1);
  REQUIRE(kDestAmp == 2);
  REQUIRE(kDestPan == 3);
  REQUIRE(kDestPartLevel == 4);
  REQUIRE(kDestSourceP == 16);
  REQUIRE(kDestFilterP == 32);
  REQUIRE(kDestShaperP == 48);
  REQUIRE(kDestPartFxP == 64);
  REQUIRE(modParamDestination(0, 0) == 80);
  REQUIRE(modParamDestination(3, 15) == 143);
  REQUIRE(kNumDestinations == 144);
}

TEST_CASE("Destinations sort into mod-slot, voice and part evaluations") {
  REQUIRE(destinationKind(kDestNone) == DestinationKind::None);
  REQUIRE(destinationKind(kDestPitch) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestAmp) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPan) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPartLevel) == DestinationKind::Part);
  REQUIRE(destinationKind(kDestSourceP + 15) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestFilterP) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestShaperP + 11) == DestinationKind::Voice);
  REQUIRE(destinationKind(kDestPartFxP) == DestinationKind::None);  // M5
  REQUIRE(destinationKind(modParamDestination(2, 5)) == DestinationKind::ModParam);
  REQUIRE(destinationKind(5) == DestinationKind::None);    // reserved
  REQUIRE(destinationKind(144) == DestinationKind::None);  // unknown, from a newer version
}

TEST_CASE("Routes multiply source, amount and via, and add per destination") {
  float source[kNumSources] = {};
  source[kSrcVelocity] = 0.8f;
  source[kSrcModWheel] = 0.5f;
  source[modOutputSource(1, 0)] = -0.25f;
  RouteSet routes;
  routes.route[0] = {kSrcVelocity, uint16_t(kDestFilterP + 0), kSrcNone};
  routes.amount[0] = 0.5f;
  routes.route[1] = {modOutputSource(1, 0), uint16_t(kDestFilterP + 0), kSrcModWheel};
  routes.amount[1] = 1.0f;
  routes.route[2] = {kSrcVelocity, kDestAmp, kSrcNone};
  routes.amount[2] = 0.0f;  // an Amp route that adds nothing still makes the Amp routed
  routes.route[3] = {kSrcVelocity, modParamDestination(0, 2), kSrcNone};
  routes.amount[3] = 1.0f;
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE(sums.value[kDestFilterP] == 0.8f * 0.5f + (-0.25f) * 1.0f * 0.5f);
  REQUIRE(sums.touched[kDestFilterP]);
  REQUIRE(sums.touched[kDestAmp]);
  REQUIRE(sums.value[kDestAmp] == 0.0f);
  REQUIRE_FALSE(sums.touched[modParamDestination(0, 2)]);  // not a voice destination
  REQUIRE_FALSE(sums.touched[kDestPitch]);
  sumRoutes(routes, source, DestinationKind::ModParam, &sums);
  REQUIRE(sums.touched[modParamDestination(0, 2)]);
  REQUIRE(sums.value[modParamDestination(0, 2)] == 0.8f);
  REQUIRE_FALSE(sums.touched[kDestFilterP]);
}

TEST_CASE("Empty, unknown and non-finite routes contribute nothing") {
  float source[kNumSources] = {};
  source[kSrcVelocity] = 1.0f;
  source[kSrcGate] = std::numeric_limits<float>::quiet_NaN();
  RouteSet routes;
  routes.route[0] = {kSrcNone, kDestAmp, kSrcNone};          // no source
  routes.route[1] = {kSrcVelocity, kDestNone, kSrcNone};     // no destination
  routes.route[2] = {200, kDestPitch, kSrcNone};             // unknown source
  routes.route[3] = {kSrcVelocity, kDestPan, 300};           // unknown via
  routes.route[4] = {kSrcGate, uint16_t(kDestSourceP + 1), kSrcNone};  // NaN source
  for (float& a : routes.amount) a = 1.0f;
  REQUIRE_FALSE(routeActive(routes.route[0]));
  REQUIRE_FALSE(routeActive(routes.route[1]));
  REQUIRE_FALSE(routeActive(routes.route[2]));
  REQUIRE_FALSE(routeActive(routes.route[3]));
  REQUIRE(routeActive(routes.route[4]));
  RouteSums sums;
  sumRoutes(routes, source, DestinationKind::Voice, &sums);
  REQUIRE_FALSE(sums.touched[kDestAmp]);
  REQUIRE_FALSE(sums.touched[kDestPitch]);
  REQUIRE_FALSE(sums.touched[kDestPan]);
  REQUIRE(sums.touched[kDestSourceP + 1]);
  REQUIRE(sums.value[kDestSourceP + 1] == 0.0f);
}

TEST_CASE("Modulated values add in normalized space and clamp") {
  RouteSums sums;
  std::fill(std::begin(sums.value), std::end(sums.value), 0.0f);
  std::fill(std::begin(sums.touched), std::end(sums.touched), false);
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 0.3f);
  sums.touched[kDestFilterP] = true;
  sums.value[kDestFilterP] = 0.9f;
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 1.0f);
  sums.value[kDestFilterP] = -0.9f;
  REQUIRE(modulated(0.3f, sums, kDestFilterP) == 0.0f);
  sums.value[kDestFilterP] = 0.25f;
  REQUIRE(modulated(0.5f, sums, kDestFilterP) == 0.75f);
}

TEST_CASE("Sources and destinations have names") {
  REQUIRE(sourceText(kSrcVelocity) == "Velocity");
  REQUIRE(sourceText(kSrcTransportPhase) == "Transport Phase");
  REQUIRE(sourceText(modOutputSource(1, 0)) == "Mod 2 Out 1");
  REQUIRE(sourceText(20) == "?");
  REQUIRE(destinationText(kDestPitch) == "Pitch");
  REQUIRE(destinationText(kDestFilterP) == "Filter P1");
  REQUIRE(destinationText(uint16_t(kDestShaperP + 11)) == "Shaper P12");
  REQUIRE(destinationText(modParamDestination(3, 15)) == "Mod 4 P16");
  REQUIRE(destinationText(kDestPartLevel) == "Part Level");
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`mod/mod_routes.h` not found).

- [ ] **Step 3: Write `core/mod/mod_routes.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/mod_engine.h"

namespace eg13 {

constexpr int kNumModSlots = 4;   // per part (spec §6)
constexpr int kNumRoutes = 12;    // per part (spec §6)
constexpr float kPitchRangeSemitones = 24.0f;  // a route amount of +-100 % into Pitch

// Matrix sources. Saved in projects: append only, never renumber. 17..31 are reserved for fixed
// sources added later; from kSrcMod on, the outputs of the part's mod slots:
// kSrcMod + slot * kMaxModOutputs + output.
enum ModSourceCode : uint16_t {
  kSrcNone = 0,
  kSrcVelocity = 1,
  kSrcReleaseVelocity = 2,
  kSrcNote = 3,            // keytrack: (note - 60) / 120
  kSrcGate = 4,
  kSrcRandom = 5,          // per voice, new on each note
  kSrcPressure = 6,        // poly aftertouch / MPE pressure
  kSrcSlide = 7,           // MPE slide (CC74)
  kSrcNoteBend = 8,        // per-note pitch bend / MPE Bend Range
  kSrcModWheel = 9,
  kSrcPitchBend = 10,
  kSrcChannelPressure = 11,
  kSrcCcA = 12,
  kSrcCcB = 13,
  kSrcCcC = 14,
  kSrcCcD = 15,
  kSrcTransportPhase = 16,  // position in the bar
  kSrcMod = 32,
};
constexpr int kNumSources = kSrcMod + kNumModSlots * kMaxModOutputs;  // 64
constexpr uint16_t modOutputSource(int slot, int output) {
  return uint16_t(kSrcMod + slot * kMaxModOutputs + output);
}

// Matrix destinations. Saved in projects: append only, never renumber. Each slot's range has
// room for 16 P's; Part FX P's are reserved for M5 (accepted, ignored until then).
enum ModDestinationCode : uint16_t {
  kDestNone = 0,
  kDestPitch = 1,      // semitones: amount x kPitchRangeSemitones
  kDestAmp = 2,
  kDestPan = 3,        // centre 0.5 in normalized space
  kDestPartLevel = 4,
  kDestSourceP = 16,   // + P index (0..15)
  kDestFilterP = 32,
  kDestShaperP = 48,
  kDestPartFxP = 64,
  kDestModP = 80,      // + slot * kModParamCount + P index
};
constexpr int kNumDestinations = kDestModP + kNumModSlots * kModParamCount;  // 144
constexpr uint16_t modParamDestination(int slot, int p) { return uint16_t(kDestModP + slot * kModParamCount + p); }

// Which evaluation a destination belongs to. Part FX P's are None until M5.
enum class DestinationKind { None, ModParam, Voice, Part };
DestinationKind destinationKind(uint16_t destination);

// A route (spec §6): source x amount x via (via None = 1) into destination. Source, destination
// and via are patch data; the amount is a host parameter.
struct Route {
  uint16_t source = kSrcNone;
  uint16_t destination = kDestNone;
  uint16_t via = kSrcNone;
};
bool operator==(const Route& a, const Route& b);
inline bool operator!=(const Route& a, const Route& b) { return !(a == b); }

struct RouteSet {
  Route route[kNumRoutes];
  float amount[kNumRoutes] = {};  // -1..1
};

// A route does something when its source and destination are known codes other than None and
// its via is None or known. Unknown codes (from a newer version) are kept but ignored.
bool routeActive(const Route& route);

// The sum of every active route into each destination of one kind. touched[d] is true when an
// active route targets d, even if it adds 0 (the Amp's "routed" test, spec §5.2).
struct RouteSums {
  float value[kNumDestinations];
  bool touched[kNumDestinations];
};
// source: kNumSources values. A non-finite contribution adds 0.
void sumRoutes(const RouteSet& routes, const float* source, DestinationKind kind, RouteSums* out);
// base + the routes into d, clamped to 0..1 (spec §6: routes add in normalized space and clamp).
inline float modulated(float base, const RouteSums& sums, uint16_t d) {
  return sums.touched[d] ? clamp01(base + sums.value[d]) : base;
}

void sourceName(uint16_t source, char* buf, size_t size);            // "Velocity", "Mod 2 Out 1"
void destinationName(uint16_t destination, char* buf, size_t size);  // "Pitch", "Filter P1"

}  // namespace eg13
```

- [ ] **Step 4: Write `core/mod/mod_routes.cpp`**

```cpp
#include "mod/mod_routes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace eg13 {

namespace {

const char* const kFixedSources[] = {"None",           "Velocity",   "Release Velocity", "Note",
                                     "Gate",           "Random",     "Pressure",         "Slide",
                                     "Note Bend",      "Mod Wheel",  "Pitch Bend",       "Channel Pressure",
                                     "CC A",           "CC B",       "CC C",             "CC D",
                                     "Transport Phase"};
constexpr int kNumFixedSourceNames = int(sizeof(kFixedSources) / sizeof(kFixedSources[0]));

bool knownSource(uint16_t s) { return s > kSrcNone && s < kNumSources; }

}  // namespace

bool operator==(const Route& a, const Route& b) {
  return a.source == b.source && a.destination == b.destination && a.via == b.via;
}

DestinationKind destinationKind(uint16_t d) {
  if (d == kDestPartLevel) return DestinationKind::Part;
  if (d == kDestPitch || d == kDestAmp || d == kDestPan) return DestinationKind::Voice;
  if (d >= kDestSourceP && d < kDestPartFxP) return DestinationKind::Voice;  // Source, Filter, Shaper P's
  if (d >= kDestModP && d < kNumDestinations) return DestinationKind::ModParam;
  return DestinationKind::None;
}

bool routeActive(const Route& r) {
  return knownSource(r.source) && destinationKind(r.destination) != DestinationKind::None &&
         (r.via == kSrcNone || knownSource(r.via));
}

void sumRoutes(const RouteSet& routes, const float* source, DestinationKind kind, RouteSums* out) {
  std::fill(std::begin(out->value), std::end(out->value), 0.0f);
  std::fill(std::begin(out->touched), std::end(out->touched), false);
  for (int r = 0; r < kNumRoutes; ++r) {
    const Route& route = routes.route[r];
    if (!routeActive(route) || destinationKind(route.destination) != kind) continue;
    float v = source[route.source] * routes.amount[r];
    if (route.via != kSrcNone) v *= source[route.via];
    out->value[route.destination] += std::isfinite(v) ? v : 0.0f;
    out->touched[route.destination] = true;
  }
}

void sourceName(uint16_t s, char* buf, size_t size) {
  if (s < kNumFixedSourceNames) {
    std::snprintf(buf, size, "%s", kFixedSources[s]);
  } else if (s >= kSrcMod && s < kNumSources) {
    const int slot = (s - kSrcMod) / kMaxModOutputs;
    std::snprintf(buf, size, "Mod %d Out %d", slot + 1, (s - kSrcMod) % kMaxModOutputs + 1);
  } else {
    std::snprintf(buf, size, "?");
  }
}

void destinationName(uint16_t d, char* buf, size_t size) {
  if (d == kDestNone) {
    std::snprintf(buf, size, "None");
  } else if (d == kDestPitch) {
    std::snprintf(buf, size, "Pitch");
  } else if (d == kDestAmp) {
    std::snprintf(buf, size, "Amp");
  } else if (d == kDestPan) {
    std::snprintf(buf, size, "Pan");
  } else if (d == kDestPartLevel) {
    std::snprintf(buf, size, "Part Level");
  } else if (d >= kDestSourceP && d < kDestFilterP) {
    std::snprintf(buf, size, "Source P%d", d - kDestSourceP + 1);
  } else if (d >= kDestFilterP && d < kDestShaperP) {
    std::snprintf(buf, size, "Filter P%d", d - kDestFilterP + 1);
  } else if (d >= kDestShaperP && d < kDestPartFxP) {
    std::snprintf(buf, size, "Shaper P%d", d - kDestShaperP + 1);
  } else if (d >= kDestPartFxP && d < kDestPartFxP + 16) {
    std::snprintf(buf, size, "Part FX P%d", d - kDestPartFxP + 1);
  } else if (d >= kDestModP && d < kNumDestinations) {
    std::snprintf(buf, size, "Mod %d P%d", (d - kDestModP) / kModParamCount + 1, (d - kDestModP) % kModParamCount + 1);
  } else {
    std::snprintf(buf, size, "?");
  }
}

}  // namespace eg13
```

Add `mod/mod_routes.cpp` to `core/CMakeLists.txt` after `mod/mod_factory.cpp`.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Matrix codes|Destinations sort|Routes multiply|contribute nothing|normalized space|have names"`
Expected: all pass. Then the full suite.

- [ ] **Step 6: Document the matrix**

Append to `docs/manual/modulation.md`:

````markdown
## The matrix

Each of a part's twelve routes has a **source**, a **destination**, an **amount** from −100 % to +100 % (the host parameter *Route 1* … *Route 12*), and optionally a second source, **via**, that scales the amount (for example an LFO into pitch *via* the mod wheel: the wheel sets the vibrato depth). A route adds source × amount × via to its destination.

**Sources**

- Per note: *Velocity*, *Release Velocity* (how fast the key was let go; 0 until it is), *Note* (the keyboard position: C4 is 0, and 100 % into any filter's cutoff makes the filter follow the keyboard exactly, an octave per octave), *Gate* (100 % while the key is held), *Random* (a new value from 0 to 100 % on each note), *Pressure* (polyphonic aftertouch or MPE pressure), *Slide* (MPE slide, CC74), *Note Bend* (MPE per-note pitch bend: 100 % at the MPE Bend Range).
- Per part: *Mod Wheel* (CC1), *Pitch Bend* (−100 % to +100 %), *Channel Pressure*, *CC A* to *CC D* (four controllers you choose with the part's CC A–D settings), *Transport Phase* (where the song is within the current bar, 0 to 100 %; it keeps running at the last tempo while the transport is stopped).
- Each mod slot's outputs.

**Destinations**: any control of the Source, Filter or Shaper (P1, P2, …), *Pitch*, *Amp*, *Pan*, *Part Level*, and any control of the part's mod slots. (Part FX controls arrive with the Part FX.)

**How routes add up**: every control is a position from 0 to 100 %. The routes into it are added to where its knob is, and the result stops at 0 % and 100 %. So a cutoff at 70 % with +30 % from an envelope opens fully at the envelope's peak; two routes of +80 % into a control at 50 % stop at 100 %. A stepped control (a mode or a model) moves through its options the same way.

- **Pitch** is in semitones: ±100 % is ±24 semitones (so 2 % is about half a semitone). Pitch routes come on top of the part's pitch bend and transpose.
- **Pan** starts from the centre: −50 % is fully left, +50 % fully right.
- **Amp**: see Amp and voices below.
- **Part Level** adds to the part's Level setting.
````

- [ ] **Step 7: Commit**

```bash
git add core/mod/mod_routes.h core/mod/mod_routes.cpp core/CMakeLists.txt tests/test_mod_routes.cpp docs/manual/modulation.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Matrix: fixed source and destination codes, route sums in normalized space"
```

---
### Task 9: PartModulation: per-voice and per-part slots, tick order, feedback

**Files:**
- Create: `core/voice/slot_params.h`, `core/mod/voice_control.h`, `core/mod/part_modulation.h`, `core/mod/part_modulation.cpp`
- Modify: `core/voice/voice.h` (take `SlotParams` from `slot_params.h`), `core/CMakeLists.txt` (add `mod/part_modulation.cpp`), `tests/test_mod_engines.h`, `docs/manual/modulation.md`
- Test: `tests/test_part_modulation.cpp`

**Interfaces:**
- Consumes: `ModSlot`, `ModEngineFactory`, `makeAllModEngines` (Task 7); `RouteSet`, `RouteSums`, `sumRoutes`, `modulated`, the codes (Task 8); `ModContext`, `EnvelopeShape` (Task 2).
- Produces:
  - `core/voice/slot_params.h`: `struct SlotParams { ParamValues source, filter, shaper; };` (moved out of `voice.h`, which includes it, so nothing else changes).
  - `core/mod/voice_control.h`: `struct VoiceControl { SlotParams params; float pitch = 0; bool ampRouted = false; float amp = 1; float pan = 0; float gain = 1; float sourceGain = 1; };`
  - `core/mod/part_modulation.h`: `struct ModParams { ParamValues slot[4]; RouteSet routes; EnvelopeShape shapes[4]; }; struct ModVoiceInputs { bool active, gate, retrigger; float note, velocity, releaseVelocity, random, pressure, slide, bend; }; struct ModPartInputs { float modWheel, pitchBend, channelPressure, slide, cc[4], transportPhase, noteBendRange; bool gate, retrigger; double beat, beatsPerSample; bool beatJump; int lastVoice; };`
  - `class PartModulation { static constexpr int kVoices = 8; explicit PartModulation(ModEngineFactory = makeAllModEngines); void setSeedBase(uint32_t); void prepare(double hostRate); void reset(); void configure(int slot, int engine, bool perPart); int engine(int) const; bool perPart(int) const; void startVoice(int v); void processPart(const ModParams&, const ModPartInputs&, const ModVoiceInputs* voices, int n); void processVoice(int v, const ModParams&, const ModPartInputs&, const ModVoiceInputs&, const SlotParams& base, VoiceControl*, int n); float partLevel(const ModParams&, float base) const; const float* outputs(int slot, int voice) const; };`
  - `tests/test_mod_engines.h`: `test::EchoModEngine`, `test::StepModEngine`, `enum TestModEngine { kTestOff, kTestEcho, kTestStep, kTestNan }`, `test::testModEngines()`.

The evaluation order is the matrix feedback rule (provisional ruling 5): every route into a mod slot's P's reads the outputs of the previous tick (per-voice slots: this voice's; per-part slots: the part's), then the slots run, then every route into the voice's controls reads this tick's outputs. A loop is a one-tick delay, bounded by the clamp; the slot order does not matter.

- [ ] **Step 1: Move `SlotParams` into its own header**

Create `core/voice/slot_params.h`:

```cpp
#pragma once

#include "engine/voice_engine.h"

namespace eg13 {

// Post-modulation parameters of each slot in the voice chain.
struct SlotParams {
  ParamValues source;
  ParamValues filter;
  ParamValues shaper;
};

}  // namespace eg13
```

In `core/voice/voice.h`, delete the `SlotParams` struct and its comment ("Post-modulation parameters of each slot in the voice chain.") and add `#include "voice/slot_params.h"` after `#include "voice/processor_slot.h"`.

Create `core/mod/voice_control.h`:

```cpp
#pragma once

#include "voice/slot_params.h"

namespace eg13 {

// What the matrix gives one voice for one control tick (spec §5.2, §6): its modulated slot
// parameters, pitch, Amp and pan, and the gains of gain staging. The Voice moves the Amp, pan
// and gains in straight lines across the tick.
struct VoiceControl {
  SlotParams params;
  float pitch = 0.0f;       // semitones added to the note: matrix Pitch + per-note bend
  bool ampRouted = false;   // a route targets the Amp: amp is its level; otherwise the gate's
  float amp = 1.0f;         // 0..1, when routed
  float pan = 0.0f;         // -1 (left) .. 1 (right)
  float gain = 1.0f;        // Voice Gain: -12 dB or 0 dB (the Amp's gain)
  float sourceGain = 1.0f;  // the Source's trim x Audio Input chord scaling
};

}  // namespace eg13
```

- [ ] **Step 2: Add the test mod engines to `tests/test_mod_engines.h`**

Add `#include <memory>` and `#include <vector>` after `#include <limits>`, and before the closing `}  // namespace eg13::test`:

```cpp
// Outputs its own P1..P8 (to watch what the matrix gives a slot).
class EchoModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override {}
  void process(const ModContext&, const ParamValues& p, int, float* out) override {
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = p.p[k];
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[8] = {{"P1", false}, {"P2", false}, {"P3", false}, {"P4", false},
                                              {"P5", false}, {"P6", false}, {"P7", false}, {"P8", false}};
    static const ModEngineInfo kInfo = {{"echo", "Echo", 0, nullptr, false}, 8, kOutputs, false};
    return kInfo;
  }
};

// Counts its ticks and reports what it hears: output 0 = 0.01 x ticks since reset, 1 = gate,
// 2 = 0.1 x retriggers since reset, 3 = the tick's beat position, 4 = 0.001 x samples since reset.
class StepModEngine final : public ModEngine {
 public:
  void prepare(double) override {}
  void reset() override { ticks_ = retriggers_ = samples_ = 0; }
  void process(const ModContext& ctx, const ParamValues&, int n, float* out) override {
    ++ticks_;
    if (ctx.retrigger) ++retriggers_;
    samples_ += n;
    for (int k = 0; k < kMaxModOutputs; ++k) out[k] = 0.0f;
    out[0] = 0.01f * float(ticks_);
    out[1] = ctx.gate ? 1.0f : 0.0f;
    out[2] = 0.1f * float(retriggers_);
    out[3] = float(ctx.beat);
    out[4] = 0.001f * float(samples_);
  }
  const ModEngineInfo& info() const override {
    static const ModOutputInfo kOutputs[5] = {{"Ticks", false}, {"Gate", false}, {"Retriggers", false},
                                              {"Beat", false}, {"Samples", false}};
    static const ModEngineInfo kInfo = {{"step", "Step", 0, nullptr, false}, 5, kOutputs, false};
    return kInfo;
  }

 private:
  int ticks_ = 0;
  int retriggers_ = 0;
  int samples_ = 0;
};

// Test mod slots: 0 Off, 1 Echo, 2 Step, 3 NaN.
enum TestModEngine : int { kTestOff = 0, kTestEcho = 1, kTestStep = 2, kTestNan = 3 };
inline std::vector<std::unique_ptr<ModEngine>> testModEngines() {
  std::vector<std::unique_ptr<ModEngine>> engines;
  engines.push_back(std::make_unique<OffModEngine>());
  engines.push_back(std::make_unique<EchoModEngine>());
  engines.push_back(std::make_unique<StepModEngine>());
  engines.push_back(std::make_unique<NanModEngine>());
  return engines;
}

```

- [ ] **Step 3: Write the failing tests (`tests/test_part_modulation.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "alloc_counter.h"
#include "mod/part_modulation.h"
#include "test_mod_engines.h"

using namespace eg13;

namespace {

struct Rig {
  PartModulation mod{test::testModEngines};
  ModParams params;
  ModPartInputs part;
  ModVoiceInputs voices[PartModulation::kVoices];
  SlotParams base;
  Rig() {
    mod.prepare(48000.0);
    for (auto& v : voices) v.active = true;
  }
  void route(int r, uint16_t source, uint16_t destination, float amount, uint16_t via = kSrcNone) {
    params.routes.route[r] = {source, destination, via};
    params.routes.amount[r] = amount;
  }
  VoiceControl tick(int v = 0) {
    mod.processPart(params, part, voices, 16);
    VoiceControl c;
    mod.processVoice(v, params, part, voices[v], base, &c, 16);
    part.retrigger = false;
    voices[v].retrigger = false;
    return c;
  }
};

}  // namespace

TEST_CASE("Routes into mod-slot P's read the previous tick whatever the slot order") {
  for (int order = 0; order < 2; ++order) {
    Rig rig;
    const int step = order == 0 ? 0 : 3;
    const int echo = order == 0 ? 1 : 0;
    rig.mod.configure(step, test::kTestStep, false);
    rig.mod.configure(echo, test::kTestEcho, false);
    rig.route(0, modOutputSource(step, 0), modParamDestination(echo, 0), 1.0f);
    for (int k = 1; k <= 20; ++k) {
      rig.tick();
      INFO("order " << order << " tick " << k);
      REQUIRE(rig.mod.outputs(step, 0)[0] == 0.01f * float(k));
      REQUIRE(std::fabs(rig.mod.outputs(echo, 0)[0] - 0.01f * float(k - 1)) < 1e-6f);
    }
  }
}

TEST_CASE("A mod slot routed into its own P settles and stays bounded") {
  Rig rig;
  rig.mod.configure(0, test::kTestEcho, false);
  rig.params.slot[0].p[0] = 0.2f;
  rig.route(0, modOutputSource(0, 0), modParamDestination(0, 0), 0.5f);  // x' = 0.2 + 0.5 x
  for (int k = 0; k < 100; ++k) {
    rig.tick();
    REQUIRE(rig.mod.outputs(0, 0)[0] <= 1.0f);
  }
  REQUIRE(std::fabs(rig.mod.outputs(0, 0)[0] - 0.4f) < 1e-5f);
  rig.params.routes.amount[0] = 1.0f;  // x' = 0.2 + x: grows until the clamp
  for (int k = 0; k < 100; ++k) rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 1.0f);
}

TEST_CASE("Voice controls read this tick's mod outputs") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  rig.base.filter.p[0] = 0.5f;
  rig.route(0, modOutputSource(0, 0), uint16_t(kDestFilterP + 0), 1.0f);
  for (int k = 1; k <= 10; ++k) {
    const VoiceControl c = rig.tick();
    REQUIRE(std::fabs(c.params.filter.p[0] - (0.5f + 0.01f * float(k))) < 1e-6f);
    REQUIRE(c.params.filter.p[1] == rig.base.filter.p[1]);
  }
}

TEST_CASE("Per-part slots run once per tick, see the part's gate and are shared by every voice") {
  Rig rig;
  rig.mod.configure(1, test::kTestStep, true);
  rig.base.source.p[2] = 0.25f;
  rig.route(0, modOutputSource(1, 1), uint16_t(kDestSourceP + 2), 0.5f);  // the part gate
  rig.part.gate = true;
  rig.part.retrigger = true;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  VoiceControl a, b;
  rig.mod.processVoice(0, rig.params, rig.part, rig.voices[0], rig.base, &a, 16);
  rig.mod.processVoice(5, rig.params, rig.part, rig.voices[5], rig.base, &b, 16);
  REQUIRE(rig.mod.outputs(1, -1)[0] == 0.01f);  // one tick, not one per voice
  REQUIRE(rig.mod.outputs(1, -1)[2] == 0.1f);   // one retrigger
  REQUIRE(a.params.source.p[2] == 0.75f);
  REQUIRE(b.params.source.p[2] == 0.75f);
  REQUIRE(rig.mod.outputs(1, 0)[0] == 0.0f);  // the per-voice instance did not run
}

TEST_CASE("Per-voice slots see their own voice's gate and retrigger") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  rig.voices[2].gate = true;
  rig.voices[2].retrigger = true;
  VoiceControl c;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  rig.mod.processVoice(2, rig.params, rig.part, rig.voices[2], rig.base, &c, 16);
  rig.mod.processVoice(3, rig.params, rig.part, rig.voices[3], rig.base, &c, 16);
  REQUIRE(rig.mod.outputs(0, 2)[1] == 1.0f);
  REQUIRE(rig.mod.outputs(0, 2)[2] == 0.1f);
  REQUIRE(rig.mod.outputs(0, 3)[1] == 0.0f);
  REQUIRE(rig.mod.outputs(0, 3)[2] == 0.0f);
  rig.mod.startVoice(2);
  REQUIRE(rig.mod.outputs(0, 2)[0] == 0.0f);
}

TEST_CASE("Pitch, Amp and Pan follow their routes") {
  Rig rig;
  rig.voices[0].velocity = 0.8f;
  rig.voices[0].bend = 1.5f;
  rig.part.modWheel = 0.5f;
  VoiceControl c = rig.tick();
  REQUIRE_FALSE(c.ampRouted);
  REQUIRE(c.pitch == 1.5f);  // the per-note bend alone
  REQUIRE(c.pan == 0.0f);
  rig.route(0, kSrcVelocity, kDestPitch, 0.5f, kSrcModWheel);   // 0.8 * 0.5 * 0.5 = 0.2 -> 4.8 st
  rig.route(1, kSrcVelocity, kDestAmp, 1.0f);
  rig.route(2, kSrcVelocity, kDestAmp, 1.0f);                   // 1.6, clamped to 1
  rig.route(3, kSrcModWheel, kDestPan, -0.5f);                  // 0.5 - 0.25 -> -0.5
  c = rig.tick();
  REQUIRE(std::fabs(c.pitch - (0.2f * 24.0f + 1.5f)) < 1e-5f);
  REQUIRE(c.ampRouted);
  REQUIRE(c.amp == 1.0f);
  REQUIRE(std::fabs(c.pan + 0.5f) < 1e-6f);
  rig.params.routes.amount[1] = 0.0f;
  rig.params.routes.amount[2] = 0.0f;
  c = rig.tick();
  REQUIRE(c.ampRouted);  // routed at zero: silent, not open
  REQUIRE(c.amp == 0.0f);
}

TEST_CASE("Fixed sources: keytrack, note bend, slide, release velocity") {
  Rig rig;
  rig.voices[0].note = 72.0f;
  rig.voices[0].bend = 24.0f;
  rig.voices[0].slide = 0.25f;
  rig.voices[0].releaseVelocity = 0.6f;
  rig.part.slide = 0.5f;
  rig.part.noteBendRange = 48.0f;
  rig.mod.configure(0, test::kTestEcho, false);
  rig.route(0, kSrcNote, modParamDestination(0, 0), 1.0f);
  rig.route(1, kSrcNoteBend, modParamDestination(0, 1), 1.0f);
  rig.route(2, kSrcSlide, modParamDestination(0, 2), 1.0f);
  rig.route(3, kSrcReleaseVelocity, modParamDestination(0, 3), 1.0f);
  rig.tick();
  const float* out = rig.mod.outputs(0, 0);
  REQUIRE(out[0] == 0.1f);   // (72 - 60) / 120: 100 % tracks an octave per octave on the cutoff
  REQUIRE(out[1] == 0.5f);   // 24 of 48 semitones
  REQUIRE(out[2] == 0.5f);   // the larger of the part's and the voice's slide
  REQUIRE(out[3] == 0.6f);
}

TEST_CASE("Part-level routes read the most recently started voice") {
  Rig rig;
  rig.voices[3].velocity = 0.6f;
  rig.part.lastVoice = 3;
  rig.route(0, kSrcVelocity, kDestPartLevel, -0.5f);
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  REQUIRE(std::fabs(rig.mod.partLevel(rig.params, 0.8f) - 0.5f) < 1e-6f);
  rig.part.lastVoice = -1;
  rig.mod.processPart(rig.params, rig.part, rig.voices, 16);
  REQUIRE(rig.mod.partLevel(rig.params, 0.8f) == 0.8f);
}

TEST_CASE("A NaN from a mod engine contributes nothing") {
  Rig rig;
  rig.mod.configure(0, test::kTestNan, false);
  rig.base.shaper.p[0] = 0.3f;
  rig.route(0, modOutputSource(0, 0), uint16_t(kDestShaperP + 0), 1.0f);
  rig.route(1, modOutputSource(0, 0), kDestPitch, 1.0f);
  const VoiceControl c = rig.tick();
  REQUIRE(c.params.shaper.p[0] == 0.3f);
  REQUIRE(c.pitch == 0.0f);
}

TEST_CASE("Changing a slot's engine or scope restarts the instances it starts using") {
  Rig rig;
  rig.mod.configure(0, test::kTestStep, false);
  for (int k = 0; k < 5; ++k) rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 0.01f * 5.0f);
  rig.mod.configure(0, test::kTestStep, true);
  rig.tick();
  REQUIRE(rig.mod.outputs(0, -1)[0] == 0.01f);
  rig.mod.configure(0, test::kTestStep, false);
  rig.tick();
  REQUIRE(rig.mod.outputs(0, 0)[0] == 0.01f);
  rig.mod.configure(0, test::kTestEcho, false);
  REQUIRE(rig.mod.engine(0) == test::kTestEcho);
  rig.mod.configure(0, 77, false);
  REQUIRE(rig.mod.engine(0) == test::kTestNan);  // clamped to the last engine
}

TEST_CASE("Part modulation with the real engines does not allocate") {
  PartModulation mod;
  mod.setSeedBase(100);
  mod.prepare(48000.0);
  ModParams params;
  for (auto& slot : params.slot) {
    for (float& v : slot.p) v = 0.5f;
  }
  params.routes.route[0] = {modOutputSource(0, 0), kDestAmp, kSrcVelocity};
  params.routes.route[1] = {modOutputSource(1, 0), uint16_t(kDestFilterP), kSrcNone};
  params.routes.route[2] = {modOutputSource(2, 0), modParamDestination(3, 2), kSrcNone};
  params.routes.route[3] = {modOutputSource(3, 3), kDestPitch, kSrcModWheel};
  for (float& a : params.routes.amount) a = 0.5f;
  ModPartInputs part;
  ModVoiceInputs voices[PartModulation::kVoices];
  for (auto& v : voices) {
    v.active = true;
    v.gate = true;
    v.velocity = 0.7f;
  }
  const SlotParams base;
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 600; ++t) {
      if (t % 150 == 0) {
        for (int s = 0; s < kNumModSlots; ++s) mod.configure(s, (t / 150 + s) % 5, s == 3);
      }
      part.beat = double(t) * 0.01;
      mod.processPart(params, part, voices, 16);
      for (int v = 0; v < PartModulation::kVoices; ++v) {
        VoiceControl c;
        mod.processVoice(v, params, part, voices[v], base, &c, 16);
      }
      mod.partLevel(params, 0.5f);
    }
    mod.startVoice(3);
    mod.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 4: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`mod/part_modulation.h` not found).

- [ ] **Step 5: Write `core/mod/part_modulation.h`**

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "mod/mod_factory.h"
#include "mod/mod_routes.h"
#include "mod/mod_slot.h"
#include "mod/voice_control.h"

namespace eg13 {

// The host values a part's modulation reads in one call: every mod slot's P1..P16, the routes and
// their amounts, and each slot's Custom envelope shape (patch data).
struct ModParams {
  ParamValues slot[kNumModSlots];
  RouteSet routes;
  EnvelopeShape shapes[kNumModSlots];
};

// One voice's fixed sources for a tick (the Part keeps them).
struct ModVoiceInputs {
  bool active = false;     // the voice is sounding: its per-voice slots run
  bool gate = false;       // the per-voice mod engines' gate
  bool retrigger = false;  // a note started on the voice since the last tick
  float note = 60.0f;      // the voice's key, glided
  float velocity = 0.0f;
  float releaseVelocity = 0.0f;
  float random = 0.0f;
  float pressure = 0.0f;
  float slide = 0.0f;
  float bend = 0.0f;  // per-note bend, semitones
};

// The part's fixed sources and timing for a tick.
struct ModPartInputs {
  float modWheel = 0.0f;
  float pitchBend = 0.0f;  // -1..1
  float channelPressure = 0.0f;
  float slide = 0.0f;      // part-wide CC74 (provisional ruling 35)
  float cc[4] = {};
  float transportPhase = 0.0f;
  float noteBendRange = 48.0f;  // semitones that read as 100 % Note Bend
  bool gate = false;       // the per-part mod engines' gate: any key held
  bool retrigger = false;  // the first key went down since the last tick
  double beat = 0.0;
  double beatsPerSample = 0.0;
  bool beatJump = false;
  int lastVoice = -1;      // the most recently started voice (provisional ruling 6)
};

// A part's mod slots and matrix (spec §6): 4 part-scope slot instances and 4 per voice, each
// holding every mod engine. Per tick the Part calls processPart() once, then processVoice() for
// each sounding voice.
//
// Order and feedback (provisional ruling 5): routes into a mod slot's P's read every mod output
// as it was at the end of the previous tick; routes into the voice's controls read this tick's.
// So a loop between slots (or a slot into itself) is a one-tick delay, bounded by the 0..1 clamp,
// and the result does not depend on slot order.
class PartModulation {
 public:
  static constexpr int kVoices = 8;

  explicit PartModulation(ModEngineFactory factory = makeAllModEngines);

  void setSeedBase(uint32_t base);  // main thread, before prepare(): every instance its own seed
  void prepare(double hostRate);    // main thread
  void reset();
  // Audio thread: slot `slot` plays `engine`, per part or per voice. A new engine starts from
  // reset; the instances a scope change starts using start from reset too.
  void configure(int slot, int engine, bool perPart);
  int engine(int slot) const { return engine_[slot]; }
  bool perPart(int slot) const { return perPart_[slot]; }
  void startVoice(int v);  // the voice starts from silence: its per-voice slots restart

  void processPart(const ModParams& params, const ModPartInputs& part, const ModVoiceInputs* voices, int n);
  void processVoice(int v, const ModParams& params, const ModPartInputs& part, const ModVoiceInputs& voice,
                    const SlotParams& base, VoiceControl* control, int n);
  // base + the routes into Part Level (after processPart()), clamped.
  float partLevel(const ModParams& params, float base) const;

  const float* outputs(int slot, int voice) const;  // voice < 0: the part instance

 private:
  ModSlot& voiceSlot(int v, int slot) { return voiceSlots_[size_t(v * kNumModSlots + slot)]; }
  const ModSlot& voiceSlot(int v, int slot) const { return voiceSlots_[size_t(v * kNumModSlots + slot)]; }
  void fillFixed(float* source, const ModPartInputs& part, const ModVoiceInputs* voice) const;
  // Mod outputs: per-voice slots from `voice` (none: 0); part slots this tick's or the previous.
  void fillMods(float* source, int voice, bool partCurrent) const;

  std::vector<ModSlot> voiceSlots_;  // [voice * kNumModSlots + slot]
  std::vector<ModSlot> partSlots_;
  int engine_[kNumModSlots] = {};
  bool perPart_[kNumModSlots] = {};
  float partPrevious_[kNumModSlots][kMaxModOutputs] = {};
  float partSource_[kNumSources] = {};
};

}  // namespace eg13
```

- [ ] **Step 6: Write `core/mod/part_modulation.cpp`**

```cpp
#include "mod/part_modulation.h"

#include <algorithm>
#include <iterator>

namespace eg13 {

namespace {
const float kNoOutputs[kMaxModOutputs] = {};
}  // namespace

PartModulation::PartModulation(ModEngineFactory factory) {
  voiceSlots_.reserve(size_t(kVoices * kNumModSlots));
  for (int i = 0; i < kVoices * kNumModSlots; ++i) voiceSlots_.emplace_back(factory());
  partSlots_.reserve(size_t(kNumModSlots));
  for (int s = 0; s < kNumModSlots; ++s) partSlots_.emplace_back(factory());
}

void PartModulation::setSeedBase(uint32_t base) {
  for (int s = 0; s < kNumModSlots; ++s) {
    for (int v = 0; v < kVoices; ++v) voiceSlot(v, s).setSeed(base + uint32_t(s * 16 + v + 1));
    partSlots_[size_t(s)].setSeed(base + uint32_t(s * 16 + kVoices + 1));
  }
}

void PartModulation::prepare(double hostRate) {
  for (auto& slot : voiceSlots_) slot.prepare(hostRate);
  for (auto& slot : partSlots_) slot.prepare(hostRate);
  reset();
}

void PartModulation::reset() {
  for (auto& slot : voiceSlots_) slot.reset();
  for (auto& slot : partSlots_) slot.reset();
  for (auto& outputs : partPrevious_) std::fill(std::begin(outputs), std::end(outputs), 0.0f);
  std::fill(std::begin(partSource_), std::end(partSource_), 0.0f);
}

void PartModulation::configure(int slot, int engine, bool perPart) {
  if (engine != engine_[slot]) {
    for (int v = 0; v < kVoices; ++v) voiceSlot(v, slot).setEngine(engine);
    partSlots_[size_t(slot)].setEngine(engine);
    engine_[slot] = partSlots_[size_t(slot)].engine();
  }
  if (perPart != perPart_[slot]) {
    if (perPart) {
      partSlots_[size_t(slot)].restart();
    } else {
      for (int v = 0; v < kVoices; ++v) voiceSlot(v, slot).restart();
    }
    perPart_[slot] = perPart;
  }
}

void PartModulation::startVoice(int v) {
  for (int s = 0; s < kNumModSlots; ++s) voiceSlot(v, s).restart();
}

const float* PartModulation::outputs(int slot, int voice) const {
  return voice < 0 ? partSlots_[size_t(slot)].outputs() : voiceSlot(voice, slot).outputs();
}

void PartModulation::fillFixed(float* source, const ModPartInputs& part, const ModVoiceInputs* voice) const {
  std::fill(source, source + kNumSources, 0.0f);
  source[kSrcModWheel] = part.modWheel;
  source[kSrcPitchBend] = part.pitchBend;
  source[kSrcChannelPressure] = part.channelPressure;
  for (int i = 0; i < 4; ++i) source[kSrcCcA + i] = part.cc[i];
  source[kSrcTransportPhase] = part.transportPhase;
  float slide = part.slide;
  if (voice != nullptr) {
    source[kSrcVelocity] = voice->velocity;
    source[kSrcReleaseVelocity] = voice->releaseVelocity;
    source[kSrcNote] = (voice->note - 60.0f) / 120.0f;  // provisional ruling 17
    source[kSrcGate] = voice->gate ? 1.0f : 0.0f;
    source[kSrcRandom] = voice->random;
    source[kSrcPressure] = voice->pressure;
    slide = std::max(slide, voice->slide);
    source[kSrcNoteBend] =
        part.noteBendRange > 0.0f ? std::clamp(voice->bend / part.noteBendRange, -1.0f, 1.0f) : 0.0f;
  }
  source[kSrcSlide] = slide;
}

void PartModulation::fillMods(float* source, int voice, bool partCurrent) const {
  for (int s = 0; s < kNumModSlots; ++s) {
    const float* outs = perPart_[s] ? (partCurrent ? partSlots_[size_t(s)].outputs() : partPrevious_[s])
                        : voice >= 0 ? voiceSlot(voice, s).outputs()
                                     : kNoOutputs;
    std::copy(outs, outs + kMaxModOutputs, source + modOutputSource(s, 0));
  }
}

namespace {

ModContext contextFor(const ModPartInputs& part, bool gate, bool retrigger) {
  ModContext ctx;
  ctx.gate = gate;
  ctx.retrigger = retrigger;
  ctx.beat = part.beat;
  ctx.beatsPerSample = part.beatsPerSample;
  ctx.beatJump = part.beatJump;
  return ctx;
}

ParamValues modulatedSlot(const ParamValues& base, const RouteSums& sums, int slot) {
  ParamValues p = base;
  for (int i = 0; i < kModParamCount; ++i) p.p[i] = modulated(base.p[i], sums, modParamDestination(slot, i));
  return p;
}

}  // namespace

void PartModulation::processPart(const ModParams& params, const ModPartInputs& part, const ModVoiceInputs* voices,
                                 int n) {
  const ModVoiceInputs* last = part.lastVoice >= 0 && voices != nullptr ? &voices[part.lastVoice] : nullptr;
  fillFixed(partSource_, part, last);
  for (int s = 0; s < kNumModSlots; ++s) {
    const float* outs = partSlots_[size_t(s)].outputs();
    std::copy(outs, outs + kMaxModOutputs, partPrevious_[s]);
  }
  fillMods(partSource_, part.lastVoice, false);
  RouteSums sums;
  sumRoutes(params.routes, partSource_, DestinationKind::ModParam, &sums);
  const ModContext ctx = contextFor(part, part.gate, part.retrigger);
  for (int s = 0; s < kNumModSlots; ++s) {
    if (!perPart_[s]) continue;
    partSlots_[size_t(s)].process(ctx, modulatedSlot(params.slot[s], sums, s), params.shapes[s], n);
  }
  fillMods(partSource_, part.lastVoice, true);
}

void PartModulation::processVoice(int v, const ModParams& params, const ModPartInputs& part,
                                  const ModVoiceInputs& voice, const SlotParams& base, VoiceControl* control, int n) {
  float source[kNumSources];
  fillFixed(source, part, &voice);
  fillMods(source, v, false);
  RouteSums sums;
  sumRoutes(params.routes, source, DestinationKind::ModParam, &sums);
  const ModContext ctx = contextFor(part, voice.gate, voice.retrigger);
  for (int s = 0; s < kNumModSlots; ++s) {
    if (perPart_[s]) continue;
    voiceSlot(v, s).process(ctx, modulatedSlot(params.slot[s], sums, s), params.shapes[s], n);
  }
  fillMods(source, v, true);
  sumRoutes(params.routes, source, DestinationKind::Voice, &sums);

  control->params = base;
  for (int i = 0; i < kMaxSlotParams; ++i) {
    control->params.source.p[i] = modulated(base.source.p[i], sums, uint16_t(kDestSourceP + i));
    control->params.filter.p[i] = modulated(base.filter.p[i], sums, uint16_t(kDestFilterP + i));
    control->params.shaper.p[i] = modulated(base.shaper.p[i], sums, uint16_t(kDestShaperP + i));
  }
  // Pitch: +-100 % is +-24 semitones (provisional ruling 4), the sum limited to +-48.
  control->pitch = std::clamp(sums.value[kDestPitch], -2.0f, 2.0f) * kPitchRangeSemitones + voice.bend;
  control->ampRouted = sums.touched[kDestAmp];
  control->amp = clamp01(sums.value[kDestAmp]);
  control->pan = sums.touched[kDestPan] ? clamp01(0.5f + sums.value[kDestPan]) * 2.0f - 1.0f : 0.0f;
}

float PartModulation::partLevel(const ModParams& params, float base) const {
  RouteSums sums;
  sumRoutes(params.routes, partSource_, DestinationKind::Part, &sums);
  return modulated(base, sums, kDestPartLevel);
}

}  // namespace eg13
```

Add `mod/part_modulation.cpp` to `core/CMakeLists.txt` after `mod/mod_routes.cpp`.

- [ ] **Step 7: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "previous tick|own P settles|this tick's mod|Per-part slots|Per-voice slots|Pitch, Amp and Pan|Fixed sources|Part-level routes|NaN from a mod engine|engine or scope|Part modulation with"`
Expected: all pass. Then the full suite (the `SlotParams` move changes nothing else).

- [ ] **Step 8: Document scope and feedback**

Append to `docs/manual/modulation.md`:

````markdown
## Mod slots: engine and scope

Each mod slot has an **Engine** (Off, Envelope, LFO, Random / S&H, Function Generator) and a **Scope**:

- *Per voice*: every voice has its own copy of the engine. A voice starting from silence starts its copies afresh, and each new note on a voice retriggers them (an envelope restarts its attack from where it is; an LFO restarts its cycle).
- *Per part*: one copy for the whole part, which every voice hears. Its gate is held while any key of the part is down and it triggers when the first key goes down after all were up, so a per-part envelope works like a paraphonic synth's single-trigger envelope and a per-part LFO keeps running while you play.

Choosing an engine gives it its own settings, including its scope (Random / S&H starts per part, the others per voice). Each engine remembers its settings in each slot, as the Source, Filter and Shaper engines do. Changing the engine takes effect at once: the new engine starts from the beginning.

**Routes between mod slots** (for example Mod 1's LFO into Mod 2's Decay) use the value the source had 16 samples earlier. That keeps loops (Mod 1 into Mod 2 into Mod 1, or a slot into its own controls) stable and the order of the slots irrelevant. Routes into everything else use the current value.

**Part Level** and routes into a per-part slot take per-note sources (velocity, a per-voice mod slot, …) from the most recently started voice.
````

- [ ] **Step 9: Commit**

```bash
git add core/voice/slot_params.h core/voice/voice.h core/mod core/CMakeLists.txt tests/test_mod_engines.h tests/test_part_modulation.cpp docs/manual/modulation.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "PartModulation: per-voice and per-part mod slots, previous-tick feedback, voice controls"
```

---
### Task 10: Voice: Amp, pan and gains per control tick

**Files:**
- Modify: `core/voice/voice.h`, `core/voice/voice.cpp`, `docs/manual/modulation.md`, `docs/manual/parts-and-routing.md`, `docs/manual/smoothing.md`
- Test: `tests/test_voice.cpp` (extended)

**Interfaces:**
- Consumes: `VoiceControl` (Task 9, `core/mod/voice_control.h`), `clamp01`.
- Produces (`core/voice/voice.h`): `static constexpr double Voice::kAmpRampSeconds = 0.005; void Voice::beginTick(const VoiceControl& control, int length); void Voice::render(const VoiceInputs& in, float* const* out, int n); float Voice::ampLevel() const;`. The M1–M3 `process(...)` overloads keep their behaviour exactly (no Amp, pan or gains): every existing voice test stays valid, and Part keeps using them on its unmodulated path.

How the M4 path works (spec §5.2, provisional ruling 9): `beginTick()` stores the tick's controls and the starting values of the Amp, pan and gains; `render()` runs Source → (Source gain) → Filter → Shaper with the tick's parameters and pitch, then multiplies by the Amp, the pan (the part's balance law) and the Voice Gain, each moving linearly to its target by the end of the tick. Without an Amp route the Amp moves toward the gate by 1/(5 ms) per sample. A voice from silence starts with the Amp closed and its pan and gains snapped. It is freed when its gate is off and either its post-Amp output or its Shaper input (plus the Shaper latency, M3) has been below −90 dBFS for 50 ms.

- [ ] **Step 1: Write the failing tests (append to `tests/test_voice.cpp`)**

In the anonymous namespace of `tests/test_voice.cpp`, after `run(...)`, add:

```cpp
SourceSpec constant() { return {std::make_unique<test::ConstantEngine>(), 0.0, 16}; }

struct Rendered {
  std::vector<float> l, r;
};

// Renders `ticks` control ticks of 16 samples through the M4 path with the same controls.
Rendered renderTicks(Voice& voice, const VoiceControl& c, int ticks) {
  Rendered out{std::vector<float>(size_t(ticks) * 16, 0.0f), std::vector<float>(size_t(ticks) * 16, 0.0f)};
  for (int t = 0; t < ticks; ++t) {
    voice.beginTick(c, 16);
    float* o[2] = {out.l.data() + t * 16, out.r.data() + t * 16};
    voice.render(VoiceInputs{}, o, 16);
  }
  return out;
}

```

Append at the end of the file:

```cpp
TEST_CASE("Without an Amp route the Amp follows the gate with 5 ms ramps") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  const VoiceControl c;  // not routed, centre, unity gains
  const Rendered on = renderTicks(voice, c, 30);  // 10 ms
  for (int i = 0; i < 240; ++i) REQUIRE(std::fabs(on.l[size_t(i)] - 0.5f * float(i + 1) / 240.0f) < 1e-5f);
  REQUIRE(on.l[300] == 0.5f);
  REQUIRE(voice.ampLevel() == 1.0f);
  voice.release();
  const Rendered off = renderTicks(voice, c, 30);
  for (int i = 0; i < 240; ++i) REQUIRE(std::fabs(off.l[size_t(i)] - 0.5f * (1.0f - float(i + 1) / 240.0f)) < 1e-5f);
  REQUIRE(off.l[300] == 0.0f);
}

TEST_CASE("A routed Amp follows its level, moving across each tick") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 0.5f;
  Rendered out = renderTicks(voice, c, 1);
  for (int i = 0; i < 16; ++i) REQUIRE(std::fabs(out.l[size_t(i)] - 0.5f * 0.5f * float(i + 1) / 16.0f) < 1e-6f);
  c.amp = 1.5f;  // clamped
  out = renderTicks(voice, c, 1);
  REQUIRE(out.l[15] == 0.5f);
  c.amp = 0.0f;  // routed at zero: silent even with the gate held
  out = renderTicks(voice, c, 2);
  REQUIRE(out.l[31] == 0.0f);
}

TEST_CASE("Pan, Voice Gain and Source gain shape the voice") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  c.pan = 1.0f;          // right
  c.gain = 0.25f;        // -12 dB
  c.sourceGain = 2.0f;   // before the (Off) Filter
  const Rendered out = renderTicks(voice, c, 4);
  // First tick: pan and gains snap (a note's first tick from silence); the Amp opens across it.
  REQUIRE(out.l[15] == 0.0f);
  REQUIRE(std::fabs(out.r[15] - 0.5f * 2.0f * 0.25f) < 1e-6f);
  REQUIRE(std::fabs(out.r[63] - 0.25f) < 1e-6f);
  REQUIRE(out.l[63] == 0.0f);
}

TEST_CASE("Pan and gains glide across a tick after the first") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.ampRouted = true;
  c.amp = 1.0f;
  renderTicks(voice, c, 2);
  c.gain = 0.5f;
  const Rendered out = renderTicks(voice, c, 1);
  for (int i = 0; i < 16; ++i) {
    const float gain = 1.0f + (0.5f - 1.0f) * float(i + 1) / 16.0f;
    REQUIRE(std::fabs(out.l[size_t(i)] - 0.5f * gain) < 1e-6f);
  }
}

TEST_CASE("The pitch control transposes the voice") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  voice.trigger(57, 1.0f);  // A3, 220 Hz
  VoiceControl c;
  c.pitch = 12.0f;
  const Rendered out = renderTicks(voice, c, 3000);
  REQUIRE(std::fabs(test::estimateFrequency(out.l, 48000.0, 4800, out.l.size()) - 440.0) < 0.5);
}

TEST_CASE("A voice is freed 50 ms after its Amp closes, even if its Source keeps sounding") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  const VoiceControl c;
  renderTicks(voice, c, 30);  // the Amp fully open
  voice.release();
  int ticks = 0;
  while (voice.active() && ticks < 1000) {
    renderTicks(voice, c, 1);
    ++ticks;
  }
  // 5 ms of release ramp, then 50 ms below -90 dBFS: 2640 samples, 165 ticks.
  REQUIRE(ticks >= 165);
  REQUIRE(ticks <= 166);
}

TEST_CASE("A voice's Amp starts closed on each note from silence") {
  Voice voice(constant());
  voice.prepare(48000.0, 256);
  voice.trigger(60, 1.0f);
  VoiceControl c;
  c.pan = -1.0f;
  renderTicks(voice, c, 400);
  voice.reset();
  voice.trigger(62, 1.0f);
  c.pan = 1.0f;
  const Rendered out = renderTicks(voice, c, 1);
  REQUIRE(out.l[0] == 0.0f);  // pan snapped right, no glide from the left
  REQUIRE(std::fabs(out.r[0] - 0.5f / 240.0f) < 1e-6f);  // the Amp opens from 0
}

TEST_CASE("Rendering the M4 path does not allocate") {
  Voice voice(sine());
  voice.prepare(48000.0, 256);
  VoiceControl c;
  std::vector<float> l(16), r(16);
  float* o[2] = {l.data(), r.data()};
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int t = 0; t < 3000; ++t) {
      if (t % 500 == 0) voice.trigger(48 + t / 500, 0.8f);
      if (t % 500 == 400) voice.release();
      c.ampRouted = (t / 250) % 2 == 0;
      c.amp = 0.7f;
      c.pan = float(t % 7) / 3.0f - 1.0f;
      c.pitch = float(t % 5);
      voice.beginTick(c, 16);
      voice.render(VoiceInputs{}, o, 7);
      voice.render(VoiceInputs{}, o, 9);
    }
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`beginTick`, `render`, `ampLevel` are not members of `Voice`).

- [ ] **Step 3: Replace `core/voice/voice.h`**

```cpp
#pragma once

#include <memory>
#include <vector>

#include "engine/native_rate_wrapper.h"
#include "mod/voice_control.h"
#include "voice/processor_slot.h"
#include "voice/slot_params.h"
#include "voice/source_factory.h"

namespace eg13 {

// What a voice reads besides its notes: the part's Input as its Input source setting routes it
// (spec §2.3), and the other part's output (soft-limited by the Synth). The Shaper's side
// signals (the Ring Mod / Vocoder Carrier and Modulator) come from either, or from the Source's
// AUX.
struct VoiceInputs {
  const float* const* input = nullptr;  // two channels, nullable: the Source's input
  const float* const* other = nullptr;  // two channels, nullable
  // The Input as the Shaper's side signals see it: mono, delayed by sourceLatencySamples() so it
  // reaches the Shaper with the voice. Part fills it from input; nullable (silence).
  const float* inputSide = nullptr;
};

// One synth voice: Source -> Filter -> Shaper -> Amp -> pan (spec §2), plus note, glide and bend
// handling and silence detection.
class Voice {
 public:
  static constexpr int kControlBlock = 16;  // pitch updates every 16 host samples
  static constexpr double kAmpRampSeconds = 0.005;  // the gate-following Amp's ramps (spec §5.2)

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

  // M4 (Part's modulated path). beginTick(): the matrix's controls for the next `length`
  // samples (the rest of the Part's 16-sample tick). render(): adds n samples (n <= what is left
  // of the tick) of Source -> Filter -> Shaper with the tick's slot parameters and pitch, the
  // Source gain before the Filter, then the Amp (routed, or following the gate with 5 ms ramps),
  // the pan and the Voice Gain. Amp, pan and gains move in straight lines across the tick; on a
  // note's first tick from silence the pan and gains start at their values (no glide).
  void beginTick(const VoiceControl& control, int length);
  void render(const VoiceInputs& in, float* const* out, int n);
  float ampLevel() const { return amp_; }  // tests: the Amp after the last sample rendered

  // M1-M3 path (tests, and parts without modulation): no Amp, pan or gains. Adds n samples into
  // out[0], out[1].
  void process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n);
  // Source parameters only (Filter and Shaper get zeros). in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& p, int n);
  void process(float* const* out, const ParamValues& p, int n) { process(nullptr, out, p, n); }

  bool active() const { return active_; }
  bool gate() const { return gate_; }
  float currentNote() const { return float(currentNote_); }
  int latencySamples() const { return latency_; }  // Source + Filter + Shaper
  int sourceLatencySamples() const { return sourceLatency_; }  // the Source path, as aligned

 private:
  // The stages of the chain, as bits: which ones failed, or wait for the held note again.
  static constexpr unsigned kSourceStage = 1u;
  static constexpr unsigned kFilterStage = 2u;
  static constexpr unsigned kShaperStage = 4u;
  static constexpr unsigned kAllStages = kSourceStage | kFilterStage | kShaperStage;

  // The stages whose output in this control block is not finite.
  unsigned failedStages(int count) const;
  // The signals the Shaper's side inputs can select for this control block: the Source AUX,
  // the aligned mono Input, and the other part's output summed to mono (nullptr when absent).
  // The slot picks from them by each engine's own selectors.
  SideSignals sideSignals(const VoiceInputs& in, int offset, int count);
  void restart(unsigned stages);  // reset the engines playing in these stages
  // The chain for n samples; c == nullptr is the M1-M3 path.
  void run(const VoiceInputs& in, float* const* out, const SlotParams& p, const VoiceControl* c, int n);
  float tickPosition(int sample) const;  // 0..1 across the tick, after `sample` more samples
  void noteOn(unsigned stages);   // the held note, at the current pitch and velocity
  NativeRateWrapper& source() { return *sources_[size_t(current_)]; }

  std::vector<std::unique_ptr<NativeRateWrapper>> sources_;
  ProcessorSlot filter_;
  ProcessorSlot shaper_;
  int current_ = 0;
  int sourceLatency_ = 0;
  int latency_ = 0;
  double hostRate_ = 48000.0;
  float targetNote_ = 60.0f;
  double currentNote_ = 60.0;  // double: float stalls short of the target on slow glides
  float pitchOffset_ = 0.0f;
  float glideSeconds_ = 0.0f;
  bool gate_ = false;
  unsigned retrigger_ = 0;  // stages to re-send the held note to after a NaN reset
  float velocity_ = 1.0f;
  bool active_ = false;
  int silentSamples_ = 0;  // the Shaper's input silent since the release
  int outSilent_ = 0;      // the output after the Amp silent since the release (M4 path)
  int silenceLimit_ = 2400;
  // The M4 path's tick: controls, position, and the Amp, pan and gains at its start and now.
  VoiceControl control_;
  int tickLength_ = kControlBlock;
  int tickPos_ = 0;
  bool fresh_ = true;  // the next tick is the note's first from silence: snap pan and gains
  float ampStep_ = 1.0f / 240.0f;
  float amp_ = 0.0f;
  float ampFrom_ = 0.0f;
  float pan_ = 0.0f;
  float panFrom_ = 0.0f;
  float gain_ = 1.0f;
  float gainFrom_ = 1.0f;
  float sourceGain_ = 1.0f;
  float sourceGainFrom_ = 1.0f;
  float source_[3][kControlBlock] = {};  // Source L, R, AUX
  float filtered_[2][kControlBlock] = {};
  float side_[kControlBlock] = {};       // mono other part
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

bool finite(const float* a, int count) {
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(a[i])) return false;
  }
  return true;
}

// (L + R) / 2 of stereo[.][offset..offset + count) into mono; nullptr when stereo is.
const float* monoOf(const float* const* stereo, int offset, int count, float* mono) {
  if (stereo == nullptr) return nullptr;
  for (int i = 0; i < count; ++i) mono[i] = 0.5f * (stereo[0][offset + i] + stereo[1][offset + i]);
  return mono;
}
}  // namespace

Voice::Voice(SourceSpec source) : Voice(only(std::move(source))) {}

Voice::Voice(std::vector<SourceSpec> sources) : Voice(std::move(sources), makeOffOnly(), makeOffOnly()) {}

Voice::Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters, std::vector<SourceSpec> shapers)
    : filter_(std::move(filters)), shaper_(std::move(shapers)) {
  for (auto& s : sources) {
    sources_.push_back(std::make_unique<NativeRateWrapper>(std::move(s.engine), s.nativeRate, s.nativeBlock));
    sources_.back()->setAuxEnabled(true);  // Source AUX, for the Ring Mod Carrier and Modulator (spec §4)
  }
  assert(!sources_.empty() && "a voice needs at least one source engine");
}

void Voice::prepare(double hostRate, int maxBlock, Alignment alignment) {
  hostRate_ = hostRate;
  silenceLimit_ = int(hostRate * kSilenceSeconds);
  ampStep_ = float(1.0 / std::max(1.0, hostRate * kAmpRampSeconds));
  auto path = [alignment](const NativeRateWrapper& s) {
    return alignment == Alignment::Input && s.info().usesInput ? s.inputLatencySamples()
                                                               : s.outputLatencySamples();
  };
  sourceLatency_ = 0;
  for (auto& s : sources_) {
    s->prepare(hostRate, maxBlock);
    sourceLatency_ = std::max(sourceLatency_, path(*s));
  }
  for (auto& s : sources_) s->setExtraDelay(sourceLatency_ - path(*s));
  // The Filter and Shaper see one control block per call.
  filter_.prepare(hostRate, kControlBlock);
  shaper_.prepare(hostRate, kControlBlock);
  // Source AUX goes to the Shaper straight from the Source: it is aligned with the voice only
  // while no Filter adds latency.
  assert(filter_.latencySamples() == 0 && "delay Source AUX by the Filter latency");
  latency_ = sourceLatency_ + filter_.latencySamples() + shaper_.latencySamples();
  reset();
}

void Voice::reset() {
  for (auto& s : sources_) s->reset();
  filter_.reset();
  shaper_.reset();
  gate_ = false;
  retrigger_ = 0;
  active_ = false;
  silentSamples_ = 0;
  outSilent_ = 0;
  control_ = VoiceControl{};
  tickLength_ = kControlBlock;
  tickPos_ = 0;
  fresh_ = true;
  amp_ = ampFrom_ = 0.0f;
  pan_ = panFrom_ = 0.0f;
  gain_ = gainFrom_ = 1.0f;
  sourceGain_ = sourceGainFrom_ = 1.0f;
}

void Voice::restart(unsigned stages) {
  if (stages & kSourceStage) source().reset();
  if (stages & kFilterStage) filter_.restart();
  if (stages & kShaperStage) shaper_.restart();
}

void Voice::noteOn(unsigned stages) {
  const float note = float(currentNote_) + pitchOffset_;
  if (stages & kSourceStage) source().noteOn(note, velocity_);
  if (stages & kFilterStage) filter_.noteOn(note, velocity_);
  if (stages & kShaperStage) shaper_.noteOn(note, velocity_);
}

void Voice::setEngine(int engine) {
  engine = std::clamp(engine, 0, int(sources_.size()) - 1);
  if (engine == current_) return;
  // Hard cut: the old engine is left as is (it is reset when selected again or by
  // reset()), and a held note restarts after latencySamples() of silence.
  current_ = engine;
  source().reset();
  retrigger_ &= ~kSourceStage;
  silentSamples_ = 0;
  if (gate_) {
    noteOn(kSourceStage);
  } else {
    active_ = false;
  }
}

void Voice::setFilterEngine(int engine) { filter_.setEngine(engine); }

void Voice::setShaperEngine(int engine) { shaper_.setEngine(engine); }

void Voice::trigger(int key, float velocity) {
  targetNote_ = float(key);
  velocity_ = velocity;
  retrigger_ = 0;
  if (!active_) {
    restart(kAllStages);  // drop stale converter samples and filter state from the previous note
    currentNote_ = double(targetNote_);
    amp_ = 0.0f;   // the Amp opens from silence
    fresh_ = true;  // the first tick snaps pan and gains
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = double(targetNote_);
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
  outSilent_ = 0;
  noteOn(kAllStages);
}

void Voice::glideTo(int key) {
  targetNote_ = float(key);
  if (glideSeconds_ <= 0.0f) currentNote_ = double(targetNote_);
}

unsigned Voice::failedStages(int count) const {
  unsigned failed = 0;
  if (!finite(source_[0], count) || !finite(source_[1], count) || !finite(source_[2], count)) {
    failed |= kSourceStage;
  }
  // Checked on its own: the Ring Mod reads a NaN input as 0, so a broken Filter can hide
  // behind a finite Shaper output.
  if (!finite(filtered_[0], count) || !finite(filtered_[1], count)) failed |= kFilterStage;
  if (!finite(buffer_[0], count) || !finite(buffer_[1], count)) failed |= kShaperStage;
  return failed;
}

void Voice::release() {
  gate_ = false;
  retrigger_ = 0;  // a pending NaN re-gate must not outlive the key release
  source().noteOff();
  filter_.noteOff();
  shaper_.noteOff();
}

SideSignals Voice::sideSignals(const VoiceInputs& in, int offset, int count) {
  SideSignals sides;
  sides.sourceAux = source_[2];
  sides.input = in.inputSide ? in.inputSide + offset : nullptr;
  sides.otherPart = monoOf(in.other, offset, count, side_);
  return sides;
}

void Voice::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  SlotParams params;
  params.source = p;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Voice::process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n) {
  run(in, out, p, nullptr, n);
}

void Voice::beginTick(const VoiceControl& control, int length) {
  control_ = control;
  tickLength_ = std::max(1, length);
  tickPos_ = 0;
  if (fresh_) {  // a note's first tick from silence: no glide from the previous note's values
    pan_ = control.pan;
    gain_ = control.gain;
    sourceGain_ = control.sourceGain;
    fresh_ = false;
  }
  ampFrom_ = amp_;
  panFrom_ = pan_;
  gainFrom_ = gain_;
  sourceGainFrom_ = sourceGain_;
}

void Voice::render(const VoiceInputs& in, float* const* out, int n) { run(in, out, control_.params, &control_, n); }

float Voice::tickPosition(int sample) const {
  return std::min(1.0f, float(tickPos_ + sample) / float(tickLength_));
}

void Voice::run(const VoiceInputs& in, float* const* out, const SlotParams& p, const VoiceControl* c, int n) {
  if (!active_) return;
  if (retrigger_ != 0 && gate_) {  // the held note, to the stages a NaN reset (restart)
    noteOn(retrigger_);
    retrigger_ = 0;
  }
  for (int done = 0; done < n;) {
    const int count = std::min(kControlBlock, n - done);
    const double coeff =
        glideSeconds_ > 0.0f
            ? std::exp(-double(count) / (double(glideSeconds_) * hostRate_))
            : 0.0;
    currentNote_ = double(targetNote_) + (currentNote_ - double(targetNote_)) * coeff;
    if (std::fabs(currentNote_ - double(targetNote_)) < 1e-4) currentNote_ = double(targetNote_);
    const float note = float(currentNote_) + pitchOffset_ + (c != nullptr ? c->pitch : 0.0f);
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
    if (c != nullptr) {  // the Source's trim and chord scaling, before the Filter (M4 gain staging)
      for (int i = 0; i < count; ++i) {
        const float g = sourceGainFrom_ + (c->sourceGain - sourceGainFrom_) * tickPosition(i + 1);
        source_[0][i] *= g;
        source_[1][i] *= g;
        source_[2][i] *= g;
      }
    }
    const float* filterIn[2] = {source_[0], source_[1]};
    float* filterOut[2] = {filtered_[0], filtered_[1]};
    filter_.process(filterIn, nullptr, filterOut, p.filter, count);
    const float* shaperIn[2] = {filtered_[0], filtered_[1]};
    float* shaperOut[2] = {buffer_[0], buffer_[1]};
    const SideSignals sides = sideSignals(in, done, count);
    shaper_.process(shaperIn, &sides, shaperOut, p.shaper, count);

    if (const unsigned failed = failedStages(count)) {
      // Spec §9: a NaN/Inf from an engine resets that engine (every stage whose output is not
      // finite) and mutes the voice for this block; a held note starts again on the next call.
      restart(failed);
      retrigger_ = gate_ ? failed : 0;
      active_ = gate_;
      if (c != nullptr) tickPos_ += count;
      return;
    }
    float peak = 0.0f;
    float outPeak = 0.0f;
    for (int i = 0; i < count; ++i) {
      float l = buffer_[0][i];
      float r = buffer_[1][i];
      if (c != nullptr) {
        // The Amp (spec §5.2): the routes' level, or the gate with 5 ms ramps; then the pan
        // (the part's balance law) and the Voice Gain, the Amp's gain.
        const float t = tickPosition(i + 1);
        if (c->ampRouted) {
          amp_ = ampFrom_ + (clamp01(c->amp) - ampFrom_) * t;
        } else {
          const float target = gate_ ? 1.0f : 0.0f;
          amp_ = target > amp_ ? std::min(target, amp_ + ampStep_) : std::max(target, amp_ - ampStep_);
        }
        pan_ = panFrom_ + (c->pan - panFrom_) * t;
        gain_ = gainFrom_ + (c->gain - gainFrom_) * t;
        const float g = amp_ * gain_;
        l *= g * std::min(1.0f, 1.0f - pan_);
        r *= g * std::min(1.0f, 1.0f + pan_);
        outPeak = std::max(outPeak, std::max(std::fabs(l), std::fabs(r)));
      }
      out[0][done + i] += l;
      out[1][done + i] += r;
      peak = std::max(peak, std::max(std::fabs(filtered_[0][i]), std::fabs(filtered_[1][i])));
    }
    if (c != nullptr) {
      sourceGain_ = sourceGainFrom_ + (c->sourceGain - sourceGainFrom_) * tickPosition(count);
      tickPos_ += count;
    }
    done += count;

    // Silence is measured at the Shaper input: the Ring Mod's internal oscillator keeps the
    // Shaper's output alive after the note has died. The Shaper latency lets its output flush.
    // With the Amp (M4), a silent output frees the voice too (spec §5.2).
    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
    } else {
      silentSamples_ = 0;
    }
    if (c != nullptr && !gate_ && outPeak < kSilenceThreshold) {
      outSilent_ += count;
    } else {
      outSilent_ = 0;
    }
    if (silentSamples_ >= silenceLimit_ + shaper_.latencySamples() || (c != nullptr && outSilent_ >= silenceLimit_)) {
      active_ = false;
      return;
    }
  }
}

}  // namespace eg13
```

What changed against M3's `voice.cpp`: `prepare` sets `ampStep_`; `reset` clears the M4 tick state; `trigger` from silence closes the Amp and marks the note fresh; `process(VoiceInputs, ..., SlotParams, n)` now calls the shared `run(..., nullptr, n)`, which is M3's loop plus the `c != nullptr` branches (pitch, Source gain, Amp/pan/gain, post-Amp silence); `beginTick`, `render` and `tickPosition` are new. With `c == nullptr` the loop does exactly what M3's did.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "Amp|Pan and|pitch control|freed 50 ms|M4 path"`
Expected: the eight new tests pass. Then the full suite: every existing voice, part and synth test passes unchanged (a planning build of this `voice.cpp` passed all 30 existing voice tests).

- [ ] **Step 6: Document the Amp**

Append to `docs/manual/modulation.md`:

````markdown
## Amp and voices

Every voice ends with an **Amp** and a **pan**, after the Shaper.

- If no route goes to *Amp*, the Amp simply follows the key: it opens over 5 ms when the note starts and closes over 5 ms when you let go. That cuts a sound that would otherwise ring on after the key (a struck Resonator, a Macro Oscillator drum): route an envelope to the Amp to shape the release.
- If any route goes to *Amp*, the Amp's level is the sum of those routes (0 to 100 %). The init patch routes Mod 1's envelope to the Amp, scaled by velocity.
- A route to *Pan* moves the voice from the centre (−50 % fully left, +50 % fully right), using the part's own pan law (the centre keeps full level on both sides).
- A voice stops, and is free for a new note, when its note is off and its sound after the Amp has been silent (below −90 dBFS) for 50 ms. It also stops when the sound reaching its Shaper has been silent that long, as before, so a Ring Mod / Vocoder whose internal oscillator would sound forever does not hold the voice.
- In an *Always on* part the Amp stays open (its voice is always on). Its per-voice envelopes and LFOs still follow the keys you hold: the first key down triggers them, so an envelope routed to the Amp turns an always-on drone into a keyed sound.
````

In `docs/manual/parts-and-routing.md`, replace the paragraph that begins "A voice stops (and becomes free for a new note) when its note is released" with:

```markdown
A voice stops (and becomes free for a new note) when its note is released and either its sound after the Amp, or the sound reaching its Shaper, has been silent (below −90 dBFS) for 50 ms (the second check waits for the Shaper's latency too). The second check exists because the Ring Mod / Vocoder's internal oscillator keeps sounding after the note has died. See [Modulation](modulation.md) for the Amp.
```

In `docs/manual/smoothing.md`, insert before "After a reset":

```markdown
- **Modulation:** EG13 works out the routes every 16 samples (a third of a millisecond at 48 kHz) and moves the Amp, the pan and the gains in a straight line across those 16 samples, so modulating them does not zipper. Routes into an engine's controls reach that engine every 16 samples and are smoothed as listed above for that engine. Without an Amp route the Amp opens and closes over 5 ms.
```

and change "After a reset, or when a voice starts from silence, every control starts at its current value with no glide (except inside the Ring Mod / Vocoder, as noted above)." to:

```markdown
After a reset, or when a voice starts from silence, every control starts at its current value with no glide (except inside the Ring Mod / Vocoder, as noted above); the Amp starts closed and opens as described in [Modulation](modulation.md).
```

- [ ] **Step 7: Commit**

```bash
git add core/voice/voice.h core/voice/voice.cpp tests/test_voice.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Voice: Amp (routed or following the gate), pan and gains per control tick"
```

---
### Task 11: Part: the modulation grid, gates, expressions and part level

**Files:**
- Modify: `core/part/part.h`, `core/part/part.cpp`, `docs/manual/parts-and-routing.md`
- Test: `tests/test_part.cpp` (extended)

**Interfaces:**
- Consumes: `PartModulation`, `ModParams`, `ModVoiceInputs`, `ModPartInputs`, `VoiceControl` (Task 9); `Voice::beginTick`, `Voice::render` (Task 10); `makeAllModEngines`, `ModEngineFactory` (Task 7); `test::testModEngines`, `test::kTestStep` (Task 9).
- Produces (`core/part/part.h`):
  - `constexpr float kDefaultReleaseVelocity = 64.0f / 127.0f;`
  - `PartSettings` gains `int modEngine[4]; bool modPerPart[4]; int mpe; int mpeBendRange = 48; int mpeMaster = 0; int mpeFirst = 1; int mpeLast = 15;`
  - `enum class Expression { Bend, Pressure, Slide }; enum class PartController { ModWheel, Slide, ChannelPressure, CcA, CcB, CcC, CcD };`
  - `struct ModTiming { double beat; double beatsPerSample; bool jump; double barPhase; double beatsPerBar; }; struct PartParams { SlotParams slots; ModParams mod; ModTiming timing; float voiceGain = 1; };`
  - `Part(ModEngineFactory = makeAllModEngines); Part(SourceFactory, ModEngineFactory = makeAllModEngines); Part(SlotFactory, SlotFactory, SlotFactory, ModEngineFactory = makeAllModEngines);`
  - `void setSeedBase(uint32_t); bool isMemberChannel(int) const; void noteOn(int key, float velocity, int channel = 0); void noteOff(int key, float releaseVelocity = kDefaultReleaseVelocity); void setController(PartController, float); void memberExpression(int channel, Expression, float); void noteExpression(int channel, int key, Expression, float); void process(const VoiceInputs&, float* const* out, const PartParams&, int n); const VoiceControl& lastControl(int v) const; const PartModulation& modulation() const;`
  - The M1–M3 `process(..., const SlotParams&, n)` and the Source-only overloads keep their behaviour.

The grid (provisional ruling 3): `tickPos_` counts samples into a 16-sample tick that runs across `process()` calls. At each tick start the Part updates the part gate, runs the part-scope slots, sets the part level from the matrix and computes every sounding voice's `VoiceControl` for 16 samples; a voice that starts mid-tick gets controls for the rest of the tick at once. Mod engines therefore always advance in whole ticks of host time, and the Amp, pan and gains ramp per sample from the tick start, so the result does not depend on the host's block sizes (test "Modulation does not depend on host block slicing", which a planning build passed at block sizes 1, 7, 16, 100 and 512).

Note expressions (provisional ruling 11): an MPE member channel's bend, pressure and slide are stored per channel and given to every voice on that channel, including notes that start later (MPE controllers send them before the note-on). `noteExpression` addresses the voices playing one key.

- [ ] **Step 1: Write the failing tests (extend `tests/test_part.cpp`)**

Add `#include "engine/tides_engine.h"` and `#include "test_mod_engines.h"` to the includes. In the anonymous namespace, after `preparedSinePart()`, add:

```cpp
SourceSpec steady() { return {std::make_unique<test::ConstantEngine>(), 0.0, 16}; }

// Renders n samples through the modulated path in host blocks of `block`.
Stereo runModulated(Part& part, int n, const PartParams& params, int block = kBlock) {
  Stereo s{std::vector<float>(size_t(n)), std::vector<float>(size_t(n))};
  for (int done = 0; done < n;) {
    const int count = std::min(block, n - done);
    float* out[2] = {s.l.data() + done, s.r.data() + done};
    part.process(VoiceInputs{}, out, params, count);
    done += count;
  }
  return s;
}

void setRoute(PartParams& params, int r, uint16_t source, uint16_t destination, float amount,
              uint16_t via = kSrcNone) {
  params.mod.routes.route[r] = {source, destination, via};
  params.mod.routes.amount[r] = amount;
}

PartSettings withMod(int slot, int engine, bool perPart = false) {
  PartSettings s;
  s.modEngine[slot] = engine;
  s.modPerPart[slot] = perPart;
  return s;
}

ParamValues engineDefaults(ModEngineKind kind) { return test::defaultParams(modEngineInfo(int(kind)).engine); }

```

Append at the end of the file:

```cpp
TEST_CASE("Without Amp routes a modulated part follows the gate, closing 5 ms after release") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  const PartParams params;
  part.noteOn(60, 1.0f);
  Stereo held = runModulated(part, 4800, params);
  REQUIRE(held.l[4799] == 0.5f);
  part.noteOff(60);
  Stereo released = runModulated(part, 480, params);
  REQUIRE(released.l[100] > 0.0f);
  REQUIRE(released.l[300] == 0.0f);
}

TEST_CASE("An Envelope routed to the Amp shapes the part's notes, scaled by velocity") {
  Part part(steady);
  part.prepare(kRate, kBlock);
  part.applySettings(withMod(0, int(ModEngineKind::Envelope)));
  PartParams params;
  params.mod.slot[0] = engineDefaults(ModEngineKind::Envelope);  // 1 ms attack, full sustain, 0.5 s release
  setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
  part.noteOn(60, 0.5f);
  const Stereo held = runModulated(part, 9600, params);
  REQUIRE(std::fabs(held.l[9599] - 0.5f * 0.5f) < 1e-4f);
  part.noteOff(60);
  const Stereo released = runModulated(part, 28800, params);
  REQUIRE(released.l[4800] < 0.25f);   // releasing over about half a second
  REQUIRE(released.l[4800] > 0.01f);
  REQUIRE(released.l[28799] < 1e-4f);
}

TEST_CASE("Modulation does not depend on host block slicing") {
  auto render = [](int block) {
    Part part(sine);
    part.setSeedBase(7);
    part.prepare(kRate, 512);
    PartSettings s;
    s.modEngine[0] = int(ModEngineKind::Envelope);
    s.modEngine[1] = int(ModEngineKind::Lfo);
    part.applySettings(s);
    PartParams params;
    params.mod.slot[0] = engineDefaults(ModEngineKind::Envelope);
    params.mod.slot[0].p[3] = 0.3f;  // decay
    params.mod.slot[0].p[5] = 0.4f;  // sustain
    params.mod.slot[1] = engineDefaults(ModEngineKind::Lfo);
    params.mod.slot[1].p[2] = TidesEngine::frequencyFor(6.0, TidesEngine::Medium);
    setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
    setRoute(params, 1, modOutputSource(1, 0), kDestPitch, 0.05f);
    setRoute(params, 2, modOutputSource(1, 1), kDestPan, 0.3f);
    Stereo out{std::vector<float>(24000), std::vector<float>(24000)};
    auto process = [&](int from, int to) {
      for (int done = from; done < to;) {
        const int count = std::min(block, to - done);
        float* o[2] = {out.l.data() + done, out.r.data() + done};
        part.process(VoiceInputs{}, o, params, count);
        done += count;
      }
    };
    process(0, 1001);
    part.noteOn(57, 0.9f);
    process(1001, 12000);
    part.noteOff(57);
    process(12000, 24000);
    return out;
  };
  const Stereo a = render(512);
  for (int block : {1, 7, 16, 100}) {
    const Stereo b = render(block);
    INFO("block " << block);
    REQUIRE(a.l == b.l);
    REQUIRE(a.r == b.r);
  }
}

TEST_CASE("Per-voice mod engines retrigger on each note, per-part ones on the first key") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.modEngine[0] = test::kTestStep;
  s.modEngine[1] = test::kTestStep;
  s.modPerPart[1] = true;
  part.applySettings(s);
  const PartParams params;
  part.noteOn(60, 1.0f);
  runModulated(part, 64, params);
  part.noteOn(64, 1.0f);
  runModulated(part, 64, params);
  part.noteOff(60);
  part.noteOff(64);
  runModulated(part, 64, params);
  part.noteOn(67, 1.0f);  // after every key was up
  runModulated(part, 64, params);
  const PartModulation& m = part.modulation();
  REQUIRE(m.outputs(0, 0)[2] == 0.1f);  // voice 0: one note
  REQUIRE(m.outputs(0, 1)[2] == 0.1f);  // voice 1: one note
  REQUIRE(std::fabs(m.outputs(1, -1)[2] - 0.2f) < 1e-6f);  // the part: first keys twice
  REQUIRE(m.outputs(1, -1)[1] == 1.0f);  // part gate high
}

TEST_CASE("Always on: the Amp stays open, per-voice mod engines follow the held keys") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s = withMod(0, test::kTestStep);
  s.alwaysOn = true;
  part.applySettings(s);
  const PartParams params;
  Stereo out = runModulated(part, 960, params);
  REQUIRE(out.l[959] == 0.5f);                       // sounding with no MIDI
  REQUIRE(part.modulation().outputs(0, 0)[1] == 0.0f);  // no key: gate low
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  runModulated(part, 64, params);
  REQUIRE(part.modulation().outputs(0, 0)[1] == 1.0f);
  REQUIRE(part.modulation().outputs(0, 0)[2] == 0.1f);  // one trigger for the chord
  part.noteOff(60);
  part.noteOff(64);
  out = runModulated(part, 64, params);
  REQUIRE(part.modulation().outputs(0, 0)[1] == 0.0f);
  REQUIRE(out.l[63] == 0.5f);  // the Amp is still open
}

TEST_CASE("MPE member expression reaches only its channel's voices, and earlier messages count") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartSettings s;
  s.mpe = 1;  // lower zone: master channel 1 (0), members 2..16 (1..15)
  s.mpeMaster = 0;
  s.mpeFirst = 1;
  s.mpeLast = 15;
  part.applySettings(s);
  PartParams params;
  setRoute(params, 0, kSrcPressure, uint16_t(kDestSourceP + 0), 1.0f);
  part.memberExpression(3, Expression::Pressure, 0.7f);  // before the note on channel 4
  part.noteOn(60, 1.0f, 1);
  part.noteOn(64, 1.0f, 3);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(part.lastControl(1).params.source.p[0] == 0.7f);
  part.memberExpression(1, Expression::Bend, 2.0f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).pitch == 2.0f);
  REQUIRE(part.lastControl(1).pitch == 0.0f);
  REQUIRE(part.acceptsChannel(0));
  REQUIRE(part.isMemberChannel(5));
  REQUIRE_FALSE(part.isMemberChannel(0));
}

TEST_CASE("Polyphonic pressure and release velocity reach their own voice") {
  Part part(sine, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartParams params;
  setRoute(params, 0, kSrcPressure, uint16_t(kDestSourceP + 0), 1.0f);
  setRoute(params, 1, kSrcReleaseVelocity, uint16_t(kDestSourceP + 1), 1.0f);
  part.noteOn(60, 1.0f);
  part.noteOn(64, 1.0f);
  part.noteExpression(-1, 64, Expression::Pressure, 0.5f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(part.lastControl(1).params.source.p[0] == 0.5f);
  REQUIRE(part.lastControl(0).params.source.p[1] == 0.0f);  // 0 until released
  part.noteOff(60, 0.3f);
  runModulated(part, 32, params);
  REQUIRE(part.lastControl(0).params.source.p[1] == 0.3f);
}

TEST_CASE("A route into Part Level moves the part's level") {
  Part part(steady, test::testModEngines);
  part.prepare(kRate, kBlock);
  PartParams params;
  setRoute(params, 0, kSrcVelocity, kDestPartLevel, -0.5f);
  part.noteOn(60, 1.0f);
  const Stereo out = runModulated(part, 9600, params);
  REQUIRE(std::fabs(out.l[9599] - 0.5f * 0.5f) < 1e-3f);
}

TEST_CASE("A modulated part does not allocate") {
  Part part;
  part.setSeedBase(3);
  part.prepare(kRate, kBlock);
  PartSettings s;
  for (int slot = 0; slot < kNumModSlots; ++slot) s.modEngine[slot] = 1 + slot;
  part.applySettings(s);
  PartParams params;
  for (int slot = 0; slot < kNumModSlots; ++slot) params.mod.slot[slot] = engineDefaults(ModEngineKind(1 + slot));
  setRoute(params, 0, modOutputSource(0, 0), kDestAmp, 1.0f, kSrcVelocity);
  setRoute(params, 1, modOutputSource(1, 0), kDestPitch, 0.02f, kSrcModWheel);
  setRoute(params, 2, modOutputSource(2, 3), uint16_t(kDestSourceP + 2), 0.3f);
  setRoute(params, 3, modOutputSource(3, 0), modParamDestination(1, 2), 0.2f);
  std::vector<float> l(kBlock), r(kBlock);
  float* out[2] = {l.data(), r.data()};
  part.process(VoiceInputs{}, out, params, kBlock);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int b = 0; b < 300; ++b) {
      if (b % 40 == 0) {
        for (int k = 0; k < 4; ++k) part.noteOn(48 + 7 * k + b % 5, 0.8f, k);
      }
      if (b % 40 == 30) part.allNotesOff();
      if (b == 100) part.memberExpression(1, Expression::Bend, 1.0f);
      if (b == 150) part.setController(PartController::ModWheel, 0.7f);
      params.timing.beat += 0.1;
      part.process(VoiceInputs{}, out, params, 1 + b % kBlock);
    }
    part.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`PartParams`, `Part(SourceFactory, ModEngineFactory)`, `memberExpression`, … not declared).

- [ ] **Step 3: Replace `core/part/part.h`**

```cpp
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "mod/part_modulation.h"
#include "util/smoothed_value.h"
#include "voice/mono_note_stack.h"
#include "voice/source_factory.h"
#include "voice/voice.h"
#include "voice/voice_allocator.h"

namespace eg13 {

// MIDI's release velocity when a note-off carries none (a note-on with velocity 0): 64.
constexpr float kDefaultReleaseVelocity = 64.0f / 127.0f;

struct PartSettings {
  float level = 1.0f;
  float pan = 0.0f;
  float glide = 0.0f;  // normalized; see glideSeconds()
  int transpose = 0;
  int baseNote = 60;
  int midiChannel = 0;  // 0 = Omni, 1..16
  int keyLow = 0;
  int keyHigh = 127;
  VoiceMode voiceMode = VoiceMode::Poly;
  int voiceCount = 8;
  bool alwaysOn = false;
  int bendRange = 2;
  bool mono = false;
  int output = 0;  // 0 = main, 1 = Bus 2, 2 = other part only
  int engine = 0;  // SourceEngine
  int filterEngine = 0;  // FilterEngine
  int shaperEngine = 0;  // ShaperEngine
  int inputSource = 0;  // 0 external, 1 other part, 2 both (routed by the Synth)
  int modEngine[kNumModSlots] = {};      // ModEngineKind per mod slot
  bool modPerPart[kNumModSlots] = {};    // scope per mod slot
  int mpe = 0;              // 0 off, 1 lower zone, 2 upper zone (the Synth works out the channels)
  int mpeBendRange = 48;    // semitones of a member channel's full pitch bend
  int mpeMaster = 0;        // MPE: the zone's master channel (0..15)
  int mpeFirst = 1;         // MPE: the zone's member channels, first..last (0..15)
  int mpeLast = 15;
};

// Per-note expression (MPE member channels, polyphonic aftertouch, CLAP note expressions).
enum class Expression { Bend, Pressure, Slide };  // Bend in semitones; Pressure and Slide 0..1
// Part-wide controllers the matrix reads.
enum class PartController { ModWheel, Slide, ChannelPressure, CcA, CcB, CcC, CcD };

// Where the part is in musical time at the start of a process() call (the Synth's beat clock).
struct ModTiming {
  double beat = 0.0;
  double beatsPerSample = 2.0 / 48000.0;  // 120 BPM at 48 kHz
  bool jump = false;  // the transport started or jumped
  double barPhase = 0.0;
  double beatsPerBar = 4.0;
};

// Everything one process() call of a modulated part reads besides audio.
struct PartParams {
  SlotParams slots;     // host values of the Source, Filter and Shaper P's
  ModParams mod;        // mod slot P's, routes and amounts, envelope shapes
  ModTiming timing;
  float voiceGain = 1.0f;  // the master Voice Gain (spec §5.2's Amp gain)
};

// One part: 8 voices, their allocator, the part's modulation and the part's settings.
class Part {
 public:
  static constexpr int kVoices = VoiceAllocator::kMaxVoices;
  static constexpr int kChannels = 16;

  using SlotFactory = std::vector<SourceSpec> (*)();

  explicit Part(ModEngineFactory mods = makeAllModEngines);  // every engine of every slot
  explicit Part(SourceFactory factory, ModEngineFactory mods = makeAllModEngines);  // tests
  Part(SlotFactory sources, SlotFactory filters, SlotFactory shapers,
       ModEngineFactory mods = makeAllModEngines);  // per voice (tests)

  void setSeedBase(uint32_t base);  // main thread, before prepare(): the part's random seeds
  void prepare(double hostRate, int maxBlock,
               Voice::Alignment alignment = Voice::Alignment::Notes);  // main thread
  void reset();
  void applySettings(const PartSettings& settings);  // once per block
  const PartSettings& settings() const { return settings_; }

  bool acceptsChannel(int channel) const;  // 0..15, negative = any
  bool acceptsKey(int key) const;
  bool isMemberChannel(int channel) const;  // an MPE member channel of this part's zone

  void noteOn(int key, float velocity, int channel = 0);
  void noteOff(int key, float releaseVelocity = kDefaultReleaseVelocity);
  void pitchBend(float value);  // -1..1
  void sustain(bool down);
  void allNotesOff();
  void setController(PartController controller, float value);  // 0..1 (ModWheel..CcD)
  // An MPE member channel's bend (semitones), pressure or slide: every voice on that channel,
  // and the next notes on it.
  void memberExpression(int channel, Expression expression, float value);
  // One note's expression: the voices playing `key` (on `channel`; negative: any channel).
  void noteExpression(int channel, int key, Expression expression, float value);

  // Modulated (M4): the part's mod slots and matrix on a fixed 16-sample grid, then each voice's
  // Amp and pan. Overwrites out[0], out[1] with n samples (n <= maxBlock).
  void process(const VoiceInputs& in, float* const* out, const PartParams& params, int n);
  // Unmodulated (M1-M3 tests): no mod slots, Amp, pan or gains.
  void process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n);
  // Source parameters only; in: the part's Input (nullable).
  void process(const float* const* in, float* const* out, const ParamValues& source, int n);
  void process(float* const* out, const ParamValues& source, int n) {
    process(nullptr, out, source, n);
  }

  int activeVoiceCount() const;
  int latencySamples() const { return voices_[0]->latencySamples(); }
  const VoiceControl& lastControl(int voice) const { return controls_[voice]; }  // tests
  const PartModulation& modulation() const { return modulation_; }               // tests

 private:
  void perform(const VoiceActions& actions);
  void startNote(int v, int key, float velocity);  // a voice's new note: its mod sources
  void delayInputSide(const float* const* input, int n);  // fills inputSide_[0..n)
  void updatePitchOffset();
  void updateGains(float level);
  ModVoiceInputs voiceInputs(int v) const;
  ModPartInputs partInputs(const PartParams& params, int offset) const;
  void startTick(const PartParams& params, int offset);  // the grid: part slots, then every voice
  void controlVoice(int v, const PartParams& params, const ModPartInputs& part, int length);

  std::vector<std::unique_ptr<Voice>> voices_;
  VoiceAllocator allocator_;
  PartSettings settings_;
  MonoNoteStack alwaysOnNotes_;
  bool alwaysOn_ = false;
  float bend_ = 0.0f;
  SmoothedValue gainL_;
  SmoothedValue gainR_;
  std::vector<float> mix_[2];
  // The Input for the Shaper's side signals: mono, delayed by the voices' Source latency so
  // it reaches the Shaper with the voice (one line shared by every voice).
  std::vector<float> inputDelay_;
  int inputDelayPos_ = 0;
  std::vector<float> inputSide_;

  // Modulation (M4).
  PartModulation modulation_;
  VoiceControl controls_[kVoices];
  bool controlValid_[kVoices] = {};  // controls_[v] belongs to the current tick
  int tickPos_ = 0;                  // samples into the 16-sample grid tick
  bool pendingJump_ = false;
  // Per-voice sources.
  int voiceKey_[kVoices] = {};
  int voiceChannel_[kVoices] = {};
  float voiceVelocity_[kVoices] = {};
  float voiceRelease_[kVoices] = {};
  float voiceRandom_[kVoices] = {};
  float voicePressure_[kVoices] = {};
  float voiceSlide_[kVoices] = {};
  float voiceBend_[kVoices] = {};
  bool voiceRetrigger_[kVoices] = {};
  float keyRelease_[128] = {};
  int noteChannel_ = 0;
  int lastVoice_ = -1;
  // MPE member channels' state, for notes that start after it was sent.
  float channelBend_[kChannels] = {};
  float channelPressure_[kChannels] = {};
  float channelSlide_[kChannels] = {};
  // Part-wide controllers and gates.
  float modWheel_ = 0.0f;
  float pressure_ = 0.0f;
  float slide_ = 0.0f;
  float cc_[4] = {};
  bool partGate_ = false;
  bool partRetrigger_ = false;
  uint32_t random_ = 0x13579BDFu;
};

}  // namespace eg13
```

- [ ] **Step 4: Replace `core/part/part.cpp`**

```cpp
#include "part/part.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "synth/param_table.h"
#include "voice/processor_factory.h"

namespace eg13 {

namespace {
constexpr double kGainSmoothingSeconds = 0.01;
}  // namespace

Part::Part(ModEngineFactory mods) : Part(makeAllSources, makeAllFilters, makeAllShapers, mods) {}

Part::Part(SlotFactory sources, SlotFactory filters, SlotFactory shapers, ModEngineFactory mods)
    : modulation_(mods) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(sources(), filters(), shapers()));
}

Part::Part(SourceFactory factory, ModEngineFactory mods) : modulation_(mods) {
  for (int v = 0; v < kVoices; ++v) voices_.push_back(std::make_unique<Voice>(factory()));
}

void Part::setSeedBase(uint32_t base) {
  modulation_.setSeedBase(base);
  random_ = 0x13579BDFu ^ (base * 2654435761u);
  if (random_ == 0) random_ = 1;
}

void Part::prepare(double hostRate, int maxBlock, Voice::Alignment alignment) {
  for (auto& voice : voices_) voice->prepare(hostRate, maxBlock, alignment);
  for (auto& buffer : mix_) buffer.assign(size_t(maxBlock), 0.0f);
  int sourceLatency = 0;
  for (auto& voice : voices_) sourceLatency = std::max(sourceLatency, voice->sourceLatencySamples());
  inputDelay_.assign(size_t(sourceLatency), 0.0f);
  inputSide_.assign(size_t(maxBlock), 0.0f);
  gainL_.prepare(hostRate, kGainSmoothingSeconds);
  gainR_.prepare(hostRate, kGainSmoothingSeconds);
  modulation_.prepare(hostRate);
  reset();
}

void Part::reset() {
  for (auto& voice : voices_) voice->reset();
  std::fill(inputDelay_.begin(), inputDelay_.end(), 0.0f);
  inputDelayPos_ = 0;
  allocator_.allNotesOff();
  for (int v = 0; v < kVoices; ++v) allocator_.voiceIdle(v);
  alwaysOnNotes_.clear();
  alwaysOn_ = false;
  bend_ = 0.0f;
  modulation_.reset();
  for (auto& c : controls_) c = VoiceControl{};
  std::fill(std::begin(controlValid_), std::end(controlValid_), false);
  std::fill(std::begin(voiceRetrigger_), std::end(voiceRetrigger_), false);
  std::fill(std::begin(voicePressure_), std::end(voicePressure_), 0.0f);
  std::fill(std::begin(voiceSlide_), std::end(voiceSlide_), 0.0f);
  std::fill(std::begin(voiceBend_), std::end(voiceBend_), 0.0f);
  std::fill(std::begin(channelBend_), std::end(channelBend_), 0.0f);
  std::fill(std::begin(channelPressure_), std::end(channelPressure_), 0.0f);
  std::fill(std::begin(channelSlide_), std::end(channelSlide_), 0.0f);
  tickPos_ = 0;
  pendingJump_ = false;
  lastVoice_ = -1;
  partGate_ = false;
  partRetrigger_ = false;
  const PartSettings current = settings_;
  settings_ = PartSettings{};
  allocator_.configure(settings_.voiceMode, settings_.voiceCount);
  applySettings(current);
  gainL_.reset(gainL_.target());
  gainR_.reset(gainR_.target());
}

void Part::applySettings(const PartSettings& s) {
  const bool configChanged =
      s.voiceMode != settings_.voiceMode || s.voiceCount != settings_.voiceCount;
  const bool alwaysOnChanged = s.alwaysOn != alwaysOn_;
  const bool baseChanged = s.baseNote != settings_.baseNote;
  const bool engineChanged = s.engine != settings_.engine;
  const bool filterChanged = s.filterEngine != settings_.filterEngine;
  const bool shaperChanged = s.shaperEngine != settings_.shaperEngine;
  settings_ = s;

  if (configChanged) perform(allocator_.configure(s.voiceMode, s.voiceCount));
  if (alwaysOnChanged) {
    if (s.alwaysOn) {
      perform(allocator_.allNotesOff());
      alwaysOnNotes_.clear();
      alwaysOn_ = true;
      if (!voices_[0]->active()) modulation_.startVoice(0);
      voices_[0]->trigger(s.baseNote, 1.0f);
      controlValid_[0] = false;
      voiceVelocity_[0] = 1.0f;
      voiceKey_[0] = s.baseNote;
    } else {
      alwaysOn_ = false;
      alwaysOnNotes_.clear();
      voices_[0]->release();
    }
  } else if (alwaysOn_ && baseChanged && alwaysOnNotes_.empty()) {
    voices_[0]->glideTo(s.baseNote);
  }
  if (engineChanged) {
    for (auto& voice : voices_) voice->setEngine(s.engine);
  }
  if (filterChanged) {
    for (auto& voice : voices_) voice->setFilterEngine(s.filterEngine);
  }
  if (shaperChanged) {
    for (auto& voice : voices_) voice->setShaperEngine(s.shaperEngine);
  }
  for (int slot = 0; slot < kNumModSlots; ++slot) modulation_.configure(slot, s.modEngine[slot], s.modPerPart[slot]);

  const float glide = glideSeconds(s.glide);
  for (auto& voice : voices_) voice->setGlideSeconds(glide);
  updatePitchOffset();
  updateGains(s.level);
}

bool Part::isMemberChannel(int channel) const {
  return settings_.mpe != 0 && channel >= settings_.mpeFirst && channel <= settings_.mpeLast &&
         channel != settings_.mpeMaster;
}

bool Part::acceptsChannel(int channel) const {
  if (channel < 0) return true;
  if (settings_.mpe != 0) return channel == settings_.mpeMaster || isMemberChannel(channel);
  return settings_.midiChannel == 0 || channel == settings_.midiChannel - 1;
}

bool Part::acceptsKey(int key) const {
  const int low = std::min(settings_.keyLow, settings_.keyHigh);
  const int high = std::max(settings_.keyLow, settings_.keyHigh);
  return key >= low && key <= high;
}

void Part::noteOn(int key, float velocity, int channel) {
  noteChannel_ = std::clamp(channel, 0, kChannels - 1);
  if (alwaysOn_) {
    // Always on (spec §5.3): notes move the pitch but do not retrigger the voice. The per-voice
    // mod engines' gate follows the keys: the first key down triggers them (provisional ruling 8).
    if (alwaysOnNotes_.empty()) {
      voiceRetrigger_[0] = true;
      voiceVelocity_[0] = velocity;
      lastVoice_ = 0;
    }
    alwaysOnNotes_.noteOn(key, velocity);
    voices_[0]->glideTo(key);
    voiceKey_[0] = key;
    voiceChannel_[0] = noteChannel_;
    return;
  }
  perform(allocator_.noteOn(key, velocity));
}

void Part::noteOff(int key, float releaseVelocity) {
  if (key >= 0 && key < 128) keyRelease_[key] = clamp01(releaseVelocity);
  if (alwaysOn_) {
    const bool wasTop = !alwaysOnNotes_.empty() && alwaysOnNotes_.topKey() == key;
    alwaysOnNotes_.noteOff(key);
    if (alwaysOnNotes_.empty()) {
      voices_[0]->glideTo(settings_.baseNote);
      voiceRelease_[0] = clamp01(releaseVelocity);
    } else if (wasTop) {
      voices_[0]->glideTo(alwaysOnNotes_.topKey());
    }
    return;
  }
  perform(allocator_.noteOff(key));
}

void Part::pitchBend(float value) {
  bend_ = std::clamp(value, -1.0f, 1.0f);
  updatePitchOffset();
}

void Part::sustain(bool down) {
  if (!alwaysOn_) perform(allocator_.setSustain(down));
}

void Part::allNotesOff() {
  if (alwaysOn_) {
    alwaysOnNotes_.clear();
    voices_[0]->glideTo(settings_.baseNote);
    return;
  }
  perform(allocator_.allNotesOff());
}

void Part::setController(PartController controller, float value) {
  value = clamp01(value);
  switch (controller) {
    case PartController::ModWheel: modWheel_ = value; break;
    case PartController::Slide: slide_ = value; break;
    case PartController::ChannelPressure: pressure_ = value; break;
    case PartController::CcA: cc_[0] = value; break;
    case PartController::CcB: cc_[1] = value; break;
    case PartController::CcC: cc_[2] = value; break;
    case PartController::CcD: cc_[3] = value; break;
  }
}

namespace {

void setExpression(Expression e, float value, float* bend, float* pressure, float* slide) {
  if (e == Expression::Bend) {
    *bend = std::isfinite(value) ? value : 0.0f;
  } else if (e == Expression::Pressure) {
    *pressure = clamp01(value);
  } else {
    *slide = clamp01(value);
  }
}

}  // namespace

void Part::memberExpression(int channel, Expression expression, float value) {
  if (channel < 0 || channel >= kChannels) return;
  setExpression(expression, value, &channelBend_[channel], &channelPressure_[channel], &channelSlide_[channel]);
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[size_t(v)]->active() && voiceChannel_[v] == channel) {
      setExpression(expression, value, &voiceBend_[v], &voicePressure_[v], &voiceSlide_[v]);
    }
  }
}

void Part::noteExpression(int channel, int key, Expression expression, float value) {
  for (int v = 0; v < kVoices; ++v) {
    if (voices_[size_t(v)]->active() && voiceKey_[v] == key && (channel < 0 || voiceChannel_[v] == channel)) {
      setExpression(expression, value, &voiceBend_[v], &voicePressure_[v], &voiceSlide_[v]);
    }
  }
}

void Part::process(const float* const* in, float* const* out, const ParamValues& source, int n) {
  SlotParams params;
  params.source = source;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Part::delayInputSide(const float* const* input, int n) {
  const int length = int(inputDelay_.size());
  for (int i = 0; i < n; ++i) {
    const float x = input ? 0.5f * (input[0][i] + input[1][i]) : 0.0f;
    if (length == 0) {
      inputSide_[size_t(i)] = x;
      continue;
    }
    inputSide_[size_t(i)] = inputDelay_[size_t(inputDelayPos_)];
    inputDelay_[size_t(inputDelayPos_)] = x;
    if (++inputDelayPos_ == length) inputDelayPos_ = 0;
  }
}

void Part::process(const VoiceInputs& in, float* const* out, const SlotParams& params, int n) {
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  float* mix[2] = {mix_[0].data(), mix_[1].data()};
  delayInputSide(in.input, n);
  VoiceInputs voiceIn = in;
  voiceIn.inputSide = inputSide_.data();
  for (int v = 0; v < kVoices; ++v) {
    voices_[size_t(v)]->process(voiceIn, mix, params, n);
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

ModVoiceInputs Part::voiceInputs(int v) const {
  const Voice& voice = *voices_[size_t(v)];
  ModVoiceInputs m;
  m.active = voice.active();
  // Always on: the voice's gate stays high (its Amp open); its mod engines follow the keys.
  m.gate = alwaysOn_ && v == 0 ? !alwaysOnNotes_.empty() : voice.gate();
  m.retrigger = voiceRetrigger_[v];
  m.note = voice.currentNote();
  m.velocity = voiceVelocity_[v];
  m.releaseVelocity = voiceRelease_[v];
  m.random = voiceRandom_[v];
  m.pressure = voicePressure_[v];
  m.slide = voiceSlide_[v];
  m.bend = voiceBend_[v];
  return m;
}

ModPartInputs Part::partInputs(const PartParams& params, int offset) const {
  ModPartInputs p;
  p.modWheel = modWheel_;
  p.pitchBend = bend_;
  p.channelPressure = pressure_;
  p.slide = slide_;
  for (int i = 0; i < 4; ++i) p.cc[i] = cc_[i];
  const ModTiming& t = params.timing;
  const double bars = t.barPhase + double(offset) * t.beatsPerSample / std::max(1e-9, t.beatsPerBar);
  p.transportPhase = float(bars - std::floor(bars));
  p.noteBendRange = float(settings_.mpeBendRange);
  p.gate = partGate_;
  p.retrigger = partRetrigger_;
  p.beat = t.beat + double(offset) * t.beatsPerSample;
  p.beatsPerSample = t.beatsPerSample;
  p.beatJump = pendingJump_;
  p.lastVoice = lastVoice_;
  return p;
}

void Part::controlVoice(int v, const PartParams& params, const ModPartInputs& part, int length) {
  VoiceControl& c = controls_[v];
  modulation_.processVoice(v, params.mod, part, voiceInputs(v), params.slots, &c, length);
  c.gain = params.voiceGain;
  c.sourceGain = 1.0f;
  voices_[size_t(v)]->beginTick(c, length);
  controlValid_[v] = true;
  voiceRetrigger_[v] = false;
}

void Part::startTick(const PartParams& params, int offset) {
  // The part-scope gate: any key held (Always on: any key down), triggering on the first key.
  bool gate = false;
  if (alwaysOn_) {
    gate = !alwaysOnNotes_.empty();
  } else {
    for (const auto& voice : voices_) gate = gate || voice->gate();
  }
  if (gate && !partGate_) partRetrigger_ = true;
  partGate_ = gate;
  ModVoiceInputs voices[kVoices];
  for (int v = 0; v < kVoices; ++v) voices[v] = voiceInputs(v);
  const ModPartInputs part = partInputs(params, offset);
  modulation_.processPart(params.mod, part, voices, Voice::kControlBlock);
  partRetrigger_ = false;
  pendingJump_ = false;
  updateGains(modulation_.partLevel(params.mod, settings_.level));
  for (int v = 0; v < kVoices; ++v) {
    controlValid_[v] = false;
    if (voices_[size_t(v)]->active()) controlVoice(v, params, part, Voice::kControlBlock);
  }
}

void Part::process(const VoiceInputs& in, float* const* out, const PartParams& params, int n) {
  std::fill(mix_[0].begin(), mix_[0].begin() + n, 0.0f);
  std::fill(mix_[1].begin(), mix_[1].begin() + n, 0.0f);
  delayInputSide(in.input, n);
  pendingJump_ = pendingJump_ || params.timing.jump;
  for (int done = 0; done < n;) {
    // Modulation runs on a fixed grid of 16-sample ticks, whatever the host's block sizes.
    if (tickPos_ == 0) startTick(params, done);
    const int count = std::min(Voice::kControlBlock - tickPos_, n - done);
    for (int v = 0; v < kVoices; ++v) {
      // A voice that started since the tick began gets controls for the rest of the tick.
      if (voices_[size_t(v)]->active() && !controlValid_[v]) {
        controlVoice(v, params, partInputs(params, done), Voice::kControlBlock - tickPos_);
      }
    }
    const float* input[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    const float* other[2] = {in.other ? in.other[0] + done : nullptr, in.other ? in.other[1] + done : nullptr};
    VoiceInputs slice;
    slice.input = in.input ? input : nullptr;
    slice.other = in.other ? other : nullptr;
    slice.inputSide = inputSide_.data() + done;
    float* mix[2] = {mix_[0].data() + done, mix_[1].data() + done};
    for (int v = 0; v < kVoices; ++v) {
      voices_[size_t(v)]->render(slice, mix, count);
      if (allocator_.sounding(v) && !voices_[size_t(v)]->active()) allocator_.voiceIdle(v);
    }
    tickPos_ = (tickPos_ + count) % Voice::kControlBlock;
    done += count;
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

int Part::activeVoiceCount() const {
  int count = 0;
  for (const auto& voice : voices_) count += voice->active() ? 1 : 0;
  return count;
}

void Part::startNote(int v, int key, float velocity) {
  voiceKey_[v] = key;
  voiceChannel_[v] = noteChannel_;
  voiceVelocity_[v] = clamp01(velocity);
  voiceRelease_[v] = 0.0f;
  random_ ^= random_ << 13;  // xorshift32: a new Random source value per note
  random_ ^= random_ >> 17;
  random_ ^= random_ << 5;
  voiceRandom_[v] = float(random_ >> 8) / 16777216.0f;
  // An MPE member channel's expression sent before the note belongs to it (provisional ruling 11).
  const bool member = isMemberChannel(noteChannel_);
  voiceBend_[v] = member ? channelBend_[noteChannel_] : 0.0f;
  voicePressure_[v] = member ? channelPressure_[noteChannel_] : 0.0f;
  voiceSlide_[v] = member ? channelSlide_[noteChannel_] : 0.0f;
  voiceRetrigger_[v] = true;
  lastVoice_ = v;
}

void Part::perform(const VoiceActions& actions) {
  for (int i = 0; i < actions.count; ++i) {
    const VoiceAction& a = actions.items[i];
    Voice& voice = *voices_[size_t(a.voice)];
    switch (a.type) {
      case VoiceAction::Type::Trigger:
        if (!voice.active()) {
          modulation_.startVoice(a.voice);  // a voice from silence: fresh mod engines
          controlValid_[a.voice] = false;
        }
        voice.trigger(a.key, a.velocity);
        startNote(a.voice, a.key, a.velocity);
        break;
      case VoiceAction::Type::Glide:
        voice.glideTo(a.key);
        voiceKey_[a.voice] = a.key;
        voiceChannel_[a.voice] = noteChannel_;
        break;
      case VoiceAction::Type::Release:
        voice.release();
        voiceRelease_[a.voice] = a.key >= 0 && a.key < 128 ? keyRelease_[a.key] : kDefaultReleaseVelocity;
        break;
    }
  }
}

void Part::updatePitchOffset() {
  const float offset = float(settings_.transpose) + bend_ * float(settings_.bendRange);
  for (auto& voice : voices_) voice->setPitchOffset(offset);
}

void Part::updateGains(float level) {
  if (settings_.mono) {
    gainL_.setTarget(level);
    gainR_.setTarget(level);
    return;
  }
  gainL_.setTarget(level * std::min(1.0f, 1.0f - settings_.pan));
  gainR_.setTarget(level * std::min(1.0f, 1.0f + settings_.pan));
}

}  // namespace eg13
```

What changed against M3's `part.cpp`: the constructors build `modulation_`; `prepare` prepares it; `reset` clears the M4 state; `applySettings` configures the mod slots and, when Always on starts, starts voice 0's mod slots; `acceptsChannel` knows MPE zones; `noteOn`/`noteOff` take a channel and a release velocity; `perform` restarts a voice's mod slots when it starts from silence and records its sources (`startNote`); the new `process(..., const PartParams&, n)` runs the grid. The M3 `process` overloads are unchanged.

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "modulated part|Envelope routed to the Amp|host block slicing|retrigger on each note|Always on: the Amp|MPE member expression|Polyphonic pressure|Part Level moves"`
Expected: all pass. Then the full suite: every existing part test passes unchanged (a planning build passed all 20).

- [ ] **Step 6: Document the voice chain**

In `docs/manual/parts-and-routing.md`, replace "Each voice runs Source → Filter → Shaper. Every engine of every slot is ready in every voice, so changing an engine never loads anything." with:

```markdown
Each voice runs Source → Filter → Shaper → Amp, then its pan. Each part also has four mod slots and a modulation matrix ([Modulation](modulation.md)). Every engine of every slot, mod slots included, is ready in every voice, so changing an engine never loads anything.
```

- [ ] **Step 7: Commit**

```bash
git add core/part/part.h core/part/part.cpp tests/test_part.cpp docs/manual/parts-and-routing.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Part: modulation on a fixed 16-sample grid, gates, MPE expressions, part level"
```

---
### Task 12: Gain staging: Source trims, Audio Input chords, Resonator velocity, the soft clipper

**Files:**
- Create: `core/util/soft_clip.h`
- Modify: `core/engine/engine_registry.h`, `core/engine/engine_registry.cpp`, `core/engine/rings_engine.h`, `core/engine/rings_engine.cpp`, `core/part/part.h`, `core/part/part.cpp`, `docs/manual/modulation.md`, `docs/manual/engines.md`, `docs/manual/limitations.md`
- Test: `tests/test_soft_clip.cpp` (new), `tests/test_rings_engine.cpp`, `tests/test_part.cpp` (extended)

**Interfaces:**
- Consumes: `Part`, `VoiceControl::sourceGain` (Tasks 10–11), `Declicker`, `kMixDeclickSeconds` (`pot_smoother.h`).
- Produces: `float sourceTrimDb(int engine); float sourceTrim(int engine);` (`engine_registry.h`); `constexpr float kSoftClipThreshold; float softClip(float);` (`core/util/soft_clip.h`, wired into the Synth in Task 14); `float RingsEngine::velocityGain() const;` (test hook). Part sets each voice's `VoiceControl::sourceGain` to `sourceTrim(engine) × inputScale_`.

The M2 review (ruling I4) asked for gain staging before M7's presets: −12 dB per voice (the Voice Gain, wired in Tasks 13–14), per-engine trims, Audio Input chord scaling and a review of the soft limiter. Planning measured each Source at its defaults (48 kHz, velocity 1, one held C3, Filter and Shaper Off): peaks 0.66 (Macro), 0.97 (Digital), 0.17 (Modal), 0.17 (Resonator), and the Input's own level (Audio Input); 8-note chords peaked at 4.1, 5.6, 0.91, 0.86 and 8 × the Input. The trims (provisional ruling 25) put single notes within about 1 dB of the Macro Oscillator. The inter-part soft limiter (unity to full scale, never above twice full scale) stays as it is: with −12 dB per voice a part reaches it only with very loud chords, and it protects loops rather than levels.

- [ ] **Step 1: Write the failing tests**

Create `tests/test_soft_clip.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "util/soft_clip.h"

using namespace eg13;

TEST_CASE("The soft clipper leaves everything below -3 dBFS untouched") {
  for (float x = -0.708f; x <= 0.708f; x += 0.001f) {
    if (std::fabs(x) <= kSoftClipThreshold) REQUIRE(softClip(x) == x);
  }
  REQUIRE(softClip(0.0f) == 0.0f);
  REQUIRE(std::fabs(20.0f * std::log10(kSoftClipThreshold) + 3.0f) < 1e-4f);
}

TEST_CASE("The soft clipper is smooth, odd, rising and never exceeds full scale") {
  float previous = softClip(kSoftClipThreshold);
  for (float x = kSoftClipThreshold + 1e-4f; x < 2.9f; x += 1e-4f) {
    const float y = softClip(x);
    REQUIRE(y >= previous);  // never falls
    REQUIRE(y < 1.0f);       // below full scale (from about 3x full scale float rounds it to 1)
    REQUIRE(softClip(-x) == -y);
    previous = y;
  }
  // Slope 1 at the knee: no corner.
  const float h = 1e-3f;
  const float slope = (softClip(kSoftClipThreshold + h) - softClip(kSoftClipThreshold)) / h;
  REQUIRE(std::fabs(slope - 1.0f) < 0.01f);
  REQUIRE(softClip(1.0f) < 0.96f);  // a full-scale peak is gently reduced
  REQUIRE(softClip(1e6f) <= 1.0f);
  REQUIRE(softClip(-1e6f) >= -1.0f);
  REQUIRE(softClip(INFINITY) == 1.0f);
}
```

Append to `tests/test_rings_engine.cpp`:

```cpp

TEST_CASE("A new note's velocity glides the Resonator's gain over 5 ms") {
  RingsEngine engine;
  engine.prepare(RingsEngine::kNativeRate, 256);
  std::vector<float> l(256), r(256);
  float* out[2] = {l.data(), r.data()};
  ParamValues p = defaults();
  p.p[8] = kPoly2;
  engine.noteOn(60.0f, 1.0f);
  engine.process(nullptr, out, p, 256);
  REQUIRE(engine.velocityGain() == 1.0f);
  engine.noteOn(64.0f, 0.25f);
  engine.process(nullptr, out, p, 24);  // one native block, half a millisecond
  REQUIRE(engine.velocityGain() > 0.8f);
  REQUIRE(engine.velocityGain() < 1.0f);
  for (int b = 0; b < 10; ++b) engine.process(nullptr, out, p, 256);  // 53 ms
  REQUIRE(std::fabs(engine.velocityGain() - 0.25f) < 1e-3f);
  engine.reset();
  engine.noteOn(60.0f, 0.5f);
  engine.process(nullptr, out, p, 24);
  REQUIRE(engine.velocityGain() == 0.5f);  // a note after a reset starts at its velocity
}
```

Append to `tests/test_part.cpp` (add `#include "voice/source_factory.h"` to its includes):

```cpp

TEST_CASE("Each Source's trim reaches its voices") {
  for (int engine = 0; engine < kNumSourceEngines; ++engine) {
    Part part(steady, test::testModEngines);
    part.prepare(kRate, kBlock);
    PartSettings s;
    s.engine = engine;  // the test voices have one engine; the trim follows the setting
    part.applySettings(s);
    part.noteOn(60, 1.0f);
    runModulated(part, 32, PartParams{});
    INFO("engine " << engine);
    REQUIRE(part.lastControl(0).sourceGain == sourceTrim(engine));
  }
  REQUIRE(sourceTrimDb(int(SourceEngine::Macro)) == 0.0f);
  REQUIRE(sourceTrimDb(int(SourceEngine::Digital)) == -3.0f);
  REQUIRE(sourceTrimDb(int(SourceEngine::Modal)) == 12.0f);
  REQUIRE(sourceTrimDb(int(SourceEngine::Resonator)) == 12.0f);
  REQUIRE(sourceTrimDb(int(SourceEngine::AudioInput)) == 0.0f);
  REQUIRE(std::fabs(sourceTrim(int(SourceEngine::Modal)) - 3.98107f) < 1e-4f);
}

TEST_CASE("An Audio Input chord plays the Input at one voice's level") {
  auto level = [](int notes) {
    Part part([] { return makeSource(int(SourceEngine::AudioInput)); }, test::testModEngines);
    part.prepare(kRate, kBlock);
    PartSettings s;
    s.engine = int(SourceEngine::AudioInput);
    part.applySettings(s);
    PartParams params;
    params.slots.source = test::defaultParams(sourceEngineInfo(int(SourceEngine::AudioInput)));
    std::vector<float> in(9600);
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.25f * float(std::sin(6.283185307179586 * 220.0 * double(i) / kRate));
    for (int k = 0; k < notes; ++k) part.noteOn(60 + 4 * k, 1.0f);
    std::vector<float> l(9600), r(9600);
    for (int done = 0; done < 9600; done += kBlock) {
      const float* input[2] = {in.data() + done, in.data() + done};
      float* out[2] = {l.data() + done, r.data() + done};
      VoiceInputs vin;
      vin.input = input;
      part.process(vin, out, params, kBlock);
    }
    return test::rms(l, 4800, 9600);
  };
  const double one = level(1);
  REQUIRE(one > 0.1);
  REQUIRE(std::fabs(level(4) / one - 1.0) < 0.01);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`util/soft_clip.h` not found; `velocityGain`, `sourceTrim`, `sourceTrimDb` not declared).

- [ ] **Step 3: Write `core/util/soft_clip.h`**

```cpp
#pragma once

#include <cmath>

namespace eg13 {

// The master output's soft clipper (spec §6, owner 2026-10-20; provisional ruling 23): unchanged
// up to -3 dBFS, then a tanh knee that leaves the straight line with the same slope and
// approaches full scale, never exceeding it (in float it reaches exactly 1.0 from about 3x full
// scale, +9.5 dBFS, on).
constexpr float kSoftClipThreshold = 0.70794578f;  // 10^(-3/20)

inline float softClip(float x) {
  const float a = std::fabs(x);
  if (!(a > kSoftClipThreshold)) return x;
  constexpr float kHeadroom = 1.0f - kSoftClipThreshold;
  return std::copysign(kSoftClipThreshold + kHeadroom * std::tanh((a - kSoftClipThreshold) / kHeadroom), x);
}

}  // namespace eg13
```

- [ ] **Step 4: Add the Source trims to the registry**

In `core/engine/engine_registry.h`, after `const EngineInfo& sourceEngineInfo(int engine);`, add:

```cpp
// Gain staging (M4, provisional ruling 25): each Source's trim at its output, before the Filter,
// so a single held note at the engine's defaults peaks near the Macro Oscillator's (planning
// measurement at 48 kHz, velocity 1, C3: peaks 0.66, 0.97, 0.17, 0.17; Audio Input passes its
// input). In dB and as a gain; unknown engines 0 dB.
float sourceTrimDb(int engine);
float sourceTrim(int engine);
```

In `core/engine/engine_registry.cpp`, before `int filterEngineFromSelector(double value)`, add:

```cpp
float sourceTrimDb(int engine) {
  switch (SourceEngine(engine)) {
    case SourceEngine::Digital: return -3.0f;
    case SourceEngine::Modal: return 12.0f;
    case SourceEngine::Resonator: return 12.0f;
    case SourceEngine::Macro:
    case SourceEngine::AudioInput: break;
  }
  return 0.0f;
}

float sourceTrim(int engine) { return std::pow(10.0f, sourceTrimDb(engine) / 20.0f); }
```

- [ ] **Step 5: Apply the trims and the Audio Input chord scaling in the Part**

In `core/part/part.h`, after `uint32_t random_ = 0x13579BDFu;`, add:

```cpp
  float inputScale_ = 1.0f;  // Audio Input chord scaling: 1 / open Audio Input voices (ruling 26)
```

In `core/part/part.cpp`, add `#include "engine/engine_registry.h"` before `#include "synth/param_table.h"`. In `Part::controlVoice`, replace `c.sourceGain = 1.0f;` with:

```cpp
  c.sourceGain = sourceTrim(settings_.engine) * inputScale_;  // gain staging (rulings 25, 26)
```

In `Part::startTick`, after `partGate_ = gate;`, add:

```cpp
  // Audio Input voices all carry the same Input: scale each by the number with an open gate, so
  // a chord plays the Input at one voice's level.
  int open = 0;
  for (const auto& voice : voices_) open += voice->active() && voice->gate() ? 1 : 0;
  inputScale_ = settings_.engine == int(SourceEngine::AudioInput) ? 1.0f / float(std::max(1, open)) : 1.0f;
```

- [ ] **Step 6: De-click the Resonator's velocity**

In `core/engine/rings_engine.h`, after the `smoothedSpread()` hook, add:

```cpp
  // Test hook: the de-clicked velocity gain applied to the last output sample.
  float velocityGain() const { return velocityGain_.value(); }
```

and after `Declicker spread_;  // EG13-only Spread, per sample`:

```cpp
  // The velocity gain moves over 5 ms instead of stepping (M4, provisional ruling 27): with
  // polyphony above 1 a new strum's velocity still rescales earlier strums, without a click.
  Declicker velocityGain_;
```

In `core/engine/rings_engine.cpp`: initialise it in the constructor (`spread_(kMixDeclickSeconds), velocityGain_(kMixDeclickSeconds) {`); in `prepare`, after `spread_.prepare(hostRate);`, add `velocityGain_.prepare(hostRate);`; in `reset`, after `spread_.reset();`, add `velocityGain_.reset();`; in `renderBlock`'s output loop, after `const float spread = spread_.next(spreadTarget);`, add `const float velocity = velocityGain_.next(velocity_);` and use `velocity` instead of `velocity_` in the three output lines (`out[0]`, `out[1]`, `out[2]`). A note after a reset still starts exactly at its velocity (the Declicker snaps its first value), so the bit-exact Resonator tests are unaffected (a planning build passed all 24 Resonator tests).

- [ ] **Step 7: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac -R "soft clipper|velocity glides|Source's trim|Audio Input chord"`
Expected: all pass. Then the full suite.

- [ ] **Step 8: Document gain staging**

Append to `docs/manual/modulation.md`:

````markdown
## Levels

EG13 keeps room for chords and evens out its Sources (gain staging):

- **Voice Gain** (a master setting): each voice plays 12 dB lower (*−12 dB*, the instrument's default), so a chord of eight loud voices does not overload the mix. *0 dB* plays each voice at full level: EG13 FX starts there (it usually plays one voice), and projects saved before modulation existed load with it, so they keep their level.
- **Source trims**: each Source is turned up or down at its output, before the Filter, so one held note at the engine's default settings comes out about as loud on every Source: the Digital Oscillator 3 dB down, the Modal Synth and the Resonator 12 dB up, the Macro Oscillator and Audio Input unchanged. This also means the Filter and the Shaper see similar levels from every Source.
- **Audio Input chords**: every Audio Input voice carries the same Input, so EG13 divides each one by the number of keys held. A chord plays the Input at one voice's level instead of several times louder.
- **Soft Clip** (a master setting, on by default): the main and Bus 2 outputs pass unchanged below −3 dBFS; above that they bend smoothly towards full scale and never go over it. Switch it off to hear EG13's output exactly (the host may then clip it).
````

In `docs/manual/engines.md`, in the Resonator section, add after the Polyphony bullet:

```markdown
- Velocity sets the Resonator's level. A new note's velocity takes 5 ms to take effect, so it does not click; with Polyphony above 1 it also changes the level of earlier strings still ringing, as there is one level for the whole Resonator.
```

In `docs/manual/limitations.md`, replace the bullet that begins "**Resonator velocity:**" with:

```markdown
- **Resonator velocity:** with Polyphony 2 or 4, a new note's velocity also changes the level of earlier notes still ringing (over 5 ms, without a click). A level per string would need a change inside the original module's code.
```

- [ ] **Step 9: Commit**

```bash
git add core/util/soft_clip.h core/engine/engine_registry.h core/engine/engine_registry.cpp core/engine/rings_engine.h core/engine/rings_engine.cpp core/part tests/test_soft_clip.cpp tests/test_rings_engine.cpp tests/test_part.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Gain staging: Source trims, Audio Input chord scaling, Resonator velocity de-click, soft clipper"
```

---
### Task 13: Mod slots as host parameters: table v5, engine switching with scope, saved settings

**Files:**
- Modify: `core/synth/param_table.h`, `core/synth/param_table.cpp`, `core/synth/synth.h`, `core/synth/synth.cpp`, `core/state/state.cpp`, `docs/manual/modulation.md`, `docs/manual/engines.md`
- Test: `tests/test_param_table.cpp`, `tests/test_levels.cpp`, `tests/test_synth.cpp`, `tests/test_state.cpp`

**Interfaces:**
- Consumes: `modEngineInfo`, `modEngineFromSelector`, `kNumModEngines`, `kModEngineSelectorMax`, `ModEngineKind` (Task 7); `Route`, codes, `kNumRoutes`, `kNumModSlots`, `kPitchRangeSemitones`, `destinationKind` (Task 8); `TidesEngine::frequencyFor`, `TidesEngine::Medium` (Task 4); `params::Slot::Mod1..Mod4`, `params::Slot::Matrix` (`core/params/param_ids.h`, unchanged).
- Produces (`core/synth/param_table.h`):
  - `SlotKind` gains `Mod1 = 3, Mod2, Mod3, Mod4`; `kNumSlotKinds = 7`; `constexpr bool isModSlot(SlotKind); constexpr SlotKind modSlotKind(int slot); constexpr int modSlotIndex(SlotKind);`
  - `kModSlotParamCount = 16; slotParamCount(Mod*) = 16; slotBlockSize(SlotKind); kSlotsBlockSize = 107; kParamsPerPart = 140; kParamCount = 286;`
  - `PartParam::MpeMode = 16, MpeBendRange, CcA, CcB, CcC, CcD` (`kPartParamCount = 21`); `MasterParam::VoiceGain = 4, SoftClip` (`kMasterParamCount = 6`).
  - `modEngineParamIndex(part, slot), modParamIndex(part, slot, p), modScopeParamIndex(part, slot), routeAmountParamIndex(part, route)`; `bool modScopeOf(size_t, int* part, int* slot); bool routeAmountOf(size_t, int* part, int* route);`
  - `ParamFormat::ModEngine, ModScope, RouteAmount, MpeMode, CcNumber, VoiceGain`.
  - `PartEngines` gains `int mod[4]; uint16_t routeDestination[12];` (existing brace initialisers `{source, filter, shaper}` stay valid).
  - `Route initPatchRoute(PluginKind, int part, int route); float initPatchAmount(PluginKind, int part, int route);` (the init patches' routes, patch data for Task 14, and their amounts, the route amounts' host defaults).
  - `core/synth/synth.h`: the stash gains the mod slots: `kModStashSize`, `kSlotStashSize`, `kScopeStashSize`, `kStashSize = kSlotStashSize + kScopeStashSize`; `slotStashIndex(SlotKind::Mod1..Mod4, part, engine, p)`; `constexpr size_t scopeStashIndex(int part, int slot, int engine);`; `kMaxHostUpdates` covers mod P's, scopes and the CC numbers. `Synth::switchEngine` swaps a mod engine's Scope with its P's; `Synth::defaultStash()` gives each mod engine its own scope; `readPartSettings` passes `modEngine[]` and `modPerPart[]` to the Part (still unmodulated until Task 14).
  - `core/state/state.cpp`: sections `mod1Settings` … `mod4Settings`: `{part: {engine: [P1..P16, Scope]}}`.

This task makes the mod slots complete as host parameters: they exist, switch engines exactly like the Source, Filter and Shaper slots (M2's per-engine stash in `Synth::setParam`, host updates sent with `CLAP_EVENT_DONT_RECORD` and a VALUES|TEXT|INFO rescan, which the plugin already does for every queued update), and their settings are saved. Sound is unchanged: the Synth still calls the Part's unmodulated path (Task 14 switches it).

Every new stepped parameter states its append rule beside its definition (Global Constraints). The host defaults follow provisional rulings 1, 2, 24, 28 and 29: shared mod P values `{0, 0, .5, .5, .5, 1, .5, .5, .5, 0, 0, 0, 0, 0, 0, 0}` with the two init-LFO rates; Scope per voice except EG13 FX Part A Mod 1; the instrument's Filter Type State Variable with P1 at 70 %.

- [ ] **Step 1: Update the existing table tests and write the new ones (`tests/test_param_table.cpp`)**

Add `#include <algorithm>` and `#include <iterator>` after `#include <cmath>`, and `#include "engine/tides_engine.h"` and `#include "mod/mod_factory.h"` after `#include "engine/ripples_engine.h"`.

Replace the first lines of the test "The table has 104 parameters with unique IDs" up to `REQUIRE(kParamCount == 104);` with:

```cpp
TEST_CASE("The table has 286 parameters with unique IDs") {
  REQUIRE(kSourceParamCount == 16);
  REQUIRE(kFilterParamCount == 4);
  REQUIRE(kShaperParamCount == 12);
  REQUIRE(kModSlotParamCount == 16);
  REQUIRE(kParamsPerPart == 140);
  REQUIRE(kParamCount == 286);
```

In "Source P9..P16 have their own IDs after P1..P8", replace the layout block (from `// Layout per part:` to `REQUIRE(masterParamIndex(MasterParam::Level) == 100);`) with:

```cpp
  // Layout per part: Source selector + P1..P16, Filter + P1..P4, Shaper + P1..P12, four mod slots
  // (selector + P1..P16 + Scope), 12 route amounts, part settings.
  REQUIRE(filterEngineParamIndex(0) == 17);
  REQUIRE(shaperEngineParamIndex(0) == 22);
  REQUIRE(modEngineParamIndex(0, 0) == 35);
  REQUIRE(modScopeParamIndex(0, 0) == 52);
  REQUIRE(modEngineParamIndex(0, 3) == 89);
  REQUIRE(routeAmountParamIndex(0, 0) == 107);
  REQUIRE(partParamIndex(0, PartParam::Level) == 119);
  REQUIRE(sourceEngineParamIndex(1) == 140);
  REQUIRE(masterParamIndex(MasterParam::Level) == 280);
```

In "Filter and Shaper defaults per plugin": replace `REQUIRE(paramDefault(filterEngineParamIndex(part), PluginKind::Instrument) == 0.0f);` with

```cpp
    // The instrument init patch's filter (M4, provisional ruling 28).
    REQUIRE(paramDefault(filterEngineParamIndex(part), PluginKind::Instrument) == float(FilterEngine::StateVariable));
```

replace `REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Instrument) == 1.0f);` with

```cpp
  REQUIRE(paramDefault(filterParamIndex(0, 0), PluginKind::Instrument) == 0.7f);  // the init patch's cutoff
  REQUIRE(paramDefault(filterParamIndex(1, 0), PluginKind::Effect) == 1.0f);
```

and replace `REQUIRE(std::string(paramSpec(filterParamIndex(0, 0)).name) == "(unused)");` with `REQUIRE(std::string(paramSpec(filterParamIndex(0, 0)).name) == "Cutoff");  // the SVF's`.

In "Slot lookups find every slot parameter and selector", after `REQUIRE(counted[2] == 2 * 12);` add:

```cpp
  for (int k = int(SlotKind::Mod1); k <= int(SlotKind::Mod4); ++k) REQUIRE(counted[k] == 2 * 16);
```

Append:

```cpp

TEST_CASE("Mod slot, scope, route and new setting IDs are stable") {
  REQUIRE(paramSpec(modEngineParamIndex(0, 0)).id == params::id(0, params::Slot::Mod1, 0));
  REQUIRE(paramSpec(modParamIndex(0, 0, 0)).id == params::id(0, params::Slot::Mod1, 1));
  REQUIRE(paramSpec(modParamIndex(1, 3, 15)).id == params::id(1, params::Slot::Mod4, 16));
  REQUIRE(paramSpec(modScopeParamIndex(0, 2)).id == params::id(0, params::Slot::Mod3, 17));
  REQUIRE(paramSpec(routeAmountParamIndex(0, 0)).id == params::id(0, params::Slot::Matrix, 1));
  REQUIRE(paramSpec(routeAmountParamIndex(1, 11)).id == params::id(1, params::Slot::Matrix, 12));
  REQUIRE(paramSpec(partParamIndex(0, PartParam::MpeMode)).id == params::id(0, params::Slot::Part, 16));
  REQUIRE(paramSpec(partParamIndex(1, PartParam::CcD)).id == params::id(1, params::Slot::Part, 21));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::VoiceGain)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 4));
  REQUIRE(paramSpec(masterParamIndex(MasterParam::SoftClip)).id ==
          params::id(params::kMasterPart, params::Slot::Part, 5));
  // Earlier IDs keep their entries.
  REQUIRE(paramIndex(params::id(0, params::Slot::Shaper, 12)) == int(shaperParamIndex(0, 11)));
  REQUIRE(paramIndex(params::id(1, params::Slot::Part, 15)) == int(partParamIndex(1, PartParam::InputSource)));
  REQUIRE(std::string(paramSpec(modEngineParamIndex(1, 1)).module) == "Part B/Mod 2");
  REQUIRE(std::string(paramSpec(routeAmountParamIndex(0, 4)).module) == "Part A/Matrix");
  REQUIRE(std::string(paramSpec(routeAmountParamIndex(0, 4)).name) == "Route 5");
  REQUIRE(std::string(paramSpec(modScopeParamIndex(0, 0)).name) == "Scope");
  int part = -1, slot = -1;
  REQUIRE(modScopeOf(modScopeParamIndex(1, 3), &part, &slot));
  REQUIRE(part == 1);
  REQUIRE(slot == 3);
  REQUIRE_FALSE(modScopeOf(modParamIndex(1, 3, 15), &part, &slot));
  int route = -1;
  REQUIRE(routeAmountOf(routeAmountParamIndex(1, 7), &part, &route));
  REQUIRE(route == 7);
  REQUIRE_FALSE(routeAmountOf(partParamIndex(0, PartParam::Level), &part, &route));
}

TEST_CASE("The init patches set mod engines, scopes, routes and amounts") {
  for (int part = 0; part < kNumParts; ++part) {
    REQUIRE(paramDefault(modEngineParamIndex(part, 0), PluginKind::Instrument) == float(ModEngineKind::Envelope));
    REQUIRE(paramDefault(modEngineParamIndex(part, 1), PluginKind::Instrument) == float(ModEngineKind::Envelope));
    REQUIRE(paramDefault(modEngineParamIndex(part, 2), PluginKind::Instrument) == float(ModEngineKind::Lfo));
    REQUIRE(paramDefault(modEngineParamIndex(part, 3), PluginKind::Instrument) == 0.0f);
    REQUIRE(paramDefault(modScopeParamIndex(part, 0), PluginKind::Instrument) == 0.0f);
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 0) == Route{modOutputSource(0, 0), kDestAmp, kSrcVelocity});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 1) == Route{modOutputSource(1, 0), kDestFilterP, kSrcNone});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 2) == Route{modOutputSource(2, 0), kDestPitch, kSrcModWheel});
    REQUIRE(initPatchRoute(PluginKind::Instrument, part, 3) == Route{});
    REQUIRE(paramDefault(routeAmountParamIndex(part, 0), PluginKind::Instrument) == 1.0f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 1), PluginKind::Instrument) == 0.3f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 2), PluginKind::Instrument) * 24.0f == 0.5f);
    REQUIRE(paramDefault(routeAmountParamIndex(part, 3), PluginKind::Instrument) == 0.0f);
  }
  REQUIRE(paramDefault(modEngineParamIndex(0, 0), PluginKind::Effect) == float(ModEngineKind::Lfo));
  REQUIRE(paramDefault(modScopeParamIndex(0, 0), PluginKind::Effect) == 1.0f);  // per part
  REQUIRE(paramDefault(modEngineParamIndex(1, 0), PluginKind::Effect) == 0.0f);
  REQUIRE(initPatchRoute(PluginKind::Effect, 0, 0) == Route{modOutputSource(0, 0), kDestFilterP, kSrcNone});
  REQUIRE(initPatchRoute(PluginKind::Effect, 1, 0) == Route{});
  REQUIRE(paramDefault(routeAmountParamIndex(0, 0), PluginKind::Effect) == 0.2f);
  REQUIRE(paramDefault(routeAmountParamIndex(1, 0), PluginKind::Effect) == 0.0f);
  // The init LFO rates (provisional ruling 29).
  REQUIRE(std::fabs(paramDefault(modParamIndex(0, 2, 2), PluginKind::Instrument) -
                    TidesEngine::frequencyFor(5.0, TidesEngine::Medium)) < 1e-6f);
  REQUIRE(std::fabs(paramDefault(modParamIndex(0, 0, 2), PluginKind::Effect) -
                    TidesEngine::frequencyFor(0.25, TidesEngine::Medium)) < 1e-6f);
  // Voice Gain -12 dB in the instrument, 0 dB in EG13 FX; Soft Clip on in both.
  REQUIRE(paramDefault(masterParamIndex(MasterParam::VoiceGain), PluginKind::Instrument) == 0.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::VoiceGain), PluginKind::Effect) == 1.0f);
  REQUIRE(paramDefault(masterParamIndex(MasterParam::SoftClip), PluginKind::Instrument) == 1.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::MpeBendRange), PluginKind::Instrument) == 48.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::CcA), PluginKind::Instrument) == 2.0f);
  REQUIRE(paramDefault(partParamIndex(0, PartParam::CcD), PluginKind::Instrument) == 16.0f);
}

TEST_CASE("Mod slot host defaults suit every mod engine") {
  // A host's reset sets each mod P to one value whatever engine is selected (provisional ruling
  // 1). Every engine must play sensibly there, and away from the documented init-patch rates
  // they are the engines' own defaults, so a reset and a fresh engine agree.
  for (int e = 1; e < kNumModEngines; ++e) {
    const EngineInfo& info = modEngineInfo(e).engine;
    for (int p = 0; p < info.paramCount; ++p) {
      INFO("engine " << info.displayName << " P" << p + 1);
      REQUIRE(paramDefault(modParamIndex(1, 3, p), PluginKind::Instrument) == info.params[p].defaultValue);
    }
    for (PluginKind plugin : {PluginKind::Instrument, PluginKind::Effect}) {
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        ParamValues p;
        for (int i = 0; i < kModParamCount; ++i) p.p[i] = paramDefault(modParamIndex(0, slot, i), plugin);
        auto engine = makeModEngine(e);
        engine->prepare(48000.0);
        engine->reset();
        float lowest[kMaxModOutputs], highest[kMaxModOutputs];
        std::fill(std::begin(lowest), std::end(lowest), 2.0f);
        std::fill(std::begin(highest), std::end(highest), -2.0f);
        float out[kMaxModOutputs];
        for (int t = 0; t < 9000; ++t) {  // 3 s, the gate toggling every half second
          ModContext ctx;
          ctx.gate = (t / 1500) % 2 == 0;
          ctx.beat = double(t) * 16.0 * 2.0 / 48000.0;
          ctx.beatsPerSample = 2.0 / 48000.0;
          engine->process(ctx, p, 16, out);
          for (int k = 0; k < kMaxModOutputs; ++k) {
            REQUIRE(std::isfinite(out[k]));
            lowest[k] = std::min(lowest[k], out[k]);
            highest[k] = std::max(highest[k], out[k]);
          }
        }
        INFO("engine " << info.displayName << " slot " << slot);
        REQUIRE(highest[0] - lowest[0] > 0.1f);  // output 1 moves
        for (int k = 0; k < kMaxModOutputs; ++k) {
          REQUIRE(lowest[k] >= -1.0f);
          REQUIRE(highest[k] <= 1.0f);
        }
      }
    }
  }
}

TEST_CASE("Mod slot and route parameters name and format through their engines and destinations") {
  char buf[64];
  PartEngines engines;
  engines.mod[0] = int(ModEngineKind::Lfo);
  engines.mod[1] = int(ModEngineKind::Envelope);
  engines.routeDestination[0] = kDestPitch;
  engines.routeDestination[1] = kDestFilterP;
  REQUIRE(std::string(paramName(modParamIndex(0, 0, 1), engines)) == "Sync");
  REQUIRE(std::string(paramName(modParamIndex(0, 0, 7), engines)) == "(unused)");
  REQUIRE(std::string(paramName(modParamIndex(0, 1, 1), engines)) == "Attack");
  REQUIRE(std::string(paramName(modParamIndex(0, 3, 0), engines)) == "(unused)");  // Off
  formatParam(modParamIndex(0, 1, 1), 0.0, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "1 ms");
  formatParam(modParamIndex(0, 0, 7), 0.5, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-");
  formatParam(modEngineParamIndex(0, 2), 3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Random / S&H");
  formatParam(modEngineParamIndex(0, 2), 25, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Off");
  formatParam(modScopeParamIndex(0, 2), 1, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "Per part");
  formatParam(routeAmountParamIndex(0, 0), 0.5 / 24.0, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "+0.50 st");
  formatParam(routeAmountParamIndex(0, 1), -0.3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-30%");
  formatParam(routeAmountParamIndex(0, 2), 0.3, buf, sizeof(buf), engines);
  REQUIRE(std::string(buf) == "-");  // an empty route
  formatParam(partParamIndex(0, PartParam::MpeMode), 2, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Upper zone");
  formatParam(partParamIndex(0, PartParam::CcB), 74, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "CC 74");
  formatParam(partParamIndex(0, PartParam::MpeBendRange), 48, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+/-48 st");
  formatParam(masterParamIndex(MasterParam::VoiceGain), 0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-12 dB");
  formatParam(masterParamIndex(MasterParam::SoftClip), 1, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "On");
  REQUIRE(clampParam(routeAmountParamIndex(0, 0), -3.0) == -1.0f);
  REQUIRE(clampParam(modScopeParamIndex(0, 0), 0.6) == 1.0f);
}
```

- [ ] **Step 2: Update the Filter host-default level test (`tests/test_levels.cpp`)**

In "Host defaults of the Filter and Shaper P's suit every engine of the slot", replace the loop that fills `hostDefaults` (from `for (int p = 0; p < int(slotParamCount(kind)); ++p) {` to its closing brace) with:

```cpp
    for (int p = 0; p < int(slotParamCount(kind)); ++p) {
      hostDefaults.p[p] = paramDefault(slotParamIndex(kind, 0, p), PluginKind::Effect);
      // One default per P position: the same for both parts and both plugins, except the
      // instrument's Filter P1, the init patch's cutoff (M4, provisional ruling 28).
      for (int part = 0; part < kNumParts; ++part) {
        for (PluginKind plugin : {PluginKind::Instrument, PluginKind::Effect}) {
          if (kind == SlotKind::Filter && p == 0 && plugin == PluginKind::Instrument) {
            REQUIRE(paramDefault(slotParamIndex(kind, part, p), plugin) == 0.7f);
            continue;
          }
          REQUIRE(paramDefault(slotParamIndex(kind, part, p), plugin) == hostDefaults.p[p]);
        }
      }
    }
```

and append:

```cpp
TEST_CASE("The instrument's init cutoff suits every filter", "[levels]") {
  for (int e = 0; e < kNumFilterEngines; ++e) {
    ProcessorSlot slot(makeAllFilters());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    ParamValues p;
    for (int i = 0; i < int(kFilterParamCount); ++i) {
      p.p[i] = paramDefault(filterParamIndex(0, i), PluginKind::Instrument);
    }
    const Level level = measure(slot, p);
    std::printf("Init cutoff: %-22s %+6.2f dB  peak %.3f\n", filterEngineInfo(e).displayName, level.gainDb,
                double(level.peak));
    REQUIRE(level.gainDb > -6.0);
    REQUIRE(level.gainDb < 3.0);
  }
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`kModSlotParamCount`, `modEngineParamIndex`, `initPatchRoute`, … not declared).

- [ ] **Step 4: Replace `core/synth/param_table.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/voice_engine.h"
#include "mod/mod_routes.h"
#include "synth/plugin_kind.h"

namespace eg13 {

constexpr int kNumParts = 2;
// Source P1..P16, Filter P1..P4, Shaper P1..P12 per part (spec §7.1; Shaper amended in M3), and
// per mod slot P1..P16 (M4). Engines use the first EngineInfo::paramCount; the rest are named
// "(unused)" and show "-".
constexpr size_t kSourceParamCount = 16;
constexpr size_t kFilterParamCount = 4;
constexpr size_t kShaperParamCount = 12;
constexpr size_t kModSlotParamCount = size_t(kModParamCount);

// The engine slots of a part: Source, Filter, Shaper, then the four mod slots (M4).
enum class SlotKind : int { Source = 0, Filter = 1, Shaper = 2, Mod1 = 3, Mod2 = 4, Mod3 = 5, Mod4 = 6 };
constexpr int kNumSlotKinds = 7;
constexpr bool isModSlot(SlotKind kind) { return int(kind) >= int(SlotKind::Mod1); }
constexpr SlotKind modSlotKind(int slot) { return SlotKind(int(SlotKind::Mod1) + slot); }
constexpr int modSlotIndex(SlotKind kind) { return int(kind) - int(SlotKind::Mod1); }

// Value = parameter-ID index within Slot::Part. Never renumber; append only.
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
  MpeMode,       // M4: 0 Off, 1 Lower zone, 2 Upper zone
  MpeBendRange,  // M4: semitones of a member channel's full pitch bend
  CcA,           // M4: the controller numbers of the CC A..D sources
  CcB,
  CcC,
  CcD,
};
constexpr size_t kPartParamCount = 21;

// Value = parameter-ID index for part 0xFF, Slot::Part. Never renumber; append only.
enum class MasterParam : uint32_t { Level = 0, Dry, Mode, SplitNote, VoiceGain, SoftClip };
constexpr size_t kMasterParamCount = 6;

enum class PartMode { Single = 0, Layer = 1, Split = 2, Multi = 3 };

constexpr size_t slotParamCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kSourceParamCount
         : kind == SlotKind::Filter ? kFilterParamCount
         : kind == SlotKind::Shaper ? kShaperParamCount
                                    : kModSlotParamCount;
}
// A slot's block in a part's table: its selector, its P's, and for a mod slot its Scope.
constexpr size_t slotBlockSize(SlotKind kind) { return 1 + slotParamCount(kind) + (isModSlot(kind) ? 1 : 0); }
// Where a slot's selector sits within a part's block.
constexpr size_t slotOffset(SlotKind kind) {
  size_t offset = 0;
  for (int k = 0; k < int(kind); ++k) offset += slotBlockSize(SlotKind(k));
  return offset;
}
// Per part: the seven slot blocks, the 12 route amounts, then the part settings. Table order
// only: saved state uses parameter IDs.
constexpr size_t kSlotsBlockSize = slotOffset(SlotKind::Mod4) + slotBlockSize(SlotKind::Mod4);
constexpr size_t kParamsPerPart = kSlotsBlockSize + size_t(kNumRoutes) + kPartParamCount;
constexpr size_t kParamCount = size_t(kNumParts) * kParamsPerPart + kMasterParamCount;

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
constexpr size_t modEngineParamIndex(int part, int slot) { return slotEngineParamIndex(modSlotKind(slot), part); }
constexpr size_t modParamIndex(int part, int slot, int p) { return slotParamIndex(modSlotKind(slot), part, p); }
constexpr size_t modScopeParamIndex(int part, int slot) {
  return modEngineParamIndex(part, slot) + 1 + kModSlotParamCount;
}
constexpr size_t routeAmountParamIndex(int part, int route) {
  return size_t(part) * kParamsPerPart + kSlotsBlockSize + size_t(route);
}
constexpr size_t partParamIndex(int part, PartParam param) {
  return size_t(part) * kParamsPerPart + kSlotsBlockSize + size_t(kNumRoutes) + size_t(uint32_t(param) - 1);
}
constexpr size_t masterParamIndex(MasterParam param) {
  return size_t(kNumParts) * kParamsPerPart + size_t(uint32_t(param));
}

enum class ParamFormat {
  Engine,  // a slot's P, formatted by the slot's selected engine
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
  ModEngine,    // M4
  ModScope,     // M4
  RouteAmount,  // M4: by the route's destination
  MpeMode,      // M4
  CcNumber,     // M4
  VoiceGain,    // M4
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

// The engine selected in each slot of a part, and each route's destination, for engine-aware
// names and text.
struct PartEngines {
  int source = 0;
  int filter = 0;
  int shaper = 0;
  int mod[kNumModSlots] = {};
  uint16_t routeDestination[kNumRoutes] = {};
};

int slotEngineCount(SlotKind kind);
int slotEngineFromSelector(SlotKind kind, double value);  // unknown -> 0 (Macro / Off)
const EngineInfo& slotEngineInfo(SlotKind kind, int engine);
// If index is a slot's P: its slot, part and P (0-based).
bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p);
// If index is a slot's engine selector: its slot and part.
bool slotSelectorOf(size_t index, SlotKind* kind, int* part);
// If index is a mod slot's Scope: its part and slot (0..3).
bool modScopeOf(size_t index, int* part, int* slot);
// If index is a route amount: its part and route (0..11).
bool routeAmountOf(size_t index, int* part, int* route);

// The init patches' routes (spec §6), patch data the Synth starts from, and their amounts (the
// route amounts' host defaults).
Route initPatchRoute(PluginKind plugin, int part, int route);
float initPatchAmount(PluginKind plugin, int part, int route);

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
float paramDefault(size_t index, PluginKind kind);
float clampParam(size_t index, double value);  // clamp, round stepped, NaN -> min
// engines: the part's selected engines and route destinations, for slot P and route text.
void formatParam(size_t index, double value, char* buf, size_t size, const PartEngines& engines = {});
void formatParam(size_t index, double value, char* buf, size_t size, int sourceEngine);
const char* paramName(size_t index, const PartEngines& engines = {});
const char* paramName(size_t index, int sourceEngine);
float glideSeconds(float normalized);  // 2 * v^2 seconds

}  // namespace eg13
```

- [ ] **Step 5: Replace `core/synth/param_table.cpp`**

```cpp
#include "synth/param_table.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/engine_registry.h"
#include "engine/shelves_engine.h"
#include "engine/sst_filter_engine.h"
#include "engine/tides_engine.h"
#include "engine/warps_engine.h"
#include "params/param_ids.h"

namespace eg13 {

static_assert(kSourceParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Source P");
static_assert(kShaperParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every Shaper P");
static_assert(kModSlotParamCount <= size_t(kMaxSlotParams), "ParamValues must hold every mod slot P");

namespace {

const char* const kSlotModules[kNumSlotKinds][kNumParts] = {
    {"Part A/Source", "Part B/Source"}, {"Part A/Filter", "Part B/Filter"}, {"Part A/Shaper", "Part B/Shaper"},
    {"Part A/Mod 1", "Part B/Mod 1"},   {"Part A/Mod 2", "Part B/Mod 2"},   {"Part A/Mod 3", "Part B/Mod 3"},
    {"Part A/Mod 4", "Part B/Mod 4"},
};
const char* const kPartModules[kNumParts] = {"Part A", "Part B"};
const char* const kMatrixModules[kNumParts] = {"Part A/Matrix", "Part B/Matrix"};
const char* const kRouteNames[kNumRoutes] = {"Route 1", "Route 2", "Route 3",  "Route 4",  "Route 5",  "Route 6",
                                             "Route 7", "Route 8", "Route 9", "Route 10", "Route 11", "Route 12"};

struct PartParamDef {
  PartParam param;
  const char* name;
  float minValue;
  float maxValue;
  float defaultValue;
  bool stepped;
  ParamFormat format;
};

// Stepped settings are integer plain values: a value keeps its meaning when an option is added at
// the end of a list (the maximum grows, nothing is renumbered).
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
    // M4. MPE: Off, Lower zone, Upper zone (append-only). CC A-D: provisional ruling 14.
    {PartParam::MpeMode, "MPE", 0, 2, 0, true, ParamFormat::MpeMode},
    {PartParam::MpeBendRange, "MPE Bend Range", 0, 96, 48, true, ParamFormat::BendRange},
    {PartParam::CcA, "CC A", 0, 127, 2, true, ParamFormat::CcNumber},
    {PartParam::CcB, "CC B", 0, 127, 4, true, ParamFormat::CcNumber},
    {PartParam::CcC, "CC C", 0, 127, 11, true, ParamFormat::CcNumber},
    {PartParam::CcD, "CC D", 0, 127, 16, true, ParamFormat::CcNumber},
};

params::Slot idSlot(SlotKind kind) {
  return kind == SlotKind::Source   ? params::Slot::Source
         : kind == SlotKind::Filter ? params::Slot::Filter
         : kind == SlotKind::Shaper ? params::Slot::Shaper
                                    : params::Slot(uint32_t(params::Slot::Mod1) + uint32_t(modSlotIndex(kind)));
}

// Default engine per slot, part and plugin: the init patches (spec §6). EG13 FX's Part A is
// Audio Input -> Analog Filter with an LFO in Mod 1; the instrument has the State Variable Filter
// (provisional ruling 28), envelopes in Mod 1 and 2 and an LFO in Mod 3. Everything else starts on
// the slot's first engine (Macro Oscillator, Off).
int defaultEngine(SlotKind kind, int part, PluginKind plugin) {
  if (plugin == PluginKind::Effect) {
    if (part != 0) return 0;
    if (kind == SlotKind::Source) return int(SourceEngine::AudioInput);
    if (kind == SlotKind::Filter) return int(FilterEngine::Analog);
    if (kind == SlotKind::Mod1) return int(ModEngineKind::Lfo);
    return 0;
  }
  if (kind == SlotKind::Filter) return int(FilterEngine::StateVariable);
  if (kind == SlotKind::Mod1 || kind == SlotKind::Mod2) return int(ModEngineKind::Envelope);
  if (kind == SlotKind::Mod3) return int(ModEngineKind::Lfo);
  return 0;
}

// The instrument init patch's cutoff, 70 % (20 Hz * 1000^0.7, about 2.5 kHz): Mod 2's +30 %
// opens it fully at the envelope's peak (spec §6; provisional ruling 28).
constexpr float kInstrumentCutoff = 0.7f;

// Host defaults of the Filter and Shaper P's (M3 final review I1; controller ruling, provisional).
// CLAP reports one default per parameter and cannot change it while active, so a host's
// reset-to-default sets the same value whatever engine is selected. Each value is chosen so
// that a slot reset as a whole gives a sensible, audible result on every engine of the slot
// (tests/test_levels.cpp, "Host defaults ..."). Engine switching still loads each engine's own
// stash and EngineInfo defaults; these values are only what the host shows as "default" and
// what a fresh project holds in a slot's P's.
//
// Filter: every filter's own defaults where they agree. P1 fully open (all three filters
// default to it; the instrument's init patch uses kInstrumentCutoff instead), P2 no resonance
// (the Analog Filter's and Classic Filters' default; the SVF reads it as Q 0.5, flat with a soft
// knee, rather than its own 0.707), P3 the first option (Analog Filter LP4, Classic Filters type
// 0 = 12 dB low-pass, SVF LP), P4 Character centred.
const float kFilterHostDefaults[kFilterParamCount] = {
    1.0f, 0.0f, 0.5f / float(SstFilterEngine::kTypeSteps), 0.5f};
// Shaper: the EQ flat (every gain 0 dB, pad off, main output) on P2, P4, P7, P10, P11, P12;
// where the EQ does not care (frequencies and Qs while their gain is 0 dB) the Ring Mod /
// Vocoder's own defaults: P1 Algorithm Analog RM, P3 Carrier Level 0.47, P5 Carrier Internal
// Osc, P6 Modulator Voice, P8 Osc Shape first, P9 Mode Modulation. The Ring Mod's other
// controls then sit at the EQ's 0.5: Timbre 0.5 and Osc Pitch 0 st (its own defaults) and Mod
// Level 0.5 (its default is 0.47, about 0.4 dB lower).
const float kShaperHostDefaults[kShaperParamCount] = {
    0.25f,
    0.5f,
    WarpsEngine::kDefaultLevel,
    0.5f,
    0.5f / float(WarpsEngine::kSelectorSteps),
    0.5f / float(WarpsEngine::kSelectorSteps),
    0.5f,
    0.5f / float(WarpsEngine::kNumOscillatorModes),
    0.25f,
    0.5f,
    0.25f,
    0.5f / float(ShelvesEngine::kNumOutputs),
};
// Mod slots (provisional ruling 1): one value per P position that suits every mod engine. Each
// engine places its controls so these are its own defaults: stepped lists default to their first
// option (0), times and levels as noted in each engine's header.
//   Envelope: Mode ADSR, Attack 1 ms, shapes linear, Decay and Release 0.5 s, Sustain 100 %.
//   LFO: Cycle, Sync off, 2 Hz, neutral shape/slope/smoothness, Shift 100 %, Phase outputs, Medium.
//   Random / S&H: VCV's defaults (coin toss, rate and biases centred, no deja vu or jitter), X Range
//   Full (its last option, at 100 %).
//   Function Generator: Envelope, fastest attack, 0.4 s decay and release, full sustain, 2.2 Hz
//   LFO, sine.
const float kModHostDefaults[kModSlotParamCount] = {0.0f, 0.0f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 0.5f,
                                                    0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

float modHostDefault(PluginKind plugin, int part, int slot, int p) {
  // Init patch LFO rates (provisional ruling 29): the instrument's Mod 3 vibrato at about 5 Hz,
  // EG13 FX's Mod 1 filter sweep at about 0.25 Hz. Every mod engine accepts any value here.
  if (p == 2 && plugin == PluginKind::Instrument && slot == 2) return TidesEngine::frequencyFor(5.0, TidesEngine::Medium);
  if (p == 2 && plugin == PluginKind::Effect && part == 0 && slot == 0) {
    return TidesEngine::frequencyFor(0.25, TidesEngine::Medium);
  }
  return kModHostDefaults[p];
}

float hostDefault(SlotKind kind, int part, int p, const EngineInfo& engine, PluginKind plugin) {
  if (kind == SlotKind::Filter) {
    return p == 0 && plugin == PluginKind::Instrument ? kInstrumentCutoff : kFilterHostDefaults[p];
  }
  if (kind == SlotKind::Shaper) return kShaperHostDefaults[p];
  if (isModSlot(kind)) return modHostDefault(plugin, part, modSlotIndex(kind), p);
  return p < engine.paramCount ? engine.params[p].defaultValue : 0.0f;  // Source: M2's rule
}

// A per-part LFO in EG13 FX's Mod 1; every other slot per voice (provisional ruling 2).
float scopeDefault(PluginKind plugin, int part, int slot) {
  return plugin == PluginKind::Effect && part == 0 && slot == 0 ? 1.0f : 0.0f;
}

int engineFor(SlotKind kind, const PartEngines& e) {
  if (isModSlot(kind)) return e.mod[modSlotIndex(kind)];
  return kind == SlotKind::Source ? e.source : kind == SlotKind::Filter ? e.filter : e.shaper;
}

ParamFormat selectorFormat(SlotKind kind) {
  return kind == SlotKind::Source   ? ParamFormat::SourceEngine
         : kind == SlotKind::Filter ? ParamFormat::FilterEngine
         : kind == SlotKind::Shaper ? ParamFormat::ShaperEngine
                                    : ParamFormat::ModEngine;
}

float selectorMax(SlotKind kind) {
  return float(kind == SlotKind::Source   ? kSourceEngineSelectorMax
               : kind == SlotKind::Filter ? kFilterEngineSelectorMax
               : kind == SlotKind::Shaper ? kShaperEngineSelectorMax
                                          : kModEngineSelectorMax);
}

const std::array<ParamSpec, kParamCount>& table() {
  static const std::array<ParamSpec, kParamCount> specs = [] {
    std::array<ParamSpec, kParamCount> t{};
    for (int part = 0; part < kNumParts; ++part) {
      for (int k = 0; k < kNumSlotKinds; ++k) {
        const SlotKind kind = SlotKind(k);
        const int instrument = defaultEngine(kind, part, PluginKind::Instrument);
        const int effect = defaultEngine(kind, part, PluginKind::Effect);
        const char* module = kSlotModules[k][part];
        // Engine selectors: a fixed range of 0..31 (unknown values select the slot's first
        // engine), so appending engines never changes the range or what a value means.
        t[slotEngineParamIndex(kind, part)] = {params::id(uint32_t(part), idSlot(kind), 0),
                                               kind == SlotKind::Source || isModSlot(kind) ? "Engine" : "Type",
                                               module, 0.0f, selectorMax(kind), float(instrument), float(effect),
                                               true, selectorFormat(kind)};
        const EngineInfo& inst = slotEngineInfo(kind, instrument);
        const EngineInfo& fx = slotEngineInfo(kind, effect);
        for (int p = 0; p < int(slotParamCount(kind)); ++p) {
          const bool used = p < inst.paramCount;
          t[slotParamIndex(kind, part, p)] = {params::id(uint32_t(part), idSlot(kind), uint32_t(p + 1)),
                                              used ? inst.params[p].name : "(unused)", module, 0.0f, 1.0f,
                                              hostDefault(kind, part, p, inst, PluginKind::Instrument),
                                              hostDefault(kind, part, p, fx, PluginKind::Effect), false,
                                              ParamFormat::Engine};
        }
        if (isModSlot(kind)) {
          // Scope: 0 per voice, 1 per part. RULE: a new scope is appended as 2 (max grows).
          const int slot = modSlotIndex(kind);
          t[modScopeParamIndex(part, slot)] = {params::id(uint32_t(part), idSlot(kind), 1 + uint32_t(kModSlotParamCount)),
                                               "Scope", module, 0.0f, 1.0f,
                                               scopeDefault(PluginKind::Instrument, part, slot),
                                               scopeDefault(PluginKind::Effect, part, slot), true,
                                               ParamFormat::ModScope};
        }
      }
      for (int r = 0; r < kNumRoutes; ++r) {
        t[routeAmountParamIndex(part, r)] = {params::id(uint32_t(part), params::Slot::Matrix, uint32_t(r + 1)),
                                             kRouteNames[r], kMatrixModules[part], -1.0f, 1.0f,
                                             initPatchAmount(PluginKind::Instrument, part, r),
                                             initPatchAmount(PluginKind::Effect, part, r), false,
                                             ParamFormat::RouteAmount};
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
    // Voice Gain: 0 = -12 dB (instrument), 1 = 0 dB (EG13 FX, and projects before M4). RULE: new
    // options are appended (max grows). Provisional ruling 24.
    t[masterParamIndex(MasterParam::VoiceGain)] = {master(MasterParam::VoiceGain), "Voice Gain", "Master", 0, 1,
                                                   0.0f, 1.0f, true, ParamFormat::VoiceGain};
    t[masterParamIndex(MasterParam::SoftClip)] = {master(MasterParam::SoftClip), "Soft Clip", "Master", 0, 1,
                                                  1.0f, 1.0f, true, ParamFormat::OnOff};
    return t;
  }();
  return specs;
}

const char* pick(const char* const* names, int count, float value) {
  const int i = std::clamp(int(std::lround(value)), 0, count - 1);
  return names[i];
}

}  // namespace

Route initPatchRoute(PluginKind plugin, int part, int route) {
  if (plugin == PluginKind::Effect) {
    // Mod 1's LFO (per part) -> Filter P1 (spec §6).
    if (part == 0 && route == 0) return {modOutputSource(0, 0), uint16_t(kDestFilterP + 0), kSrcNone};
    return {};
  }
  switch (route) {  // both parts (spec §6)
    case 0: return {modOutputSource(0, 0), kDestAmp, kSrcVelocity};       // Mod 1 -> Amp, via velocity
    case 1: return {modOutputSource(1, 0), uint16_t(kDestFilterP + 0), kSrcNone};  // Mod 2 -> Filter P1
    case 2: return {modOutputSource(2, 0), kDestPitch, kSrcModWheel};     // Mod 3 -> pitch, via mod wheel
    default: return {};
  }
}

float initPatchAmount(PluginKind plugin, int part, int route) {
  if (plugin == PluginKind::Effect) return part == 0 && route == 0 ? 0.2f : 0.0f;  // +20 %
  switch (route) {
    case 0: return 1.0f;                          // 100 %
    case 1: return 0.3f;                          // +30 %
    case 2: return 0.5f / kPitchRangeSemitones;   // +-0.5 semitone
    default: return 0.0f;
  }
}

int slotEngineCount(SlotKind kind) {
  return kind == SlotKind::Source   ? kNumSourceEngines
         : kind == SlotKind::Filter ? kNumFilterEngines
         : kind == SlotKind::Shaper ? kNumShaperEngines
                                    : kNumModEngines;
}

int slotEngineFromSelector(SlotKind kind, double value) {
  return kind == SlotKind::Source   ? sourceEngineFromSelector(value)
         : kind == SlotKind::Filter ? filterEngineFromSelector(value)
         : kind == SlotKind::Shaper ? shaperEngineFromSelector(value)
                                    : modEngineFromSelector(value);
}

const EngineInfo& slotEngineInfo(SlotKind kind, int engine) {
  return kind == SlotKind::Source   ? sourceEngineInfo(engine)
         : kind == SlotKind::Filter ? filterEngineInfo(engine)
         : kind == SlotKind::Shaper ? shaperEngineInfo(engine)
                                    : modEngineInfo(engine).engine;
}

bool slotParamOf(size_t index, SlotKind* kind, int* part, int* p) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  for (int k = 0; k < kNumSlotKinds; ++k) {
    const size_t first = slotOffset(SlotKind(k)) + 1;
    if (local >= first && local < first + slotParamCount(SlotKind(k))) {
      *kind = SlotKind(k);
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
  for (int k = 0; k < kNumSlotKinds; ++k) {
    if (local == slotOffset(SlotKind(k))) {
      *kind = SlotKind(k);
      *part = int(index / kParamsPerPart);
      return true;
    }
  }
  return false;
}

bool modScopeOf(size_t index, int* part, int* slot) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  for (int s = 0; s < kNumModSlots; ++s) {
    if (index % kParamsPerPart == modScopeParamIndex(0, s)) {
      *part = int(index / kParamsPerPart);
      *slot = s;
      return true;
    }
  }
  return false;
}

bool routeAmountOf(size_t index, int* part, int* route) {
  if (index >= size_t(kNumParts) * kParamsPerPart) return false;
  const size_t local = index % kParamsPerPart;
  if (local < kSlotsBlockSize || local >= kSlotsBlockSize + size_t(kNumRoutes)) return false;
  *part = int(index / kParamsPerPart);
  *route = int(local - kSlotsBlockSize);
  return true;
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
  static const char* const kScopes[2] = {"Per voice", "Per part"};
  static const char* const kMpeModes[3] = {"Off", "Lower zone", "Upper zone"};
  static const char* const kVoiceGains[2] = {"-12 dB", "0 dB"};

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
    case ParamFormat::ModEngine:
      std::snprintf(buf, size, "%s", modEngineInfo(modEngineFromSelector(v)).engine.displayName);
      break;
    case ParamFormat::ModScope:
      std::snprintf(buf, size, "%s", pick(kScopes, 2, v));
      break;
    case ParamFormat::RouteAmount: {
      int part = 0, route = 0;
      routeAmountOf(index, &part, &route);
      const uint16_t destination = engines.routeDestination[route];
      if (destinationKind(destination) == DestinationKind::None) {
        std::snprintf(buf, size, "-");
      } else if (destination == kDestPitch) {
        std::snprintf(buf, size, "%+.2f st", double(v * kPitchRangeSemitones));
      } else {
        std::snprintf(buf, size, "%+d%%", int(std::lround(v * 100.0f)));
      }
      break;
    }
    case ParamFormat::MpeMode:
      std::snprintf(buf, size, "%s", pick(kMpeModes, 3, v));
      break;
    case ParamFormat::CcNumber:
      std::snprintf(buf, size, "CC %d", int(v));
      break;
    case ParamFormat::VoiceGain:
      std::snprintf(buf, size, "%s", pick(kVoiceGains, 2, v));
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

- [ ] **Step 6: Extend the Synth's stash to the mod slots (`core/synth/synth.h`, `core/synth/synth.cpp`)**

In `core/synth/synth.h`, replace everything from the comment `// Every engine's remembered parameters, per part and slot:` down to (not including) `using EngineStash = std::array<float, kStashSize>;` with:

```cpp
// Every engine's remembered parameters, per part and slot: Source engines' P1..P16 first (M2's
// layout, so stashIndex() is unchanged), then Filter engines' P1..P4, Shaper engines' P1..P12,
// mod engines' P1..P16 per mod slot (M4), then each mod engine's Scope per mod slot (M4).
constexpr size_t kSourceStashSize = size_t(kNumParts) * size_t(kNumSourceEngines) * kSourceParamCount;
constexpr size_t kFilterStashSize = size_t(kNumParts) * size_t(kNumFilterEngines) * kFilterParamCount;
constexpr size_t kShaperStashSize = size_t(kNumParts) * size_t(kNumShaperEngines) * kShaperParamCount;
constexpr size_t kModStashSize =
    size_t(kNumParts) * size_t(kNumModSlots) * size_t(kNumModEngines) * kModSlotParamCount;
constexpr size_t kSlotStashSize = kSourceStashSize + kFilterStashSize + kShaperStashSize + kModStashSize;
constexpr size_t kScopeStashSize = size_t(kNumParts) * size_t(kNumModSlots) * size_t(kNumModEngines);
constexpr size_t kStashSize = kSlotStashSize + kScopeStashSize;
constexpr size_t stashIndex(int part, int engine, int p) {
  return (size_t(part) * size_t(kNumSourceEngines) + size_t(engine)) * kSourceParamCount + size_t(p);
}
constexpr size_t slotStashIndex(SlotKind kind, int part, int engine, int p) {
  return kind == SlotKind::Source ? stashIndex(part, engine, p)
         : kind == SlotKind::Filter
             ? kSourceStashSize +
                   (size_t(part) * size_t(kNumFilterEngines) + size_t(engine)) * kFilterParamCount + size_t(p)
         : kind == SlotKind::Shaper
             ? kSourceStashSize + kFilterStashSize +
                   (size_t(part) * size_t(kNumShaperEngines) + size_t(engine)) * kShaperParamCount + size_t(p)
             : kSourceStashSize + kFilterStashSize + kShaperStashSize +
                   ((size_t(part) * size_t(kNumModSlots) + size_t(modSlotIndex(kind))) * size_t(kNumModEngines) +
                    size_t(engine)) * kModSlotParamCount +
                   size_t(p);
}
// A mod engine's remembered Scope (0 per voice, 1 per part) in one mod slot of one part.
constexpr size_t scopeStashIndex(int part, int slot, int engine) {
  return kSlotStashSize + (size_t(part) * size_t(kNumModSlots) + size_t(slot)) * size_t(kNumModEngines) + size_t(engine);
}
// The layout covers the stash exactly: each slot's block starts where the previous ends, and the
// last entry is the last index.
static_assert(slotStashIndex(SlotKind::Filter, 0, 0, 0) == kSourceStashSize, "Filter block starts after Source");
static_assert(slotStashIndex(SlotKind::Shaper, 0, 0, 0) == kSourceStashSize + kFilterStashSize,
              "Shaper block starts after Filter");
static_assert(slotStashIndex(SlotKind::Mod1, 0, 0, 0) == kSourceStashSize + kFilterStashSize + kShaperStashSize,
              "Mod block starts after Shaper");
static_assert(slotStashIndex(SlotKind::Source, kNumParts - 1, kNumSourceEngines - 1, int(kSourceParamCount) - 1) ==
                  kSourceStashSize - 1, "Source block size");
static_assert(slotStashIndex(SlotKind::Filter, kNumParts - 1, kNumFilterEngines - 1, int(kFilterParamCount) - 1) ==
                  kSourceStashSize + kFilterStashSize - 1, "Filter block size");
static_assert(slotStashIndex(SlotKind::Shaper, kNumParts - 1, kNumShaperEngines - 1, int(kShaperParamCount) - 1) ==
                  kSourceStashSize + kFilterStashSize + kShaperStashSize - 1, "Shaper block size");
static_assert(slotStashIndex(SlotKind::Mod4, kNumParts - 1, kNumModEngines - 1, int(kModSlotParamCount) - 1) ==
                  kSlotStashSize - 1, "Mod block size");
static_assert(scopeStashIndex(kNumParts - 1, kNumModSlots - 1, kNumModEngines - 1) == kStashSize - 1,
              "Scope block size");
```

Replace the `kMaxHostUpdates` definition and its comment with:

```cpp
// Host-update queue capacity, coalesced per id: per part every slot P, each mod slot's Scope, and
// the four learnable CC numbers (M4).
constexpr size_t kMaxHostUpdates =
    size_t(kNumParts) * (kSourceParamCount + kFilterParamCount + kShaperParamCount +
                         size_t(kNumModSlots) * (kModSlotParamCount + 1) + 4);
```

In `core/synth/synth.cpp`:

- In `Synth::defaultStash()`, before `return stash;`, add:

```cpp
  // Each mod engine's own scope (spec §4: Random / S&H per part, the others per voice).
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      for (int e = 0; e < kNumModEngines; ++e) {
        stash[scopeStashIndex(part, slot, e)] = modEngineInfo(e).perPartByDefault ? 1.0f : 0.0f;
      }
    }
  }
```

- In `Synth::engineStash()`, before `return s;`, add:

```cpp
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      const int e = engine(modSlotKind(slot), part);
      s[scopeStashIndex(part, slot, e)] = float(param(paramSpec(modScopeParamIndex(part, slot)).id));
    }
  }
```

- In `Synth::partEngines()`, before `return e;`, add `for (int slot = 0; slot < kNumModSlots; ++slot) e.mod[slot] = engine(modSlotKind(slot), part);`.

- In `Synth::switchEngine()`, between the P loop and `current = next;`, add:

```cpp
  if (isModSlot(kind)) {  // a mod engine remembers its scope too
    const int slot = modSlotIndex(kind);
    const size_t index = modScopeParamIndex(part, slot);
    stash_[scopeStashIndex(part, slot, previous)].store(load(index), std::memory_order_relaxed);
    const float value = stash_[scopeStashIndex(part, slot, next)].load(std::memory_order_relaxed);
    values_[index].store(value, std::memory_order_relaxed);
    queueHostUpdate({paramSpec(index).id, value});
  }
```

- In `Synth::readPartSettings()`, before `return s;`, add:

```cpp
  for (int slot = 0; slot < kNumModSlots; ++slot) {
    s.modEngine[slot] = slotEngineFromSelector(modSlotKind(slot), load(modEngineParamIndex(part, slot)));
    s.modPerPart[slot] = load(modScopeParamIndex(part, slot)) >= 0.5f;
  }
```

`setParam`, `adoptEngines`, `loadParams` and `applyPending` already loop over every `SlotKind`, so mod selectors switch at the selector event and loaded snapshots adopt their mod engines without further change.

- [ ] **Step 7: Save the mod slots' settings (`core/state/state.cpp`)**

Replace the `kSections` array with:

```cpp
const Section kSections[kNumSlotKinds] = {
    {SlotKind::Source, "engineSettings"},  // M2's key
    {SlotKind::Filter, "filterSettings"},
    {SlotKind::Shaper, "shaperSettings"},
    {SlotKind::Mod1, "mod1Settings"},  // M4: P1..P16, then the Scope
    {SlotKind::Mod2, "mod2Settings"},
    {SlotKind::Mod3, "mod3Settings"},
    {SlotKind::Mod4, "mod4Settings"},
};

// Numbers per engine in a section: the P's, and for a mod slot its Scope last.
size_t sectionWidth(SlotKind kind) { return slotParamCount(kind) + (isModSlot(kind) ? 1 : 0); }
```

In `readSection`, change `const size_t count = slotParamCount(section.kind);` to `const size_t count = sectionWidth(section.kind);`, and replace the loop that stores the values with:

```cpp
      for (size_t p = 0; p < list.size(); ++p) {
        const double v = list[p].get<double>();
        const float clamped = float(std::clamp(std::isnan(v) ? 0.0 : v, 0.0, 1.0));
        if (p < slotParamCount(section.kind)) {
          stash[slotStashIndex(section.kind, int(part), int(engine), int(p))] = clamped;
        } else {  // a mod slot's Scope: 0 per voice, 1 per part
          stash[scopeStashIndex(int(part), modSlotIndex(section.kind), int(engine))] = std::round(clamped);
        }
      }
```

In `StateStore::save`, after the loop that pushes an engine's P values, add:

```cpp
        if (isModSlot(section.kind)) values.push_back(stash[scopeStashIndex(part, modSlotIndex(section.kind), e)]);
```

The schema stays 4 here; Task 16 moves to schema 5 with routes, envelope shapes and the migration. An M3 build reading these sections keeps them as unknown data.

- [ ] **Step 8: Update and add the Synth and State tests**

In `tests/test_synth.cpp`, in "Filter and Shaper names and text follow their engines", replace the first two lines of the body with:

```cpp
  Synth synth(PluginKind::Instrument);
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Cutoff");  // the init patch's SVF
  synth.setParam(filterTypeId(0), double(FilterEngine::Analog));
  REQUIRE(std::string(synth.paramName(filterParamIndex(0, 0))) == "Frequency");
```

and append:

```cpp
TEST_CASE("Switching a mod engine while notes sound swaps its P values and scope") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  runBlocks(synth, 2);
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  const uint32_t engineId = paramSpec(modEngineParamIndex(0, 0)).id;
  const uint32_t scopeId = paramSpec(modScopeParamIndex(0, 0)).id;
  const uint32_t p4 = paramSpec(modParamIndex(0, 0, 3)).id;
  synth.setParam(p4, 0.8);  // the Envelope's Decay
  synth.setParam(engineId, double(ModEngineKind::Random));
  runBlocks(synth, 1);
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == int(kModSlotParamCount) + 1);
  REQUIRE(updates[int(kModSlotParamCount)].id == scopeId);
  REQUIRE(updates[int(kModSlotParamCount)].value == 1.0f);  // Random / S&H starts per part
  REQUIRE(synth.param(scopeId) == 1.0);
  REQUIRE(synth.param(p4) == double(modEngineInfo(int(ModEngineKind::Random)).engine.params[3].defaultValue));
  REQUIRE(std::string(synth.paramName(modParamIndex(0, 0, 0))) == "T Model");
  REQUIRE(synth.takeEngineChanged());
  synth.setParam(scopeId, 0.0);  // per voice
  synth.setParam(engineId, double(ModEngineKind::Envelope));
  runBlocks(synth, 1);
  REQUIRE(synth.param(p4) == double(0.8f));
  REQUIRE(synth.param(scopeId) == 0.0);
  synth.setParam(engineId, double(ModEngineKind::Random));
  REQUIRE(synth.param(scopeId) == 0.0);  // Random remembers the scope it was given
  REQUIRE(std::string(synth.paramName(modParamIndex(0, 0, 0))) == "T Model");
  char buf[64];
  synth.formatParam(modParamIndex(0, 0, 13), modOptionValue(1), buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Minor");
  synth.setActive(false);
}
```

In `tests/test_state.cpp`, in "Schema 3 without engineSettings uses each engine's defaults", replace the final `for` loop with:

```cpp
  // The engines in use report their live values (a host default may differ from the engine's own,
  // e.g. the init patch's cutoff); every other engine holds its own defaults.
  for (size_t i = 0; i < kSlotStashSize; ++i) {
    if (!currentEntry(s, i)) REQUIRE(stash[i] == defaults[i]);
  }
```

In "Filter and Shaper settings: wrong types, out-of-range values and Off", section "values outside 0..1 clamp", use engine 2 (Classic Filters) instead of 3 (the instrument's current State Variable Filter, whose entries now report live values): change `"filterSettings":{"1":{"3":[-0.5,1.5,0.25,7]}}` to `"filterSettings":{"1":{"2":[-0.5,1.5,0.25,7]}}` and the four `slotStashIndex(SlotKind::Filter, 1, 3, …)` to `slotStashIndex(SlotKind::Filter, 1, 2, …)`.

Append:

```cpp
TEST_CASE("Mod slot settings and scopes round-trip per engine") {
  Synth a(PluginKind::Instrument);
  const uint32_t engineId = paramSpec(modEngineParamIndex(1, 2)).id;
  a.setParam(engineId, double(ModEngineKind::FunctionGenerator));
  a.setParam(paramSpec(modParamIndex(1, 2, 4)).id, 0.33);
  a.setParam(paramSpec(modScopeParamIndex(1, 2)).id, 1.0);
  a.setParam(engineId, double(ModEngineKind::Random));  // stashes the Function Generator's settings
  a.setParam(paramSpec(modScopeParamIndex(1, 2)).id, 0.0);
  const std::string json = state::StateStore{}.save(a);
  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  REQUIRE(b.engine(SlotKind::Mod3, 1) == int(ModEngineKind::Random));
  REQUIRE(b.param(paramSpec(modScopeParamIndex(1, 2)).id) == 0.0);
  const EngineStash stash = b.engineStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Mod3, 1, int(ModEngineKind::FunctionGenerator), 4)] == 0.33f);
  REQUIRE(stash[scopeStashIndex(1, 2, int(ModEngineKind::FunctionGenerator))] == 1.0f);
  REQUIRE(stash[scopeStashIndex(1, 2, int(ModEngineKind::Random))] == 0.0f);
  b.setParam(engineId, double(ModEngineKind::FunctionGenerator));
  REQUIRE(b.param(paramSpec(modParamIndex(1, 2, 4)).id) == double(0.33f));
  REQUIRE(b.param(paramSpec(modScopeParamIndex(1, 2)).id) == 1.0);
}

TEST_CASE("mod settings arrays have 17 numbers; a scope is 0 or 1") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.1,0.7]}}})") ==
          state::LoadResult::Ok);
  const EngineStash stash = s.engineStash();
  REQUIRE(stash[slotStashIndex(SlotKind::Mod2, 0, 4, 15)] == 0.1f);
  REQUIRE(stash[scopeStashIndex(0, 1, 4)] == 1.0f);  // 0.7 rounds to per part
  REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{},"mod2Settings":{"0":{"4":[0.1,0.1]}}})") ==
          state::LoadResult::OkWithWarnings);
}
```

- [ ] **Step 9: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: every test passes. A planning build of Tasks 1–13 exactly as written here passed the whole suite (494 test cases). If a test written before M4 fails, it is one this step's list should have covered: report it rather than loosening it.

- [ ] **Step 10: Document the host view of modulation**

Append to `docs/manual/modulation.md`:

````markdown
## In the host

- *Part A/Mod 1* … *Part B/Mod 4*: **Engine** (a stepped list from 0 to 31; values without an engine select Off), **Scope** (*Per voice* / *Per part*) and P1–P16, named after the engine in the slot (unused ones show "(unused)" and "-").
- *Part A/Matrix*, *Part B/Matrix*: **Route 1** … **Route 12**, the routes' amounts from −100 % to +100 %. The text follows the route's destination: semitones for Pitch, percent otherwise, "-" for a route that is not set up.
- Per part: **MPE** (*Off*, *Lower zone*, *Upper zone*), **MPE Bend Range** (default ±48 semitones), **CC A** to **CC D** (the controller numbers of the four CC sources; defaults 2 Breath, 4 Foot, 11 Expression, 16).
- Master: **Voice Gain** (*−12 dB* / *0 dB*) and **Soft Clip** (*Off* / *On*), see Levels.

**Reset to default** in the host sets a mod slot's P to the same value whatever engine is in the slot (a plugin can give each parameter only one default). The values suit every engine: P1 and P2 go to 0 (each engine's first option, or its shortest attack), P3 to P9 to 50 % except P6 to 100 %, and P10 to P16 to 0. That gives the Envelope's and Function Generator's ADSR (1 ms attack, about half-second decay and release, full sustain), the LFO cycling at 2 Hz, and Random / S&H at the module's centre settings with a bipolar range. Exceptions: in the instrument, Mod 3's P3 resets to about 5 Hz (the init patch's vibrato), and in EG13 FX, Part A Mod 1's P3 to about 0.25 Hz. Scope resets to per voice (EG13 FX's Part A Mod 1: per part). A route amount resets to its init patch value (0 for routes the init patch does not use).
````

In `docs/manual/engines.md`, replace the paragraph that begins "Defaults: every part starts with Filter *Type* **Off**" with:

```markdown
Defaults: in the instrument, every part starts with Filter *Type* **State Variable Filter** at a cutoff of 70 % (about 2.5 kHz; the init patch's envelope opens it, see [Modulation](modulation.md)) and Shaper *Type* **Off**. EG13 FX's Part A starts with Filter *Type* set to **Analog Filter** (with the Analog Filter's own default controls), after the Audio Input source; its Part B starts with Filter and Shaper **Off**. Choosing an engine gives it its own default controls (or, later in a project, the settings it had when you last left it).
```

and in the first bullet of "Reset to default", replace "**Filter** P1 to P4: 100 %, 0 %, the first option, 50 %." with "**Filter** P1 to P4: 100 % (in the instrument 70 %, the init patch's cutoff), 0 %, the first option, 50 %.".

- [ ] **Step 11: Commit**

```bash
git add core/synth core/state/state.cpp tests/test_param_table.cpp tests/test_levels.cpp tests/test_synth.cpp tests/test_state.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Mod slots as host parameters: table v5, per-engine stash with scope, saved settings"
```

---
### Task 14: Patch data (routes, envelope shapes), the init patches' routes, state schema 5

**Files:**
- Modify: `core/synth/synth.h`, `core/synth/synth.cpp`, `core/state/state.h`, `core/state/state.cpp`, `docs/manual/parts-and-routing.md`
- Test: `tests/test_synth.cpp`, `tests/test_state.cpp`

**Interfaces:**
- Consumes: `Route`, `RouteSet` codes (Task 8), `EnvelopeShape`, `sanitized` (Task 2), `initPatchRoute` (Task 13), the mod stash sections (Task 13).
- Produces (`core/synth/synth.h`): `struct PatchData { std::array<std::array<Route, 12>, 2> routes; std::array<std::array<EnvelopeShape, 4>, 2> shapes; };` `static PatchData Synth::defaultPatch(PluginKind); PatchData Synth::patch() const; const PatchData& Synth::appliedPatch() const; void Synth::setRoute(int part, int route, const Route&); void Synth::setEnvelopeShape(int part, int slot, const EnvelopeShape&); void Synth::loadParams(const ParamSnapshot&, const EngineStash* = nullptr, const PatchData* = nullptr);`. `state::kSchemaVersion = 5`; state keys `routes` and `envelopeShapes`.

Routes and Custom shapes are patch data (spec §7.1): saved with the project, set by the core API (the editor arrives in M6), not host parameters. Thread safety follows the `pendingState_` pattern exactly (Global Constraints): the main thread keeps `mainPatch_`; a state load publishes its patch together with its values (one pending bundle, so a load's routes and parameters reach the audio thread in the same block); a route edit publishes a copy through `editState_`, the same four-state protocol; a load cancels an edit still waiting (it is older), and the audio thread applies a waiting load before a waiting edit (the edit is newer). Nothing on the audio thread allocates or waits.

The Synth still plays the Part's unmodulated path here: Task 15 switches it.

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_synth.cpp`:

```cpp

TEST_CASE("Routes and parameters of a load apply in the same block") {
  Synth synth(PluginKind::Instrument);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  runBlocks(synth, 1);
  synth.setRoute(0, 5, Route{kSrcGate, kDestPan, kSrcNone});  // an edit, not yet taken
  PatchData loaded = Synth::defaultPatch(PluginKind::Instrument);
  loaded.routes[1][2] = Route{kSrcRandom, uint16_t(kDestSourceP + 3), kSrcNone};
  ParamSnapshot values = synth.defaultSnapshot();
  values[routeAmountParamIndex(1, 2)] = -0.4f;
  synth.loadParams(values, nullptr, &loaded);  // supersedes the edit
  REQUIRE(synth.patch().routes[1][2] == loaded.routes[1][2]);
  REQUIRE(synth.appliedPatch().routes[1][2] == initPatchRoute(PluginKind::Instrument, 1, 2));  // not yet
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  REQUIRE(synth.appliedPatch().routes[0][5] == Route{});  // the older edit was dropped
  REQUIRE(synth.param(paramSpec(routeAmountParamIndex(1, 2)).id) == double(-0.4f));
  synth.setRoute(0, 6, Route{kSrcNote, kDestPitch, kSrcNone});  // an edit after the load
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[0][6] == Route{kSrcNote, kDestPitch, kSrcNone});
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  // A load and an edit made right after it reach the audio thread in the same block, in order.
  synth.loadParams(values, nullptr, &loaded);
  synth.setRoute(0, 7, Route{kSrcGate, kDestAmp, kSrcNone});
  synth.beginBlock();
  REQUIRE(synth.appliedPatch().routes[0][7] == Route{kSrcGate, kDestAmp, kSrcNone});
  REQUIRE(synth.appliedPatch().routes[0][6] == Route{});  // the load replaced the earlier edit
  REQUIRE(synth.appliedPatch().routes[1][2] == loaded.routes[1][2]);
  synth.setActive(false);
}

TEST_CASE("A route amount's text follows the route's destination") {
  Synth synth(PluginKind::Instrument);
  char buf[64];
  synth.formatParam(routeAmountParamIndex(0, 2), 0.5 / 24.0, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+0.50 st");  // the init patch's vibrato route
  synth.formatParam(routeAmountParamIndex(0, 1), 0.3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+30%");
  synth.formatParam(routeAmountParamIndex(0, 7), 0.3, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "-");
  synth.setRoute(0, 7, Route{kSrcVelocity, kDestPitch, kSrcNone});
  synth.formatParam(routeAmountParamIndex(0, 7), 0.25, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "+6.00 st");
  REQUIRE(synth.takeEngineChanged());  // the host is told to refresh the text
}
```

In `tests/test_state.cpp`, change the three `"\"schemaVersion\":4"` searches to `"\"schemaVersion\":5"`, rename the test "Saved state is schema 4 and carries every engine's settings" to "Saved state is schema 5 and carries every engine's settings", and append:

```cpp

TEST_CASE("Saved state is schema 5 and carries routes and envelope shapes") {
  Synth a(PluginKind::Instrument);
  a.setRoute(1, 7, Route{kSrcModWheel, uint16_t(kDestShaperP + 2), kSrcVelocity});
  EnvelopeShape shape;
  shape.count = 5;
  shape.type[4] = EnvelopeShape::Step;
  shape.loop[1] = 1;
  a.setEnvelopeShape(0, 3, shape);
  const std::string json = state::StateStore{}.save(a);
  Synth b(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(b, json) == state::LoadResult::Ok);
  const PatchData patch = b.patch();
  REQUIRE(patch.routes[1][7] == Route{kSrcModWheel, uint16_t(kDestShaperP + 2), kSrcVelocity});
  REQUIRE(patch.routes[0][0] == initPatchRoute(PluginKind::Instrument, 0, 0));  // the init patch's, saved
  REQUIRE(patch.shapes[0][3] == shape);
  REQUIRE(patch.shapes[1][3] == EnvelopeShape{});
  REQUIRE(b.appliedPatch().routes[1][7] == patch.routes[1][7]);  // inactive: applied at once
}

TEST_CASE("Schema 4 projects load with no modulation at their old level") {
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    Synth s(kind);
    state::StateStore store;
    REQUIRE(store.load(s, R"({"schemaVersion":4,"params":{}})") == state::LoadResult::Ok);
    for (int part = 0; part < kNumParts; ++part) {
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        REQUIRE(s.engine(modSlotKind(slot), part) == int(ModEngineKind::Off));
        REQUIRE(s.param(paramSpec(modScopeParamIndex(part, slot)).id) == 0.0);
      }
      for (int r = 0; r < kNumRoutes; ++r) {
        REQUIRE(s.patch().routes[size_t(part)][size_t(r)] == Route{});
        REQUIRE(s.param(paramSpec(routeAmountParamIndex(part, r)).id) == 0.0);
      }
    }
    REQUIRE(s.param(paramSpec(masterParamIndex(MasterParam::VoiceGain)).id) == 1.0);  // 0 dB
    REQUIRE(s.param(paramSpec(masterParamIndex(MasterParam::SoftClip)).id) == 0.0);   // off
  }
}

TEST_CASE("Malformed routes and envelope shapes are dropped with a warning") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  const char* json =
      R"({"schemaVersion":5,"params":{},"routes":{"0":[[9,2,0],[1,"x",0],[1,2],[70000,1,0]],"5":[],"1":7},)"
      R"("envelopeShapes":{"0":[{"count":9,"types":[0,1,2,7,0,0],"loops":[0,1,0,0,0,3]},{"count":2},5]}})";
  REQUIRE(store.load(s, json) == state::LoadResult::OkWithWarnings);
  const PatchData patch = s.patch();
  REQUIRE(patch.routes[0][0] == Route{kSrcModWheel, kDestAmp, kSrcNone});
  REQUIRE(patch.routes[0][1] == Route{});
  REQUIRE(patch.routes[0][2] == Route{});
  REQUIRE(patch.routes[0][3] == Route{});
  REQUIRE(patch.routes[1][0] == Route{});
  REQUIRE(patch.shapes[0][0].count == 6);                      // clamped
  REQUIRE(patch.shapes[0][0].type[3] == EnvelopeShape::Ramp);  // unknown type
  REQUIRE(patch.shapes[0][0].loop[5] == 1);
  REQUIRE(patch.shapes[0][1] == EnvelopeShape{});  // malformed: the default
}

TEST_CASE("Routes with codes from a newer version come back unchanged on resave") {
  Synth s(PluginKind::Instrument);
  state::StateStore store;
  REQUIRE(store.load(s, R"({"schemaVersion":5,"params":{},"routes":{"0":[[30,200,17]]}})") ==
          state::LoadResult::Ok);
  REQUIRE(s.patch().routes[0][0] == Route{30, 200, 17});
  REQUIRE_FALSE(routeActive(s.patch().routes[0][0]));  // ignored by this version
  Synth again(PluginKind::Instrument);
  REQUIRE(store.load(again, store.save(s)) == state::LoadResult::Ok);
  REQUIRE(again.patch().routes[0][0] == Route{30, 200, 17});
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`PatchData`, `setRoute`, `appliedPatch`, … not declared).

- [ ] **Step 3: Patch data in the Synth (`core/synth/synth.h`)**

Add `#include "mod/mod_routes.h"` after `#include "engine/engine_registry.h"`. Before `struct HostUpdate {`, add:

```cpp
// Patch data (spec §7.1): what is saved with a project but is not a host parameter. Each part's
// 12 routes (source, destination, via; the amounts are host parameters) and each mod slot's
// Custom envelope shape.
struct PatchData {
  std::array<std::array<Route, kNumRoutes>, kNumParts> routes{};
  std::array<std::array<EnvelopeShape, kNumModSlots>, kNumParts> shapes{};
};
```

Replace the `loadParams` declaration and its comment with:

```cpp
  // Main thread. Applied whole at the next beginBlock/process when active, with the patch data.
  // A stash replaces every engine's remembered settings; without one they reset. Without patch
  // data the plugin's init patch routes apply (defaultPatch()).
  void loadParams(const ParamSnapshot& values, const EngineStash* stash = nullptr, const PatchData* patch = nullptr);
```

After `static EngineStash defaultStash();  // each engine's own defaults`, add:

```cpp
  static PatchData defaultPatch(PluginKind kind);  // the init patch's routes; ADSR custom shapes
  // Main thread. The patch data as the main thread last set it (applied to the audio at the next
  // block when active).
  PatchData patch() const { return mainPatch_; }
  const PatchData& appliedPatch() const { return patch_; }  // tests: the audio thread's copy
  void setRoute(int part, int route, const Route& value);
  void setEnvelopeShape(int part, int slot, const EnvelopeShape& shape);
```

After `void applyPending();` add `void publishPatch();  // main thread: mainPatch_ to the audio thread`. After `std::vector<float> feedRing_[2];  // B's output for A, delayed by feedDelay_` add:

```cpp
  // Patch data: the audio thread's copy, the main thread's, the copy a load publishes with its
  // values, and a main-thread edit waiting for the audio thread (same states as pendingState_).
  PatchData patch_;
  PatchData mainPatch_;
  PatchData pendingPatch_;
  PatchData editPatch_;
  std::atomic<int> editState_{kIdle};
```

- [ ] **Step 4: Patch data in the Synth (`core/synth/synth.cpp`)**

Replace the head of `Synth::loadParams` (its signature down to the end of the `if (!active_ ...) { ... return; }` block) with:

```cpp
PatchData Synth::defaultPatch(PluginKind kind) {
  PatchData patch;
  for (int part = 0; part < kNumParts; ++part) {
    for (int r = 0; r < kNumRoutes; ++r) patch.routes[size_t(part)][size_t(r)] = initPatchRoute(kind, part, r);
  }
  return patch;  // envelope shapes: EnvelopeShape's default, an ADSR
}

void Synth::loadParams(const ParamSnapshot& values, const EngineStash* stash, const PatchData* patch) {  // main thread
  ParamSnapshot clamped{};
  for (size_t i = 0; i < kParamCount; ++i) clamped[i] = clampParam(i, double(values[i]));
  const EngineStash s = stash ? *stash : defaultStash();
  mainPatch_ = patch ? *patch : defaultPatch(kind_);
  if (!active_.load(std::memory_order_acquire)) {  // no audio thread can run
    store(clamped);
    for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(s[i], std::memory_order_relaxed);
    hostUpdateCount_ = 0;  // superseded by the load
    adoptEngines(clamped);
    patch_ = mainPatch_;
    pendingState_.store(kIdle, std::memory_order_release);
    editState_.store(kIdle, std::memory_order_release);
    return;
  }
  // A route edit still waiting is older than this load: drop it (the load carries mainPatch_).
  for (;;) {
    int expected = kReady;
    if (editState_.compare_exchange_strong(expected, kIdle, std::memory_order_acq_rel)) break;
    if (expected == kIdle) break;
    std::this_thread::yield();  // kApplying: the audio thread is copying the edit
  }
```

In the rest of `loadParams`, after `pendingStash_ = s;`, add `pendingPatch_ = mainPatch_;`. After `loadParams`, add:

```cpp
void Synth::setRoute(int part, int route, const Route& value) {  // main thread
  if (part < 0 || part >= kNumParts || route < 0 || route >= kNumRoutes) return;
  mainPatch_.routes[size_t(part)][size_t(route)] = value;
  publishPatch();
  engineChanged_.store(true, std::memory_order_release);  // the route amount's text follows it
}

void Synth::setEnvelopeShape(int part, int slot, const EnvelopeShape& shape) {  // main thread
  if (part < 0 || part >= kNumParts || slot < 0 || slot >= kNumModSlots) return;
  mainPatch_.shapes[size_t(part)][size_t(slot)] = sanitized(shape);
  publishPatch();
}

// The pendingState_ protocol for patch edits: the audio thread takes the whole patch at its next
// block; a newer edit replaces one it has not taken yet.
void Synth::publishPatch() {
  if (!active_.load(std::memory_order_acquire)) {
    patch_ = mainPatch_;
    return;
  }
  for (;;) {
    int expected = kReady;
    if (editState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    expected = kIdle;
    if (editState_.compare_exchange_strong(expected, kWriting, std::memory_order_acq_rel)) break;
    std::this_thread::yield();
  }
  editPatch_ = mainPatch_;
  editState_.store(kReady, std::memory_order_release);
}

```

Replace `Synth::applyPending()` with:

```cpp
void Synth::applyPending() {  // audio thread, or main thread while inactive
  int expected = kReady;
  if (pendingState_.compare_exchange_strong(expected, kApplying, std::memory_order_acq_rel)) {
    store(pendingValues_);
    patch_ = pendingPatch_;  // a load's routes and shapes arrive with its values
    for (size_t i = 0; i < kStashSize; ++i) stash_[i].store(pendingStash_[i], std::memory_order_relaxed);
    hostUpdateCount_ = 0;  // switches from before the load are superseded by it
    // The snapshot's P values belong to its engines: adopt them without swapping.
    adoptEngines(pendingValues_);
    pendingState_.store(kIdle, std::memory_order_release);
  }
  // Then a route edit: it is newer than any load still waiting (a load drops older edits).
  int edit = kReady;
  if (editState_.compare_exchange_strong(edit, kApplying, std::memory_order_acq_rel)) {
    patch_ = editPatch_;
    editState_.store(kIdle, std::memory_order_release);
  }
}

```

In `Synth::partEngines()`, before `return e;`, add:

```cpp
  for (int r = 0; r < kNumRoutes; ++r) e.routeDestination[r] = mainPatch_.routes[size_t(part)][size_t(r)].destination;
```

- [ ] **Step 5: State schema 5 (`core/state/state.h`, `core/state/state.cpp`)**

In `core/state/state.h`, set `constexpr int kSchemaVersion = 5;`.

In `core/state/state.cpp`: extend the schema comment with "Schema 4 -> 5 (M4): mod slots, routes and envelope shapes; older documents load with no modulation at their old level (Voice Gain 0 dB, Soft Clip off; provisional ruling 30)."; make `isKnownTopLevel` also accept `"routes"` and `"envelopeShapes"`; before the closing `}  // namespace` of the anonymous namespace add:

```cpp
// "routes": {part: [[source, destination, via], ... up to 12]}. Codes are kept as saved (unknown
// ones too: a newer version's routes come back unchanged on resave); entries that are not three
// integers 0..65535 are dropped. False if anything had to be dropped.
bool readRoutes(const nlohmann::json& root, PatchData& patch) {
  const auto found = root.find("routes");
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_array() ||
        partIt.value().size() > size_t(kNumRoutes)) {
      clean = false;
      continue;
    }
    const auto& list = partIt.value();
    for (size_t r = 0; r < list.size(); ++r) {
      const auto& entry = list[r];
      bool valid = entry.is_array() && entry.size() == 3;
      for (size_t k = 0; valid && k < 3; ++k) {
        valid = entry[k].is_number_integer() && entry[k].get<long long>() >= 0 && entry[k].get<long long>() <= 65535;
      }
      if (!valid) {
        clean = false;
        continue;
      }
      patch.routes[part][r] = {uint16_t(entry[0].get<long long>()), uint16_t(entry[1].get<long long>()),
                               uint16_t(entry[2].get<long long>())};
    }
  }
  return clean;
}

// "envelopeShapes": {part: [{"count": n, "types": [6], "loops": [6]}, ... 4 slots]}. A malformed
// shape keeps the default (ADSR); values out of range are sanitized.
bool readShapes(const nlohmann::json& root, PatchData& patch) {
  const auto found = root.find("envelopeShapes");
  if (found == root.end()) return true;
  if (!found->is_object()) return false;
  bool clean = true;
  for (auto partIt = found->begin(); partIt != found->end(); ++partIt) {
    uint32_t part = 0;
    if (!parseId(partIt.key(), &part) || part >= uint32_t(kNumParts) || !partIt.value().is_array() ||
        partIt.value().size() > size_t(kNumModSlots)) {
      clean = false;
      continue;
    }
    const auto& list = partIt.value();
    for (size_t slot = 0; slot < list.size(); ++slot) {
      const auto& e = list[slot];
      const auto count = e.is_object() ? e.find("count") : e.end();
      const auto types = e.is_object() ? e.find("types") : e.end();
      const auto loops = e.is_object() ? e.find("loops") : e.end();
      bool valid = e.is_object() && count != e.end() && count->is_number_integer() && types != e.end() &&
                   types->is_array() && types->size() == size_t(EnvelopeShape::kMaxSegments) && loops != e.end() &&
                   loops->is_array() && loops->size() == size_t(EnvelopeShape::kMaxSegments);
      for (size_t i = 0; valid && i < size_t(EnvelopeShape::kMaxSegments); ++i) {
        valid = (*types)[i].is_number_integer() && (*loops)[i].is_number_integer();
      }
      if (!valid) {
        clean = false;
        continue;
      }
      EnvelopeShape shape;
      shape.count = uint8_t(std::clamp(count->get<long long>(), 0LL, 255LL));
      for (size_t i = 0; i < size_t(EnvelopeShape::kMaxSegments); ++i) {
        shape.type[i] = uint8_t(std::clamp((*types)[i].get<long long>(), 0LL, 255LL));
        shape.loop[i] = uint8_t(std::clamp((*loops)[i].get<long long>(), 0LL, 255LL));
      }
      patch.shapes[part][slot] = sanitized(shape);
    }
  }
  return clean;
}

```

In `StateStore::save`, before `return root.dump();`, add:

```cpp
  const PatchData patch = synth.patch();
  nlohmann::json routes = nlohmann::json::object();
  nlohmann::json shapes = nlohmann::json::object();
  for (int part = 0; part < kNumParts; ++part) {
    nlohmann::json list = nlohmann::json::array();
    for (const Route& r : patch.routes[size_t(part)]) list.push_back({r.source, r.destination, r.via});
    routes[std::to_string(part)] = std::move(list);
    nlohmann::json slots = nlohmann::json::array();
    for (const EnvelopeShape& shape : patch.shapes[size_t(part)]) {
      nlohmann::json types = nlohmann::json::array();
      nlohmann::json loops = nlohmann::json::array();
      for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
        types.push_back(int(shape.type[i]));
        loops.push_back(int(shape.loop[i]));
      }
      slots.push_back({{"count", int(shape.count)}, {"types", std::move(types)}, {"loops", std::move(loops)}});
    }
    shapes[std::to_string(part)] = std::move(slots);
  }
  root["routes"] = std::move(routes);
  root["envelopeShapes"] = std::move(shapes);
```

In `StateStore::load`, before `EngineStash stash = Synth::defaultStash();`, add:

```cpp
  PatchData patch;  // no routes, ADSR custom shapes
  if (schema < 5) {
    // Schema 4 (M3) had no modulation: those projects load with every mod slot empty, no routes,
    // and their old level (Voice Gain 0 dB, Soft Clip off). With no route to the Amp, the Amp
    // follows the gate (spec §5.2).
    for (int part = 0; part < kNumParts; ++part) {
      for (int slot = 0; slot < kNumModSlots; ++slot) {
        values[modEngineParamIndex(part, slot)] = 0.0f;
        values[modScopeParamIndex(part, slot)] = 0.0f;
      }
      for (int r = 0; r < kNumRoutes; ++r) values[routeAmountParamIndex(part, r)] = 0.0f;
    }
    values[masterParamIndex(MasterParam::VoiceGain)] = 1.0f;
    values[masterParamIndex(MasterParam::SoftClip)] = 0.0f;
  }
  if (!readRoutes(root, patch)) warnings = true;
  if (!readShapes(root, patch)) warnings = true;

```

and change `synth.loadParams(values, &stash);` to `synth.loadParams(values, &stash, &patch);`.

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: every test passes, including the four new state tests and two new Synth tests.

- [ ] **Step 7: Document projects from earlier versions and the init patch's routes**

In `docs/manual/parts-and-routing.md`, under "Projects from earlier versions", append:

```markdown
Projects saved before modulation existed open with every mod slot empty, no routes, *Voice Gain* at 0 dB and *Soft Clip* off, so they play at the level they had. One thing does change: with no route to the Amp, the Amp follows the keys, so a sound that used to ring on after you let go (a struck Resonator, a long Macro Oscillator decay) now stops 5 ms after the key. Add an envelope on a mod slot and route it to the Amp to bring the tail back. The routes, the envelopes' custom shapes and every mod engine's settings are saved with the project.
```

- [ ] **Step 8: Commit**

```bash
git add core/synth core/state tests/test_synth.cpp tests/test_state.cpp docs/manual/parts-and-routing.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Patch data: routes and envelope shapes with the pending pattern; state schema 5"
```

---
### Task 15: The Synth plays modulation: init patches, Voice Gain, soft clipper, beat clock

**Files:**
- Create: `core/mod/beat_clock.h`, `tests/synth_helpers.h`
- Modify: `core/synth/synth.h`, `core/synth/synth.cpp`, `docs/manual/parts-and-routing.md`, `docs/manual/modulation.md`
- Test: `tests/test_beat_clock.cpp` (new), `tests/test_synth.cpp`

**Interfaces:**
- Consumes: `Part::process(const VoiceInputs&, float* const*, const PartParams&, int)`, `PartParams`, `ModTiming`, `Part::setSeedBase` (Task 11); `softClip` (Task 12); `PatchData` and `patch_` (Task 14); `MasterParam::VoiceGain`, `MasterParam::SoftClip`, `modParamIndex`, `routeAmountParamIndex` (Task 13).
- Produces: `core/mod/beat_clock.h`: `struct Transport { bool playing; bool hasTempo; double tempo; bool hasBeats; double beats; double barStart; bool hasTimeSignature; int numerator; int denominator; }; class BeatClock { static constexpr double kDefaultTempo = 120.0; void prepare(double); void reset(); void setTransport(const Transport&); double beat() const; double tempo() const; double beatsPerSample() const; double beatsPerBar() const; double barPhase() const; bool jumped() const; void advance(int n); };` `void Synth::setTransport(const Transport&)` (audio thread, before a host block). `tests/synth_helpers.h`: `void test::m3Patch(Synth&)`.

From this task on the Synth calls the Part's modulated path, so both init patches play (spec §6), the Voice Gain applies, Soft Clip guards both outputs, and the beat clock gives every part its musical time (owner decision 4: phase-locked while the transport plays, free-running at the last tempo while it is stopped). Each part gets its own random seed base, so Random / S&H and the Random source never repeat between parts.

Pre-M4 tests that construct a Synth and expect M3's exact levels call `test::m3Patch(synth)` (no mod slots, no routes, Voice Gain 0 dB, Soft Clip off, the instrument's Filter Off). A planning build of the whole plan up to this task found exactly two such tests (Bus 2 and FX alignment, below); every other existing test passed unchanged with the init patches playing.

- [ ] **Step 1: Write the beat clock tests (`tests/test_beat_clock.cpp`)**

```cpp
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "mod/beat_clock.h"

using namespace eg13;

TEST_CASE("The beat clock free-runs at 120 BPM before the host gives a tempo") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  REQUIRE(clock.tempo() == 120.0);
  clock.advance(48000);
  REQUIRE(std::fabs(clock.beat() - 2.0) < 1e-9);
  REQUIRE(std::fabs(clock.barPhase() - 0.5) < 1e-9);
}

TEST_CASE("The beat clock locks to the playing transport and keeps the last tempo when stopped") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  Transport t;
  t.playing = true;
  t.hasTempo = true;
  t.tempo = 90.0;
  t.hasBeats = true;
  t.beats = 17.25;
  t.barStart = 16.0;
  t.hasTimeSignature = true;
  t.numerator = 3;
  t.denominator = 4;
  clock.setTransport(t);
  REQUIRE(clock.jumped());  // the transport started
  REQUIRE(clock.beat() == 17.25);
  REQUIRE(std::fabs(clock.barPhase() - 1.25 / 3.0) < 1e-12);
  clock.advance(480);
  REQUIRE_FALSE(clock.jumped());
  t.beats = 17.25 + 480.0 * 1.5 / 48000.0;  // where the host says it is next: no jump
  clock.setTransport(t);
  REQUIRE_FALSE(clock.jumped());
  t.beats = 40.0;  // the host jumped (loop, locate)
  clock.setTransport(t);
  REQUIRE(clock.jumped());
  clock.advance(1);
  Transport stopped;  // no tempo, not playing: keep 90 BPM and run on
  clock.setTransport(stopped);
  const double before = clock.beat();
  clock.advance(32000);
  REQUIRE(std::fabs(clock.beat() - (before + 1.0)) < 1e-9);  // 90 BPM: 1.5 beats per second
  REQUIRE(clock.tempo() == 90.0);
  t.beats = 50.0;  // playing again: a start
  clock.setTransport(t);
  REQUIRE(clock.jumped());
  REQUIRE(clock.beat() == 50.0);
}

TEST_CASE("The beat clock ignores nonsense from the host") {
  BeatClock clock;
  clock.prepare(44100.0);
  clock.reset();
  Transport t;
  t.hasTempo = true;
  t.tempo = 0.0;
  t.hasTimeSignature = true;
  t.numerator = 0;
  clock.setTransport(t);
  REQUIRE(clock.tempo() == 120.0);
  REQUIRE(clock.beatsPerBar() == 4.0);
  t.tempo = NAN;
  t.playing = true;
  t.hasBeats = true;
  t.beats = NAN;
  clock.setTransport(t);
  REQUIRE(clock.tempo() == 120.0);
  REQUIRE(std::isfinite(clock.beat()));
}
```

- [ ] **Step 2: Write the helper and the failing Synth tests**

Create `tests/synth_helpers.h`:

```cpp
#pragma once

#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"

namespace eg13::test {

// For Synth tests written before modulation (M1-M3): the voice as M3 played it. No mod slot, no
// route (the Amp then follows the gate with its 5 ms ramps), Voice Gain 0 dB, Soft Clip off, and
// in the instrument the Filter Off (EG13 FX keeps its Analog Filter, as in M3). Main thread.
inline void m3Patch(Synth& synth) {
  for (int part = 0; part < kNumParts; ++part) {
    if (synth.kind() == PluginKind::Instrument) synth.setParam(paramSpec(filterEngineParamIndex(part)).id, 0.0);
    for (int slot = 0; slot < kNumModSlots; ++slot) synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, 0.0);
    for (int r = 0; r < kNumRoutes; ++r) synth.setRoute(part, r, Route{});
  }
  synth.setParam(paramSpec(masterParamIndex(MasterParam::VoiceGain)).id, 1.0);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::SoftClip)).id, 0.0);
}

}  // namespace eg13::test
```

In `tests/test_synth.cpp`, add `#include "synth_helpers.h"` after `#include "synth/synth.h"`. In "Bus 2 carries parts routed to it, and falls back to main when not connected", add `test::m3Patch(synth);` right after `Synth synth(PluginKind::Instrument, sine);`. In "FX: processed input is aligned with Dry", add `test::m3Patch(synth);  // wet + Dry reach full scale: no soft clipping here` right after `Synth synth(PluginKind::Effect);`. Append:

```cpp

TEST_CASE("The instrument init patch: an envelope on the Amp, scaled by velocity, with a release") {
  // A steady 0.5 source (it ignores the gate), so only the Amp shapes the level.
  auto steady = [] { return SourceSpec{std::make_unique<test::ConstantEngine>(), 0.0, 16}; };
  auto play = [&steady](float velocity) {
    Synth synth(PluginKind::Instrument, steady);
    synth.setParam(masterId(MasterParam::SoftClip), 0.0);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 57, velocity);
    Stereo out(52800);
    run(synth, out, 0, 24000);
    synth.noteOff(0, 57);
    run(synth, out, 24000, 52800);
    return out;
  };
  const Stereo loud = play(1.0f);
  const Stereo soft = play(0.5f);
  const double held = test::rms(loud.l, 4800, 24000);
  // 0.5 through the State Variable Filter's low-pass (opened by Mod 2), the Amp at velocity x
  // full sustain, -12 dB Voice Gain, 80 % master level.
  REQUIRE(std::fabs(held - 0.5 * 0.25119 * 0.8) < 0.002);
  REQUIRE(std::fabs(test::rms(soft.l, 4800, 24000) / held - 0.5) < 0.01);
  // Mod 1's release (0.5 s), not the gate's 5 ms: still sounding 0.1 s after the key.
  REQUIRE(test::rms(loud.l, 28800, 29280) > 0.25 * held);
  REQUIRE(test::rms(loud.l, 52320, 52800) < 0.01 * held);  // 0.6 s after the key
}

TEST_CASE("Voice Gain plays each voice 12 dB lower") {
  auto level = [](double voiceGain) {
    Synth synth(PluginKind::Instrument, sine);
    test::m3Patch(synth);
    synth.setParam(masterId(MasterParam::VoiceGain), voiceGain);
    synth.prepare(kRate, kBlock);
    synth.noteOn(0, 60, 1.0f);
    Stereo out(9600);
    run(synth, out, 0, 9600);
    return test::rms(out.l, 4800, 9600);
  };
  REQUIRE(std::fabs(level(0.0) / level(1.0) - 0.25119) < 1e-3);
}

TEST_CASE("Soft Clip keeps both outputs within full scale") {
  auto peak = [](bool softClip, bool bus2) {
    Synth synth(PluginKind::Instrument, sine);
    test::m3Patch(synth);  // 0 dB voices: an 8-note chord of 0.5 sines peaks near 3
    synth.setParam(masterId(MasterParam::Level), 1.0);
    synth.setParam(masterId(MasterParam::SoftClip), softClip ? 1.0 : 0.0);
    if (bus2) synth.setParam(partId(0, PartParam::Output), 1);
    synth.prepare(kRate, kBlock);
    for (int key = 60; key < 68; ++key) synth.noteOn(0, key, 1.0f);
    Stereo out(9600), busOut(9600);
    run(synth, out, 0, 9600, nullptr, &busOut);
    const Stereo& o = bus2 ? busOut : out;
    float p = 0.0f;
    for (size_t i = 0; i < o.l.size(); ++i) p = std::max(p, std::max(std::fabs(o.l[i]), std::fabs(o.r[i])));
    return p;
  };
  REQUIRE(peak(false, false) > 1.5f);
  REQUIRE(peak(true, false) <= 1.0f);
  REQUIRE(peak(true, false) > 0.9f);
  REQUIRE(peak(true, true) <= 1.0f);
}

TEST_CASE("Synth processing with every mod slot and route in use does not allocate") {
  Synth synth(PluginKind::Instrument);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Layer));
  for (int part = 0; part < kNumParts; ++part) {
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, double(1 + slot));
    }
    for (int r = 0; r < kNumRoutes; ++r) {
      synth.setRoute(part, r, Route{modOutputSource(r % kNumModSlots, 0), uint16_t(r < 4 ? kDestSourceP + r : kDestPan), kSrcNone});
      synth.setParam(paramSpec(routeAmountParamIndex(part, r)).id, 0.2);
    }
  }
  synth.prepare(44100.0, kBlock);
  synth.setActive(true);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  test::ScopedAllocationCounter counter;
  for (int block = 0; block < 200; ++block) {
    if (block % 50 == 0) for (int key : {48, 55, 60, 64, 67}) synth.noteOn(0, key, 0.8f);
    if (block % 50 == 40) synth.allNotesOff();
    if (block == 120) synth.setParam(paramSpec(modEngineParamIndex(0, 1)).id, 3.0);
    if (block == 150) synth.setParam(masterId(MasterParam::VoiceGain), 1.0);
    run(synth, out, 0, kBlock);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
  synth.setActive(false);
}
```

- [ ] **Step 3: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile error (`mod/beat_clock.h` not found).

- [ ] **Step 4: Write `core/mod/beat_clock.h`**

```cpp
#pragma once

#include <cmath>

namespace eg13 {

// What the host says about its transport at the start of a block (CLAP's clap_event_transport).
struct Transport {
  bool playing = false;
  bool hasTempo = false;
  double tempo = 120.0;  // BPM
  bool hasBeats = false;
  double beats = 0.0;     // song position in quarter notes
  double barStart = 0.0;  // quarter notes
  bool hasTimeSignature = false;
  int numerator = 4;
  int denominator = 4;
};

// Musical time for tempo-synced modulation (owner decision 4, spec §6): while the transport
// plays, the beat position is the host's (phase-locked); while it is stopped, it keeps running at
// the last tempo the host gave (120 BPM if none). A start, or a jump in the song position, is
// reported once as jumped() so engines can restart in line with the song.
class BeatClock {
 public:
  static constexpr double kDefaultTempo = 120.0;

  void prepare(double sampleRate) { rate_ = sampleRate > 0.0 ? sampleRate : 48000.0; }
  void reset() {  // position 0; keeps the last tempo and time signature
    beat_ = 0.0;
    barStart_ = 0.0;
    playing_ = false;
    jump_ = false;
  }
  // Audio thread, before the block's audio.
  void setTransport(const Transport& t) {
    if (t.hasTempo && std::isfinite(t.tempo) && t.tempo > 0.0) tempo_ = t.tempo;
    if (t.hasTimeSignature && t.numerator > 0 && t.denominator > 0) {
      beatsPerBar_ = double(t.numerator) * 4.0 / double(t.denominator);
    }
    if (t.playing && t.hasBeats && std::isfinite(t.beats)) {
      // A start, or a position more than a 64th note away from where the clock is: a jump.
      if (!playing_ || std::fabs(t.beats - beat_) > 1.0 / 64.0) jump_ = true;
      beat_ = t.beats;
      if (std::isfinite(t.barStart)) barStart_ = t.barStart;
    }
    playing_ = t.playing;
  }
  double beat() const { return beat_; }
  double tempo() const { return tempo_; }
  double beatsPerSample() const { return tempo_ / 60.0 / rate_; }
  double beatsPerBar() const { return beatsPerBar_; }
  double barPhase() const {  // 0..1 within the current bar
    const double bars = (beat_ - barStart_) / beatsPerBar_;
    return bars - std::floor(bars);
  }
  bool jumped() const { return jump_; }
  void advance(int n) {  // after n samples
    beat_ += double(n) * beatsPerSample();
    jump_ = false;
  }

 private:
  double rate_ = 48000.0;
  double tempo_ = kDefaultTempo;
  double beatsPerBar_ = 4.0;
  double beat_ = 0.0;
  double barStart_ = 0.0;
  bool playing_ = false;
  bool jump_ = false;
};

}  // namespace eg13
```

- [ ] **Step 5: Play the modulated path in the Synth**

In `core/synth/synth.h`: add `#include "mod/beat_clock.h"` after `#include "engine/engine_registry.h"`; after `void setEnvelopeShape(...)`, add

```cpp
  // Audio thread, at the start of each host block: the host's transport, for tempo sync and the
  // Transport Phase source. Without it the beat clock free-runs at 120 BPM.
  void setTransport(const Transport& transport) { clock_.setTransport(transport); }
```

and after `std::atomic<int> editState_{kIdle};` add `BeatClock clock_;`.

In `core/synth/synth.cpp`:

- After `#include <thread>`, add `#include "util/soft_clip.h"`; in the anonymous namespace add `constexpr float kVoiceHeadroom = 0.25118864f;  // -12 dB (Voice Gain option 0, provisional ruling 24)`.
- In both constructors, after the line that creates the parts, add `for (int p = 0; p < kNumParts; ++p) parts_[size_t(p)]->setSeedBase(uint32_t(p + 1) * 1000u);`.
- In `prepare`, before `reset();`, add `clock_.prepare(sampleRate);`; at the end of `reset()`, add `clock_.reset();`.
- In `process`, after `processChunk(...)` in the chunk loop, add `clock_.advance(count);`.
- In `processChunk`, replace everything from `bool toBus2[kNumParts];` down to (not including) `feedOther(p, n);` with:

```cpp
  bool toBus2[kNumParts];
  bool heard[kNumParts];
  ModTiming timing;
  timing.beat = clock_.beat();
  timing.beatsPerSample = clock_.beatsPerSample();
  timing.jump = clock_.jumped();
  timing.barPhase = clock_.barPhase();
  timing.beatsPerBar = clock_.beatsPerBar();
  const float voiceGain = load(masterParamIndex(MasterParam::VoiceGain)) >= 0.5f ? 1.0f : kVoiceHeadroom;
  for (int p = 0; p < kNumParts; ++p) {  // A before B: A -> B has no delay
    Part& part = *parts_[size_t(p)];
    PartParams params;
    for (int i = 0; i < int(kSourceParamCount); ++i) params.slots.source.p[i] = load(sourceParamIndex(p, i));
    for (int i = 0; i < int(kFilterParamCount); ++i) params.slots.filter.p[i] = load(filterParamIndex(p, i));
    for (int i = 0; i < int(kShaperParamCount); ++i) params.slots.shaper.p[i] = load(shaperParamIndex(p, i));
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      for (int i = 0; i < int(kModSlotParamCount); ++i) params.mod.slot[slot].p[i] = load(modParamIndex(p, slot, i));
      params.mod.shapes[slot] = patch_.shapes[size_t(p)][size_t(slot)];
    }
    for (int r = 0; r < kNumRoutes; ++r) {
      params.mod.routes.route[r] = patch_.routes[size_t(p)][size_t(r)];
      params.mod.routes.amount[r] = load(routeAmountParamIndex(p, r));
    }
    params.timing = timing;
    params.voiceGain = voiceGain;
```

  and change the call `part.process(inputs, out, params, n);` to keep that name (it now takes `PartParams`). Before the output loop (`for (int i = 0; i < n; ++i) {` that starts with `float main[2]`), add `const bool softClipOn = load(masterParamIndex(MasterParam::SoftClip)) >= 0.5f;`, and replace the two output writes at its end with:

```cpp
    for (int ch = 0; ch < 2; ++ch) {
      const float m = main[ch] * gain + delayed[ch] * dry;
      const float b = bus[ch] * gain;
      // The master soft clipper (owner decision 2), on both outputs after Dry.
      mainOut[ch][i] = softClipOn ? softClip(m) : m;
      if (bus2Out != nullptr) bus2Out[ch][i] = softClipOn ? softClip(b) : b;
    }
```

- [ ] **Step 6: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: every test passes (a planning build of the plan to this point passed all 507). If another pre-M4 Synth test fails on level or timing, add `test::m3Patch(synth);` after its constructor and note it in the commit message; if a test fails for any other reason, stop and report.

- [ ] **Step 7: Document the init patches and tempo**

In `docs/manual/parts-and-routing.md`, replace the "Default patches" section's EG13 bullet with:

```markdown
- **EG13:** both parts play the Macro Oscillator through the **State Variable Filter** (low-pass at 70 %, about 2.5 kHz) and Shaper *Off*, with modulation:
  - Mod 1: Envelope (ADSR), per voice, routed to the **Amp**, scaled by velocity, 100 %.
  - Mod 2: Envelope (ADSR), per voice, routed to the **Filter's cutoff**, +30 % (it opens the filter fully at the envelope's peak).
  - Mod 3: LFO at about 5 Hz, per voice, routed to **pitch** through the **mod wheel**, ±0.5 semitone (vibrato).
  - Mod 4: empty. Pitch bend moves the pitch ±2 semitones (the part's Bend Range).
  - Voice Gain −12 dB, Soft Clip on.
```

and add to the EG13 FX bullets:

```markdown
  - Part A's Mod 1 is an LFO at about 0.25 Hz, per part, routed to the Analog Filter's cutoff, +20 %: with the filter fully open it sweeps down to about 5 kHz and back every four seconds.
  - Voice Gain 0 dB (EG13 FX usually plays a single voice), Soft Clip on.
```

Append to `docs/manual/modulation.md`:

````markdown
## Tempo

EG13 follows the host's tempo and song position. While the transport plays, tempo-synced engines (the LFO and Random / S&H with Sync on) and the Transport Phase source line up with the song; when it starts, or when you jump to another place in the song, they jump into line. While the transport is stopped they keep running at the last tempo the host gave (120 BPM until the host gives one), so a synced LFO never stops.
````

- [ ] **Step 8: Commit**

```bash
git add core/mod/beat_clock.h core/synth tests/test_beat_clock.cpp tests/synth_helpers.h tests/test_synth.cpp docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Synth plays modulation: init patches, Voice Gain, master soft clipper, beat clock"
```

---
### Task 16: MIDI, MPE zones, pressure, note expressions, release velocity, CC learn

**Files:**
- Modify: `core/synth/synth.h`, `core/synth/synth.cpp`, `docs/manual/modulation.md`
- Test: `tests/test_synth.cpp`

**Interfaces:**
- Consumes: `Part::noteOn(key, velocity, channel)`, `Part::noteOff(key, releaseVelocity)`, `Part::setController`, `Part::memberExpression`, `Part::noteExpression`, `Part::isMemberChannel`, `Expression`, `PartController`, `kDefaultReleaseVelocity`, `PartSettings::mpe*` (Task 11); `PartParam::MpeMode`, `MpeBendRange`, `CcA..CcD` (Task 13); `Synth::queueHostUpdate`, `engineChanged_` (M2).
- Produces (`core/synth/synth.h`): `void noteOff(int channel, int key, float releaseVelocity = kDefaultReleaseVelocity); void channelPressure(int channel, float value); void polyPressure(int channel, int key, float value); void noteExpression(int channel, int key, Expression, float value); void learnCc(int part, int slot); const Part& part(int index) const;` (tests). `pitchBend` and `controlChange` keep their signatures and learn MPE and the CC sources.

MPE without a UI (provisional ruling 11): the Synth works out each MPE part's zone from both parts' *MPE* settings when it reads the part settings (once per block) and hands the Part its master and member channels. A member channel's bend (× MPE Bend Range), pressure and CC74 go to the notes on that channel; the master channel's bend, pressure and controllers are part-wide. Learning (provisional ruling 14) writes the part's CC A–D parameter from the audio thread exactly as an engine switch writes P values: through `setParam`, a queued host update (sent with `CLAP_EVENT_DONT_RECORD`) and the engine-changed flag (VALUES|TEXT|INFO rescan).

- [ ] **Step 1: Write the failing tests (append to `tests/test_synth.cpp`)**

```cpp

TEST_CASE("The mod wheel brings in the init patch's vibrato") {
  // Per-cycle frequency range of a sine note over half a second, with the wheel at `wheel`.
  auto spread = [](int wheel) {
    Synth synth(PluginKind::Instrument, sine);
    synth.prepare(kRate, kBlock);
    synth.controlChange(0, 1, wheel);
    synth.noteOn(0, 69, 1.0f);
    Stereo out(48000);
    run(synth, out, 0, 48000);
    double lowest = 1e9, highest = 0.0, last = -1.0;
    for (size_t i = 24000; i < out.l.size(); ++i) {
      if (out.l[i - 1] < 0.0f && out.l[i] >= 0.0f) {
        const double t = double(i - 1) + double(-out.l[i - 1]) / double(out.l[i] - out.l[i - 1]);
        if (last >= 0.0) {
          lowest = std::min(lowest, kRate / (t - last));
          highest = std::max(highest, kRate / (t - last));
        }
        last = t;
      }
    }
    return highest / lowest;
  };
  REQUIRE(spread(0) < 1.002);
  const double full = spread(127);  // +-0.5 semitone: a whole semitone from lowest to highest
  REQUIRE(full > 1.04);
  REQUIRE(full < 1.08);
}

TEST_CASE("MPE member messages reach only their own voice; the master channel reaches the part") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(partId(0, PartParam::MpeMode), 1);  // lower zone: master 1, members 2..16
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcChannelPressure, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  synth.noteOn(1, 60, 1.0f);  // MIDI channel 2
  synth.noteOn(2, 64, 1.0f);  // MIDI channel 3
  synth.pitchBend(1, 0.5f);   // member: that note only, x 48 semitones
  synth.channelPressure(0, 0.6f);  // master: the whole part
  run(synth, out, 0, kBlock);
  const Part& a = synth.part(0);
  REQUIRE(a.lastControl(0).pitch == 24.0f);
  REQUIRE(a.lastControl(1).pitch == 0.0f);
  REQUIRE(a.lastControl(0).params.source.p[1] == 0.6f);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.6f);
  synth.channelPressure(2, 0.3f);  // member: that note's Pressure, not the part's
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.6f);
  synth.noteOn(5, 72, 1.0f);  // channel 6: another member
  REQUIRE(synth.activeVoiceCount(0) == 3);
}

TEST_CASE("Both parts in MPE split the member channels") {
  Synth synth(PluginKind::Instrument, sine);
  synth.setParam(masterId(MasterParam::Mode), double(PartMode::Multi));
  synth.setParam(partId(0, PartParam::MpeMode), 1);  // lower zone: channels 2..8
  synth.setParam(partId(1, PartParam::MpeMode), 2);  // upper zone: channels 9..15
  synth.prepare(kRate, kBlock);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  synth.noteOn(2, 60, 1.0f);  // channel 3: Part A
  synth.noteOn(9, 62, 1.0f);  // channel 10: Part B
  synth.noteOn(7, 64, 1.0f);  // channel 8: Part A
  synth.noteOn(8, 65, 1.0f);  // channel 9: Part B
  run(synth, out, 0, kBlock);
  REQUIRE(synth.activeVoiceCount(0) == 2);
  REQUIRE(synth.activeVoiceCount(1) == 2);
  synth.setParam(partId(1, PartParam::MpeMode), 0);  // Part B off: Part A's zone grows to 2..16
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).isMemberChannel(14));
}

TEST_CASE("CC A-D read their controller numbers and can learn a new one") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  synth.setParam(partId(0, PartParam::CcA), 20);
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcCcA, uint16_t(kDestSourceP + 0), kSrcNone});
  synth.setRoute(0, 1, Route{kSrcCcB, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setRoute(0, 2, Route{kSrcSlide, uint16_t(kDestSourceP + 2), kSrcNone});
  for (int r = 0; r < 3; ++r) synth.setParam(paramSpec(routeAmountParamIndex(0, r)).id, 1.0);
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  synth.noteOn(0, 60, 1.0f);
  synth.controlChange(0, 20, 127);
  synth.controlChange(0, 74, 127);  // not MPE: the part's Slide
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[0] == 1.0f);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[2] == 1.0f);
  HostUpdate updates[kMaxHostUpdates];
  synth.takeHostUpdates(updates, int(kMaxHostUpdates));
  synth.takeEngineChanged();
  synth.learnCc(0, 1);  // CC B learns the next controller
  synth.controlChange(0, 64, 127);  // sustain is never learnt
  synth.controlChange(0, 33, 127);
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 33.0);
  REQUIRE(synth.takeHostUpdates(updates, int(kMaxHostUpdates)) == 1);
  REQUIRE(updates[0].id == partId(0, PartParam::CcB));
  REQUIRE(updates[0].value == 33.0f);
  REQUIRE(synth.takeEngineChanged());
  synth.controlChange(0, 34, 127);  // the learn was used up
  REQUIRE(synth.param(partId(0, PartParam::CcB)) == 33.0);
  run(synth, out, 0, kBlock);
  REQUIRE(synth.part(0).lastControl(0).params.source.p[1] == 1.0f);
  synth.setActive(false);
}

TEST_CASE("Polyphonic pressure, note expressions and release velocity reach their notes") {
  Synth synth(PluginKind::Instrument, sine);
  test::m3Patch(synth);
  for (int p = 0; p < 3; ++p) synth.setParam(paramSpec(sourceParamIndex(0, p)).id, 0.0);  // routes add to 0
  synth.setRoute(0, 0, Route{kSrcPressure, uint16_t(kDestSourceP + 0), kSrcNone});
  synth.setRoute(0, 1, Route{kSrcReleaseVelocity, uint16_t(kDestSourceP + 1), kSrcNone});
  synth.setParam(paramSpec(routeAmountParamIndex(0, 0)).id, 1.0);
  synth.setParam(paramSpec(routeAmountParamIndex(0, 1)).id, 1.0);
  synth.prepare(kRate, kBlock);
  synth.noteOn(0, 60, 1.0f);
  synth.noteOn(0, 64, 1.0f);
  synth.polyPressure(0, 64, 0.5f);
  synth.noteExpression(0, 60, Expression::Bend, 3.0f);
  Stereo out(kBlock);
  run(synth, out, 0, kBlock);
  const Part& a = synth.part(0);
  REQUIRE(a.lastControl(0).params.source.p[0] == 0.0f);
  REQUIRE(a.lastControl(1).params.source.p[0] == 0.5f);
  REQUIRE(a.lastControl(0).pitch == 3.0f);
  synth.noteOff(0, 64, 0.25f);
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(1).params.source.p[1] == 0.25f);
  synth.noteOn(0, 67, 1.0f);  // voice 2, the first free one (voice 1 is still releasing)
  synth.noteOn(0, 67, 0.0f);  // a note-on at velocity 0: a note-off without release velocity
  run(synth, out, 0, kBlock);
  REQUIRE(a.lastControl(2).params.source.p[1] == kDefaultReleaseVelocity);
}
```

- [ ] **Step 2: Run them to verify they fail**

Run: `cmake --build --preset mac --target eg13_tests`
Expected: compile errors (`channelPressure`, `polyPressure`, `learnCc`, `part`, … not members of `Synth`).

- [ ] **Step 3: Declare the MIDI API (`core/synth/synth.h`)**

Replace the declarations from `void noteOn(int channel, int key, float velocity);` through `void allNotesOff();` with:

```cpp
  void noteOn(int channel, int key, float velocity);
  // channel < 0: every channel. releaseVelocity: 0..1, the Release Velocity source.
  void noteOff(int channel, int key, float releaseVelocity = kDefaultReleaseVelocity);
  // -1..1. An MPE part takes its master channel's bend part-wide and a member channel's as that
  // channel's notes' bend, times the MPE Bend Range (provisional ruling 11).
  void pitchBend(int channel, float value);
  // CC1 mod wheel, CC64 sustain, CC74 slide (per note on an MPE member channel), the part's CC A-D
  // numbers, CC120/123 all notes off; a pending learnCc() takes this CC.
  void controlChange(int channel, int cc, int value);
  void channelPressure(int channel, float value);       // 0..1; per note on MPE member channels
  void polyPressure(int channel, int key, float value);  // 0..1: that note's Pressure
  // A note expression (CLAP): the voices playing key on channel (negative: any channel).
  void noteExpression(int channel, int key, Expression expression, float value);
  void allNotesOff();
  // Main thread: the next controller message on a channel the part hears sets CC A..D (slot
  // 0..3) to its number; -1 cancels. For EG13's interface (M6).
  void learnCc(int part, int slot) { learn_[size_t(part)].store(slot, std::memory_order_release); }

```

After `int activeVoiceCount(int part) const {...}`, add `const Part& part(int index) const { return *parts_[size_t(index)]; }  // tests`. After `BeatClock clock_;`, add:

```cpp
  std::array<std::atomic<int>, kNumParts> learn_{};  // CC A..D slot waiting for a learn, or -1
```

- [ ] **Step 4: Route the messages (`core/synth/synth.cpp`)**

- In both constructors, first thing in the body: `for (auto& learn : learn_) learn.store(-1, std::memory_order_relaxed);`
- In `noteOn`, pass the channel: `part.noteOn(key, velocity, channel);`
- `noteOff` takes `float releaseVelocity` and passes it: `parts_[size_t(p)]->noteOff(key, releaseVelocity);`
- Replace `Synth::pitchBend` with:

```cpp
void Synth::pitchBend(int channel, float value) {
  for (auto& part : parts_) {
    const PartSettings& s = part->settings();
    if (s.mpe != 0 && channel != s.mpeMaster) {
      if (part->isMemberChannel(channel)) {
        part->memberExpression(channel, Expression::Bend, std::clamp(value, -1.0f, 1.0f) * float(s.mpeBendRange));
      }
    } else if (part->acceptsChannel(channel)) {
      part->pitchBend(value);
    }
  }
}

void Synth::channelPressure(int channel, float value) {
  for (auto& part : parts_) {
    if (part->isMemberChannel(channel)) {
      part->memberExpression(channel, Expression::Pressure, value);
    } else if (part->acceptsChannel(channel)) {
      part->setController(PartController::ChannelPressure, value);
    }
  }
}

void Synth::polyPressure(int channel, int key, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->noteExpression(channel, key, Expression::Pressure, value);
  }
}

void Synth::noteExpression(int channel, int key, Expression expression, float value) {
  for (auto& part : parts_) {
    if (part->acceptsChannel(channel)) part->noteExpression(channel, key, expression, value);
  }
}

```

- Replace `Synth::controlChange` with:

```cpp
void Synth::controlChange(int channel, int cc, int value) {
  const float v = float(std::clamp(value, 0, 127)) / 127.0f;
  for (int p = 0; p < kNumParts; ++p) {
    Part& part = *parts_[size_t(p)];
    if (!part.acceptsChannel(channel)) continue;
    int learning = learn_[size_t(p)].load(std::memory_order_acquire);
    if (learning >= 0 && learning < 4 && cc != 64 && cc != 120 && cc != 123 &&
        learn_[size_t(p)].compare_exchange_strong(learning, -1, std::memory_order_acq_rel)) {
      const size_t index = partParamIndex(p, PartParam(uint32_t(PartParam::CcA) + uint32_t(learning)));
      setParam(paramSpec(index).id, double(cc));
      queueHostUpdate({paramSpec(index).id, float(cc)});
      engineChanged_.store(true, std::memory_order_release);
    }
    const bool member = part.isMemberChannel(channel);
    if (cc == 74) {
      if (member) {
        part.memberExpression(channel, Expression::Slide, v);
      } else {
        part.setController(PartController::Slide, v);
      }
    }
    if (member) continue;  // other controllers on a member channel are not per note: ignored
    if (cc == 1) part.setController(PartController::ModWheel, v);
    for (int k = 0; k < 4; ++k) {
      if (cc == int(load(partParamIndex(p, PartParam(uint32_t(PartParam::CcA) + uint32_t(k)))))) {
        part.setController(PartController(int(PartController::CcA) + k), v);
      }
    }
  }
  if (cc == 64) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->sustain(value >= 64);
    }
  } else if (cc == 120 || cc == 123) {
    for (auto& part : parts_) {
      if (part->acceptsChannel(channel)) part->allNotesOff();
    }
    if (channel >= 0) {
      std::fill(std::begin(routed_[clampChannel(channel)]), std::end(routed_[clampChannel(channel)]),
                uint8_t{0});
    }
  }
}

```

- In `readPartSettings`, after `s.inputSource = int(v(PartParam::InputSource));`, add:

```cpp
  // MPE zones (provisional ruling 11): the lower zone's master is channel 1, the upper zone's 16;
  // a zone has 15 member channels, or 7 each when the other part uses the other zone.
  s.mpe = int(v(PartParam::MpeMode));
  s.mpeBendRange = int(v(PartParam::MpeBendRange));
  const int other = int(load(partParamIndex(1 - part, PartParam::MpeMode)));
  if (s.mpe == 1) {
    s.mpeMaster = 0;
    s.mpeFirst = 1;
    s.mpeLast = other == 2 ? 7 : 15;
  } else if (s.mpe == 2) {
    s.mpeMaster = 15;
    s.mpeFirst = other == 1 ? 8 : 0;
    s.mpeLast = 14;
  }
```

- [ ] **Step 5: Run the tests**

Run: `cmake --build --preset mac --target eg13_tests && ctest --preset mac`
Expected: every test passes (a planning build of the plan to this point passed all 512).

- [ ] **Step 6: Document MIDI and MPE**

Append to `docs/manual/modulation.md`:

````markdown
## MIDI, MPE and expression

- **Mod wheel** (CC1), **pitch bend**, **channel pressure** and the four **CC** sources come from the part's MIDI channel. Set *CC A* to *CC D* in the host to the controller numbers you want (defaults: 2 Breath, 4 Foot, 11 Expression, 16). EG13's own interface will also let you learn them by moving a control.
- **Polyphonic aftertouch** is each note's *Pressure*. **Release velocity** (how fast you let go) is each note's *Release Velocity*; a note-off without one (a note-on with velocity 0) counts as 64.
- **MPE**: set the part's *MPE* to *Lower zone* (master channel 1, notes on channels 2 to 16) or *Upper zone* (master channel 16, notes on channels 1 to 15). If Part A uses one zone and Part B the other, the lower zone gets channels 2 to 8 and the upper zone 9 to 15. With MPE on, the part's *MIDI Channel* setting is not used.
  - On a note's own channel, pitch bend bends that note (by the *MPE Bend Range*, default ±48 semitones; it also feeds the *Note Bend* source), pressure is that note's *Pressure*, and CC74 is its *Slide*. Values sent before the note (as MPE controllers do) apply to it from the start. Other controllers on a note's channel are ignored.
  - On the master channel, pitch bend bends the whole part (by its *Bend Range*), and pressure and controllers are part-wide.
  - EG13 does not read the MPE Configuration Message: the zones are set by the *MPE* settings alone. If the same key is played on two channels at once, both share one voice.
- Without MPE, CC74 is a part-wide *Slide*. In CLAP hosts, per-note *Tuning* bends a note, per-note *Pressure* is its Pressure and per-note *Brightness* its Slide (other per-note expressions are not used yet).
````

- [ ] **Step 7: Commit**

```bash
git add core/synth tests/test_synth.cpp docs/manual/modulation.md
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "MIDI and MPE: zones, per-note bend/pressure/slide, poly pressure, release velocity, CC A-D learn"
```

---
### Task 17: Plugin: transport, note expressions, MPE dialect, pressure, release velocity

**Files:**
- Modify: `plugin/src/eg13_plugin.cpp`
- Test: `scripts/validate.sh` (clap-validator, auval, pluginval); manual host check

**Interfaces:**
- Consumes: `Synth::setTransport`, `Transport` (Task 15); `Synth::noteOff(channel, key, releaseVelocity)`, `channelPressure`, `polyPressure`, `noteExpression`, `Expression` (Task 16).
- Produces: the CLAP plugin passes the host's transport to the beat clock every block (and on in-block `CLAP_EVENT_TRANSPORT`), maps CLAP note expressions (provisional ruling 12), MIDI 0x80 release velocity, 0xA0 polyphonic pressure and 0xD0 channel pressure, and declares `CLAP_NOTE_DIALECT_MIDI_MPE`.

The plugin layer has no unit tests in this repository (M0–M3 test it with the validators and by hand); everything it calls is covered by Tasks 15 and 16.

- [ ] **Step 1: Pass the transport**

In `plugin/src/eg13_plugin.cpp`, after `Plugin* self(const clap_plugin_t* p) {...}`, add:

```cpp
// The host's transport (CLAP beat times are fixed point) for the beat clock.
Transport transportFrom(const clap_event_transport_t& t) {
  Transport out;
  out.playing = (t.flags & CLAP_TRANSPORT_IS_PLAYING) != 0;
  out.hasTempo = (t.flags & CLAP_TRANSPORT_HAS_TEMPO) != 0;
  out.tempo = t.tempo;
  out.hasBeats = (t.flags & CLAP_TRANSPORT_HAS_BEATS_TIMELINE) != 0;
  out.beats = double(t.song_pos_beats) / double(CLAP_BEATTIME_FACTOR);
  out.barStart = double(t.bar_start) / double(CLAP_BEATTIME_FACTOR);
  out.hasTimeSignature = (t.flags & CLAP_TRANSPORT_HAS_TIME_SIGNATURE) != 0;
  out.numerator = t.tsig_num;
  out.denominator = t.tsig_denom;
  return out;
}

```

In `pluginProcess`, after `plugin->synth.beginBlock();`, add:

```cpp
  if (proc->transport != nullptr) plugin->synth.setTransport(transportFrom(*proc->transport));
```

- [ ] **Step 2: Map note-offs, note expressions, transport events and MIDI**

In `handleEvent`, replace the `CLAP_EVENT_NOTE_OFF` / `CLAP_EVENT_NOTE_CHOKE` case with:

```cpp
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
      const auto* ev = reinterpret_cast<const clap_event_note_t*>(hdr);
      if (ev->key < 0) {
        plugin->synth.allNotesOff();
      } else if (hdr->type == CLAP_EVENT_NOTE_OFF) {
        plugin->synth.noteOff(ev->channel, ev->key, float(ev->velocity));  // release velocity
      } else {
        plugin->synth.noteOff(ev->channel, ev->key);
      }
      break;
    }
    case CLAP_EVENT_NOTE_EXPRESSION: {
      // Provisional ruling 12: tuning, pressure and brightness; the others are not used yet.
      const auto* ev = reinterpret_cast<const clap_event_note_expression_t*>(hdr);
      if (ev->key < 0 || ev->key > 127) break;
      if (ev->expression_id == CLAP_NOTE_EXPRESSION_TUNING) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Bend, float(ev->value));
      } else if (ev->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Pressure, float(ev->value));
      } else if (ev->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS) {
        plugin->synth.noteExpression(ev->channel, ev->key, Expression::Slide, float(ev->value));
      }
      break;
    }
    case CLAP_EVENT_TRANSPORT: {
      plugin->synth.setTransport(transportFrom(*reinterpret_cast<const clap_event_transport_t*>(hdr)));
      break;
    }
```

In the `CLAP_EVENT_MIDI` case, replace the `if (status == 0x90 && d2 > 0) { ... }` chain with:

```cpp
      if (status == 0x90 && d2 > 0) {
        plugin->synth.noteOn(channel, d1, float(d2) / 127.0f);
      } else if (status == 0x80) {
        plugin->synth.noteOff(channel, d1, float(d2) / 127.0f);  // with release velocity
      } else if (status == 0x90) {
        plugin->synth.noteOff(channel, d1);  // velocity 0: no release velocity (64)
      } else if (status == 0xA0) {
        plugin->synth.polyPressure(channel, d1, float(d2) / 127.0f);
      } else if (status == 0xB0) {
        plugin->synth.controlChange(channel, d1, d2);
      } else if (status == 0xD0) {
        plugin->synth.channelPressure(channel, float(d1) / 127.0f);
      } else if (status == 0xE0) {
        plugin->synth.pitchBend(channel, float(((d2 << 7) | d1) - 8192) / 8192.0f);
      }
```

In `notePortsGet`, set `info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI | CLAP_NOTE_DIALECT_MIDI_MPE;`.

- [ ] **Step 3: Build and validate**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass; clap-validator, the AU Info.plist check, `auval -v aumu EG13 Symb`, `auval -v aumf EGfx Symb` and pluginval pass ("All validators passed."). The parameter list now has 286 parameters; no earlier ID changed.

- [ ] **Step 4: Check in a host**

In Bitwig or Reaper (CLAP) and Logic (AU): load EG13 and play: notes have the init patch's envelope (a 0.5 s release), the mod wheel brings in vibrato, pitch bend bends ±2 semitones. Load EG13 FX on an audio track: the filter sweeps slowly. With an MPE controller (or Bitwig's per-note expressions) on a part set to *MPE Lower zone*, bend one note of a chord and check the others stay. Start and stop the transport with an LFO set to *Sync On* on a pitch route: it locks to the bar when playing and keeps going when stopped. Open an M3 project: same level, notes stop 5 ms after release. Report anything that differs from `docs/manual/modulation.md`.

- [ ] **Step 5: Commit**

```bash
git add plugin/src/eg13_plugin.cpp
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "Plugin: transport, note expressions, MPE dialect, pressure and release velocity"
```

---
### Task 18: Levels, CPU and memory reports; README; manual; validators and Linux

**Files:**
- Modify: `tests/test_levels.cpp`, `tests/test_benchmark.cpp`, `README.md`, `docs/manual/limitations.md`, `docs/manual/latency.md`, `docs/manual/index.md`
- Test: the above; `scripts/validate.sh`; the Docker command

**Interfaces:**
- Consumes: everything above; `test::m3Patch` (Task 15).
- Produces: printed level, CPU and memory figures; README and manual figures; a passing validator run and Linux build.

The CPU rule (provisional ruling 31): M3 left the heaviest mono-source chain at 49–51.5 % of the 50 % budget, so modulation cannot share it. The 50 % budget keeps measuring the voice chain without modulation (the M3 benchmark now calls `test::m3Patch`), and modulation has its own budget: at most 10 % of one M1 core for 2 × 8 voices with all four slots per voice running the costliest mod engine (the LFO in Frequency mode: 0.072 % per instance, measured in planning against Stages 0.010 %, Peaks 0.021 % and Marbles 0.052 %) and 12 active routes per part. A planning run of exactly this benchmark measured 49.7 % without and 57.1 % with full modulation: 7.3 % for modulation. If the REQUIRE fails on the M1: the pre-authorised lever (F19-style) is to skip mod slots that no active route reads (an unrouted slot changes nothing audible; keep its tick count so a route added later finds it running); re-measure; if still over, report the figures and stop: do not change DSP, defaults or budgets.

- [ ] **Step 1: Add the level reports (`tests/test_levels.cpp`)**

Add `#include "synth_helpers.h"` before `#include "test_helpers.h"`, and append:

```cpp

namespace {

// Peak of Part A's output for `notes` held for one second at velocity 1 (C3 upward in thirds),
// at the given Source engine, after the first 0.1 s. m3: no modulation, Voice Gain 0 dB, Soft
// Clip off (the trims alone); otherwise the instrument's init patch.
float sourcePeak(int engine, int notes, bool m3, bool softClip) {
  Synth synth(PluginKind::Instrument);
  if (m3) test::m3Patch(synth);
  synth.setParam(paramSpec(sourceEngineParamIndex(0)).id, double(engine));
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Level)).id, 1.0);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::SoftClip)).id, softClip ? 1.0 : 0.0);
  synth.prepare(kRate, 128);
  std::vector<float> in(128), l(128), r(128);
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  for (int k = 0; k < notes; ++k) synth.noteOn(0, 48 + 4 * k, 1.0f);
  float peak = 0.0f;
  for (int b = 0; b < int(kRate) / 128; ++b) {
    for (int i = 0; i < 128; ++i) in[size_t(i)] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(b * 128 + i) / kRate));
    synth.process(input, out, nullptr, 128);
    if (b * 128 < int(kRate) / 10) continue;
    for (int i = 0; i < 128; ++i) peak = std::max(peak, std::max(std::fabs(l[size_t(i)]), std::fabs(r[size_t(i)])));
  }
  return peak;
}

}  // namespace

TEST_CASE("Source trims bring every Source near the Macro Oscillator's level", "[levels]") {
  const float macro = sourcePeak(int(SourceEngine::Macro), 1, true, false);
  std::printf("One held C3 at velocity 1, defaults, after the trims (Voice Gain 0 dB, Soft Clip off):\n");
  for (int e = 0; e < kNumSourceEngines; ++e) {
    const float peak = sourcePeak(e, 1, true, false);
    const double db = 20.0 * std::log10(double(peak) / double(macro));
    std::printf("  %-20s trim %+5.1f dB  peak %.3f (%+5.1f dB against the Macro Oscillator)\n",
                sourceEngineInfo(e).displayName, double(sourceTrimDb(e)), double(peak), db);
    if (e != int(SourceEngine::AudioInput)) REQUIRE(std::fabs(db) < 1.5);  // the Input sets its own level
  }
}

TEST_CASE("Eight-note chords with the init patch stay within full scale", "[levels]") {
  std::printf("Eight-note chord, init patch (Voice Gain -12 dB), master 100%%: peak before / after Soft Clip\n");
  for (int e = 0; e < kNumSourceEngines; ++e) {
    const float raw = sourcePeak(e, 8, false, false);
    const float clipped = sourcePeak(e, 8, false, true);
    std::printf("  %-20s %.3f / %.3f\n", sourceEngineInfo(e).displayName, double(raw), double(clipped));
    REQUIRE(clipped <= 1.0f);
    REQUIRE(std::isfinite(raw));
  }
}
```

- [ ] **Step 2: Run them and keep the numbers**

Run: `cmake --build --preset mac --target eg13_tests && build/mac/tests/eg13_tests "[levels]" -s 2>&1 | grep -E "trim|chord|/ |Hz:|Init cutoff"` and `build/mac/tests/eg13_tests "Memory per part with every engine preallocated" -s 2>&1 | grep "Part memory"`.
Expected: both pass. Planning measured, after the trims, single notes at 0.659 (Macro), 0.684 (Digital), 0.680 (Modal), 0.674 (Resonator), all within 0.3 dB; eight-note chords with the init patch peaking 0.999 (Macro), 0.942 (Digital), 0.573 (Modal) before the soft clipper; part memory 5.43 MB constructed + 1.50 MB prepared. Keep the printed tables for Step 5 and the final report. If a trimmed Source is more than 1.5 dB from the Macro Oscillator, report BLOCKED with the table rather than changing trims.

- [ ] **Step 3: Extend the CPU benchmark (`tests/test_benchmark.cpp`)**

Add `#include "engine/tides_engine.h"` and `#include "synth_helpers.h"` next to `#include "synth/synth.h"`. In `chainLoad`, after `Synth synth(PluginKind::Instrument);`, add:

```cpp
  test::m3Patch(synth);  // the 50 % budget measures the chain without modulation (ruling 31)
```

Append:

```cpp

namespace {

// Percent of one core for 2 x 8 voices of the heaviest mono-source chain (Macro Oscillator ->
// Analog Filter -> Vocoder) at 48 kHz, 128-sample blocks. modulated: every mod slot of every voice
// runs the LFO in its costliest output mode (Frequency, measured during planning) and each part
// has 12 active routes into the Source, Filter, Shaper, pitch, Amp, pan and mod slot P's;
// otherwise no modulation (test::m3Patch). Provisional ruling 31.
double modulatedLoad(bool modulated, int seconds) {
  constexpr double kRate = 48000.0;
  constexpr int kBlock = 128;
  Synth synth(PluginKind::Instrument);
  test::m3Patch(synth);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
  const uint16_t destinations[kNumRoutes] = {
      uint16_t(kDestSourceP + 1), uint16_t(kDestSourceP + 2), uint16_t(kDestFilterP + 0), uint16_t(kDestFilterP + 1),
      uint16_t(kDestShaperP + 0), uint16_t(kDestShaperP + 1), kDestPitch, kDestAmp, kDestPan,
      modParamDestination(0, 2), modParamDestination(1, 3), uint16_t(kDestSourceP + 3)};
  for (int part = 0; part < kNumParts; ++part) {
    synth.setParam(paramSpec(sourceEngineParamIndex(part)).id, double(SourceEngine::Macro));
    synth.setParam(paramSpec(filterEngineParamIndex(part)).id, double(FilterEngine::Analog));
    synth.setParam(paramSpec(filterParamIndex(part, 0)).id, 0.6);
    synth.setParam(paramSpec(shaperEngineParamIndex(part)).id, double(ShaperEngine::RingMod));
    synth.setParam(paramSpec(shaperParamIndex(part, 0)).id, 1.0);  // vocoder
    synth.setParam(paramSpec(shaperParamIndex(part, 4)).id, WarpsEngine::optionValue(0));
    synth.setParam(paramSpec(shaperParamIndex(part, 5)).id, WarpsEngine::optionValue(0));
    synth.setParam(paramSpec(shaperParamIndex(part, 8)).id, 0.25);
    if (!modulated) continue;
    for (int slot = 0; slot < kNumModSlots; ++slot) {
      synth.setParam(paramSpec(modEngineParamIndex(part, slot)).id, double(ModEngineKind::Lfo));
      synth.setParam(paramSpec(modScopeParamIndex(part, slot)).id, 0.0);  // per voice
      synth.setParam(paramSpec(modParamIndex(part, slot, 9)).id, double(modOptionValue(TidesEngine::Frequency)));
    }
    for (int r = 0; r < kNumRoutes; ++r) {
      synth.setRoute(part, r, Route{modOutputSource(r % kNumModSlots, (r / kNumModSlots) % 4), destinations[r], kSrcNone});
      synth.setParam(paramSpec(routeAmountParamIndex(part, r)).id, 0.1);
    }
    // The Amp route alone would leave voices nearly silent; a second one keeps them open.
    synth.setRoute(part, 11, Route{kSrcGate, kDestAmp, kSrcNone});
    synth.setParam(paramSpec(routeAmountParamIndex(part, 11)).id, 0.8);
  }
  synth.prepare(kRate, kBlock);
  synth.setActive(true);
  std::vector<float> in(kBlock, 0.0f), l(kBlock), r(kBlock);
  const float* input[2] = {in.data(), in.data()};
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

}  // namespace

TEST_CASE("CPU: modulation at full load stays within its own budget", "[.benchmark]") {
  const double without = modulatedLoad(false, 5);
  const double with = modulatedLoad(true, 5);
  std::printf("Heaviest mono-source chain, 2 x 8 voices, no modulation: %.2f%% of one core over 5 s\n", without);
  std::printf("Same with 4 per-voice LFO slots (Frequency outputs) and 12 routes per part: %.2f%% of one core\n", with);
  std::printf("Modulation at full load: %.2f%% of one core (budget 10%%)\n", with - without);
  REQUIRE(with - without < 10.0);
}

TEST_CASE("CPU: the init patches", "[.benchmark]") {
  // For information: each plugin's init patch, both parts playing 8 voices (Layer).
  for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 128;
    Synth synth(kind);
    synth.setParam(paramSpec(masterParamIndex(MasterParam::Mode)).id, double(PartMode::Layer));
    synth.prepare(kRate, kBlock);
    synth.setActive(true);
    std::vector<float> in(kBlock), l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) in[size_t(i)] = 0.1f * float(std::sin(0.05 * i));
    const float* input[2] = {in.data(), in.data()};
    float* out[2] = {l.data(), r.data()};
    for (int key = 48; key < 56; ++key) synth.noteOn(0, key, 0.8f);
    const int blocks = int(kRate) * 5 / kBlock;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) synth.process(input, out, nullptr, kBlock);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    synth.setActive(false);
    std::printf("%s init patch, 2 x 8 voices: %.2f%% of one core over 5 s\n",
                kind == PluginKind::Effect ? "EG13 FX" : "EG13", 100.0 * elapsed / 5.0);
  }
}
```

- [ ] **Step 4: Run the benchmarks on the macOS build (Apple M1)**

Run: `build/mac/tests/eg13_tests "[.benchmark]" -s 2>&1 | grep -E "%"` (Release build; about five minutes).
Expected: the M3 table and its "Heaviest ..." lines (now without modulation, as M3 measured them), then the modulation lines; the modulation difference is below 10 %. If "CPU: Source x Filter x Shaper chains" fails its 50 % REQUIRE, that is M3's known margin (README): report the figure, it is not this milestone's budget.

- [ ] **Step 5: Update `README.md`**

Replace the `**Status:**` paragraph with:

```markdown
**Status:** M4 (modulation): two parts × 8 voices, each Source → Filter → Shaper → Amp → pan, with four mod slots per part (Envelope, LFO, Random / S&H, Function Generator; per voice or per part) and a 12-route modulation matrix (velocity, release velocity, keytrack, gate, random, MPE pressure / slide / per-note bend, mod wheel, pitch bend, channel pressure, four CCs, transport phase and the mod slots' outputs into any engine control, pitch, Amp, pan, part level and other mod slots). MPE (lower or upper zone per part), tempo sync, gain staging (−12 dB per voice, Source trims) and a master soft clipper. Sources: Macro Oscillator, Digital Oscillator, Modal Synth, Resonator, Audio Input. Filters: Analog Filter, Classic Filters (205 types), State Variable Filter. Shapers: Ring Mod / Vocoder and EQ. See `docs/superpowers/specs/2026-10-20-eg13-design.md` for the full design and `docs/manual/` for how EG13 behaves.
```

In "Engines", append:

```markdown
- **Envelope** — based on Stages
- **LFO** — based on Tides 2
- **Random / S&H** — based on Marbles
- **Function Generator** — based on Peaks
```

After the CPU lines, add the lines Step 4 printed for modulation and the init patches, prefixed with `Modulation, Apple M1 (base), 48 kHz, 128-sample blocks, 2 × 8 voices:`, and the sentence: "The 50 % budget applies to the voice chain with a mono Source and no modulation; modulation has its own budget of 10 % of a core with every mod slot of every voice in use."

- [ ] **Step 6: Update the manual's limitations, latency and index**

In `docs/manual/limitations.md`, replace the bullet that begins "**Levels and headroom:**" with (using the chord figures Step 2 printed):

```markdown
- **Levels:** each voice plays 12 dB lower (Voice Gain) and the Sources are trimmed to similar levels, so an eight-note chord of the init patch peaks around full scale (measured: Macro Oscillator 1.0, Digital Oscillator 0.94, Modal Synth 0.57). The Soft Clip on the outputs keeps louder material within full scale, gently. Some Filter and Shaper settings are still much louder than their input (Classic Filters at full Resonance about +13 dB, the EQ at its extremes, the Ring Mod / Vocoder up to +12 dB at *Fold*); turn the master level down, or let the Soft Clip catch them.
- **Routes without an editor:** until EG13's own interface arrives, routes (source, destination, via) and Custom envelope shapes cannot be changed in the host: the init patches' routes play, their amounts are host parameters, and saved projects keep whatever routes they hold.
- **MPE configuration:** EG13 does not read the MPE Configuration Message; set the zones with each part's *MPE* setting. A key held on two channels at once shares one voice.
- **Note expressions:** of CLAP's per-note expressions, EG13 uses Tuning, Pressure and Brightness; Volume, Pan, Vibrato and Expression are ignored for now.
```

and append to the "**CPU:**" bullet: "Modulation adds about 7 % of a core when every mod slot of every voice is in use (measured on an M1; budget 10 %); see the README."

In `docs/manual/latency.md`, append: "Modulation adds no latency: a key reaches the modulation engines within a quarter of a millisecond, and their outputs reach the sound every 16 samples."

In `docs/manual/index.md`, change the first paragraph's "Each voice runs a **Source**, then a **Filter**, then a **Shaper**." to "Each voice runs a **Source**, then a **Filter**, then a **Shaper**, then an **Amp**; each part has four **mod slots** and a **modulation matrix**."

- [ ] **Step 7: Validate the plugins and the Linux build**

Run: `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac && scripts/validate.sh`
Expected: all tests pass; all validators pass ("All validators passed.").

Run the Docker command from Global Constraints.
Expected: GCC 13 builds with `-Werror` and every test passes. GCC is stricter than Clang about a few things this plan's code relies on: `constexpr` loops in `slotOffset` (C++14, fine), aggregate initialisation of `ModEngineInfo` with nested `EngineInfo` braces (fine), and `std::clamp` needing `<algorithm>` (included). If GCC reports a warning as an error, fix the warning in the file it names without changing behaviour.

- [ ] **Step 8: Commit**

```bash
git add tests/test_levels.cpp tests/test_benchmark.cpp README.md docs/manual
git -c user.name="Westlee Latta" -c user.email=symbioticaudio@proton.me commit -m "M4 reports: Source levels after trims, chord peaks, modulation CPU budget; README and manual"
```

---

## Self-review notes

- **Spec coverage.** §4 Mod rows: Envelope (Task 3), LFO with tempo sync (Task 4), Random / S&H per part by default with tempo sync (Task 5), Function Generator with envelope, LFO, tap LFO and random (Task 6). §5.2 Amp: routed or following the gate with 5 ms ramps, Amp gain = Voice Gain, freeing at −90 dBFS for 50 ms at the Amp's output (Tasks 10–11). §5.3 Always on with modulation (Tasks 10–11, ruling 8). §6 mod slots, scopes, fixed sources, the 12-route matrix with via, destinations, normalized add-and-clamp (Tasks 7–9, 11), control rate every 16 samples with per-sample smoothing of Amp/pan/gains (Tasks 2, 10, 11), owner decisions: soft clipper (Tasks 12, 15), MPE (Tasks 11, 16, 17), tempo free-run and phase-lock (Tasks 4, 5, 15, 17); init patches of both plugins (Tasks 13–15; Part FX Granular in the FX init patch is M5: Part A's chain is Audio Input → Analog Filter → Shaper Off with Mod 1's LFO on the cutoff, and its Part FX is added when M5 brings the slot). §7.1 mod slot engine, scope, P1–P16 and route amounts 1–12 as host parameters, stable append-only IDs (Task 13). §7.2 per-engine settings per slot including scope, routes and shapes as patch data, schema 5 with migration (Tasks 13–14). §9 NaN recovery per mod slot (Task 7), unknown codes kept (Tasks 8, 14). §10 fidelity bit-exact against the bare modules at their native rates (Tasks 3–6), 44.1/48/96 kHz (and 176.4 kHz for the sampler) tests, allocation tests per component, CPU budget (Task 18), validators (Tasks 17–18). §11 M4 row: Stages, Tides 2, Marbles, Peaks; matrix; init patches.
- **Gain staging (M2 ruling I4):** −12 dB per voice with migration (Tasks 13–15), per-engine trims and Audio Input chord scaling (Task 12), soft limiter reviewed and kept (Task 12), Resonator velocity de-click (Task 12).
- **Lessons applied.** Independent references written from firmware loops (Tasks 3–6); every test in this plan was compiled and run during planning on a copy of the repository with the plan's code (512 test cases passing through Task 16; the benchmark and level reports ran too); IDs append-only with migration rules beside every stepped list (Tasks 2, 13); shared host defaults checked on every mod engine (Task 13); engine switching reuses `Synth::switchEngine` with the stash, scope included (Task 13); the pending pattern for patch data (Task 14); manual updated in every task; Amp/pan snap on a voice's first tick; NaN recovery per slot; CPU measured with every mod slot in use (Task 18).
- **Not in M4:** Part FX (M5; the FX init patch's Granular), the route and Custom-shape editors and MIDI learn UI (M6), factory presets (M7).
