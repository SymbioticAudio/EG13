# M6b: live modulation, matrix editing, keyboard, settings (implementation plan)

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the EG13 editor shows modulation moving and lets you edit the matrix. It also lets you play notes from a keyboard, drive every control from the computer keyboard, and set app-wide preferences.

**Architecture:**
- The audio thread gains two publishes, both once per chunk, with relaxed atomics and no allocation, in the same style as `activeVoices_`:
  - the final modulated value of every modulation destination of the part's most recently started voice;
  - the min/max of every mod output since the last snapshot.
- `UiSnapshot` and `EG13Snapshot` carry both.
- Routes stay patch data on the main thread. The C API reads and writes them directly through `Synth::patch()` and `Synth::setRoute`, then marks the host state dirty.
- UI notes travel the same SPSC queue as edits, as `UiEvent::NoteOn` and `UiEvent::NoteOff`. On the audio thread they call a new `Synth::uiNoteOn` and `Synth::uiNoteOff`, which apply the Mode and key-range rules but ignore MIDI channels.
- Every new look lives in the view and style files and is gated by the owner, as in M6a.

**Tech stack:** C++17, CLAP 1.x, clap-wrapper v0.16.0, Swift 6 (tools 6.0) and SwiftUI on macOS 14+, symbiotic-design 0.1.0, Catch2, Swift Testing.

**Spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md`: §6 (modulation) and §8 (UI), as amended by Task 1. The facts this plan relies on, with file:line references, are in `.superpowers/m6b-facts.md`; it is git-ignored, so regenerate it if it's missing.

## Planner decisions (for owner review with this plan)

Task 1 writes these into spec §8. The owner can veto any of them here, or at the gates.

1. **Which voice the display shows.** Live values come from the part's most recently started voice. This is the same rule the matrix uses for Part Level (§6). Per-part destinations (Part FX, Part Level, per-part mod slots) show the part value.
2. **Modulation on a knob.**
   - A modulated knob draws a thin second arc, the "modulation ring", from its set value to its live modulated value, plus a dot at the live position.
   - A knob with routes into it but no current movement still shows the ring's range: the sum of its route amounts, clamped.
   - Everything else about the knob is unchanged.
3. **Motion chips.**
   - Each module card's header strip shows one chip per route that targets the card, written source → parameter, e.g. "M1 → CUT".
   - A chip's fill brightness follows the source's live level.
   - Clicking a chip opens that route in the matrix editor.
   - Cards have room in the header strip; knobs do not.
4. **Mod shape display.**
   - Each mod mini-card shows a 2 s trace of the slot's first output, at display rate.
   - Each frame draws a min–max bar from the core's publish, so fast LFOs read as a band rather than aliasing.
5. **Matrix editor.**
   - The bottom matrix row of 12 amount knobs goes away. This settles the open owner call from layout round 2.
   - The right-hand column gets a **MODS / MATRIX** switch. MATRIX shows 12 route rows, each with:
     - a source menu, a via menu and a destination menu (all LCD style);
     - a small amount knob;
     - a clear button.
   - The freed vertical space goes to the cards.
6. **"Modulate from…"** is a knob's context menu (right-click or ⌃-click).
   - It lists the sources. Picking one fills the first empty route with that source → this knob, at amount +25 %.
   - When all 12 routes are used, the item is disabled and reads "Matrix full".
   - The same context menu has "Remove modulation" items, one per route into the knob.
7. **Keyboard.**
   - A custom SwiftUI keyboard (no AudioKit; YAGNI).
   - It is a collapsible 84 pt strip under the bottom bar, inside the 1200 × 760 canvas. The window keeps its aspect; the columns already scroll.
   - 3 octaves are visible, with octave −/+ buttons covering C0–C8. The default view starts at C3.
   - Velocity comes from the vertical click position, 0.2 at the top to 1.0 at the bottom (the existing `EG13KeyMap.velocity`), or fixed at 100 (0.79) in settings.
   - Notes from the keyboard sound in EG13 only. They aren't sent to the host and can't be recorded; the manual says so.
8. **Keyboard focus.**
   - Tab and ⇧Tab move through every control in reading order: header, part tabs, cards (picker, then knobs), Mods or Matrix, bottom bar, keyboard toggle.
   - The arrows adjust the focused control, as now. Return opens a picker or menu. Delete resets to the default.
   - The focused control shows a 2 pt LCD-green (#AFD22C) focus ring.
9. **Settings and About.** The cog opens a sheet with two tabs.
   - **Settings** (app-wide, saved to `~/Library/Application Support/SymbioticAudio/EG13/settings.json`):
     - default window size for new editors: 75, 100, 125, 150 or 200 %;
     - keyboard shown when the editor opens;
     - keyboard velocity: by position or fixed;
     - modulation display on or off.
   - Latency, Soft Clip and the other master settings stay in the header MASTER group (approved in M6a).
   - **About:** product name, version and build, the §1.1 credit line, GPL-3.0 and the repository URL.
10. **Out of scope:**
    - editing Custom envelope shapes;
    - CC-learn UI;
    - drag-to-assign;
    - preset browsing (M7).
    `docs/manual/limitations.md` keeps listing them.

## Global Constraints

- **Licence and commits:** licence GPL-3.0. Commit as `Westlee Latta <symbioticaudio@proton.me>` with the session trailers. Never rewrite pushed history.
- **Trademark:** never use "Mutable Instruments" as a product name, in the UI or in the docs.
- **symbiotic-design:** use its tokens, type and icons; never fork or copy them. Request missing ones in `~/workspace/symbiotic-design/HANDOFF.md` under "Requests from Mutable/EG13". No SF Symbols; `scripts/build-ui.sh` fails on them.
- **Dependencies:** pinned with `exact:`; symbiotic-design `0.1.0` only. No AudioKit.
- **Platform:** macOS deployment target 14.0. The Linux build and CI build no UI and stay green.
- **Audio thread:** no allocation, no locks, no Swift. The UI never calls `Synth` from the audio thread, and the audio thread never reads patch data that the main thread writes, except through the existing patch hand-off.
- **ObjC classes:** every ObjC-visible class is prefixed `EG13`; `scripts/check-objc-prefix.sh` checks the .clap, .vst3 and .component.
- **Approved design:** the M6a control and layout decisions (spec §8) stay. New visuals pass an owner gate.
- **Manual:** every user-facing behaviour is documented in `docs/manual/` in the same task.
- **No back-compat:** EG13 is unreleased, so there is no state migration and no back-compat code.
- **Isolation:** the per-parameter observation isolation from M6a is kept. A change to one parameter's value or modulation must not invalidate other parameters' controls.

## Review Focus

1. **The editor closes while a key is held.** Every UI note gets its note-off. Test: Task 3, "Destroying the editor releases held UI notes".
2. **A route is set to a destination the current engine doesn't use** (an unused P), or the engine changes under a route. Neither the chip nor the ring may show for a hidden knob, and nothing may crash. Test: Task 6, "Routes into hidden P's show no chip or ring".
3. **The Mode changes while UI notes are held** (Split → Layer), or the split note moves. The note-off releases exactly the parts the note-on reached. Test: Task 2, "uiNoteOff releases the parts uiNoteOn reached after a mode change".
4. **A host state load or undo replaces the routes while the matrix editor is open.** The editor shows the loaded routes within one frame, and a menu that's open closes without applying a stale choice. Test: Task 6, "Generation change reloads routes".
5. **The settings file is missing, corrupt, or from a newer build.** Defaults are used and the file is rewritten only when the user changes a setting. Test: Task 10, "Corrupt settings load as defaults".

---

### Task 1: Spec §8 for M6b, and housekeeping

**Files:**
- Modify: `docs/superpowers/specs/2026-10-20-eg13-design.md` §6 and §8. Add the planner decisions above as approved with this plan, and note "settled" on the matrix-row question.
- Modify: `core/mod/mod_routes.h:61` to drop the stale "Part FX P's are None until M5" comment, and `docs/superpowers/m6-resume.md` to remove the reference to the deleted `docs/superpowers/m6-spike/`.
- Modify: `.github/workflows/ci.yml`, where the macos job pins Xcode. Add a step `sudo xcode-select -s /Applications/Xcode_<latest available on macos-15>.app` and print `swift --version`. In `README.md`, write "Swift 6.0 or later (developed with 6.4 / Xcode 27.1)".

- [ ] **Step 1:** Make the edits. Run `swift test --package-path ui/EG13UI` and the mac build and ctest. Expected: pass.
- [ ] **Step 2:** Commit `M6b: spec §8 for M6b, CI Xcode pin, housekeeping`.

### Task 2: Core UI notes

**Files:**
- Modify: `core/synth/synth.h|.cpp`
- Test: `tests/test_ui_notes.cpp`

**Interfaces:**
- Produces:
  - `void Synth::uiNoteOn(int partHint, int key, float velocity)`
  - `void Synth::uiNoteOff(int key)`
  - `void Synth::uiAllNotesOff()`
  All three are audio-thread only.
- Routing rules:
  - The same as `noteOn` for Mode, split note and each part's key range, but the channel is ignored.
  - In Multi, only `partHint` plays.
  - Single always plays Part A.
  - Releases use a separate `uiRouted_[128]` part mask, so a UI note and a MIDI note on the same key don't release each other.

- [ ] **Step 1: Failing tests:**
  - "uiNoteOn follows Split and Layer and ignores channels": in Split at 60, key 59 → A only, key 60 → B only. In Layer → both. Part B on MIDI channel 2 still sounds.
  - "uiNoteOn in Multi plays the hinted part".
  - "uiNoteOff releases the parts uiNoteOn reached after a mode change": note on in Split, change to Layer, note off → no voice active.
  - "A UI note and a MIDI note on one key are independent".
- [ ] **Steps 2–4:** Implement; mac ctest and the Docker Linux check pass.
- [ ] **Step 5:** Commit `M6b: UI note routing in the core`.

### Task 3: Bridge: UI notes and drain-side pump request

**Files:**
- Modify: `plugin/src/ui_bridge.h|.cpp`, `plugin/src/eg13_plugin.cpp`
- Test: `tests/test_ui_bridge.cpp`

**Interfaces:**
- Consumes: Task 2's `uiNoteOn`, `uiNoteOff` and `uiAllNotesOff`.
- Produces:
  - `UiEvent::Type` gains `NoteOn` and `NoteOff`, with `{int part; int key; float velocity}` packed into the existing event.
  - `UiBridge::noteOn(int part, int key, float velocity)`, `noteOff(int key)` and `releaseAllNotes()` (main thread). The bridge tracks held keys like open gestures. Gui destroy calls `releaseAllNotes()` right after `closeAllGestures()`.
  - **Pump request:** `pump()` no longer re-requests itself. `drain()` calls `requestPump_` once when it finds the backlog flag set, and the flag is an atomic the main thread sets when the overflow is non-empty. This replaces the M6a re-request loop.

- [ ] **Step 1: Failing tests:**
  - "Destroying the editor releases held UI notes": noteOn on keys 60 and 64, then `releaseAllNotes`, then drain → 2 uiNoteOff.
  - "UI notes keep order with edits".
  - "A never-draining host gets no repeated pump requests": fill past 1024, call `pump` 10 times, and count the requestPump calls → 1.
  - "Drain asks for one pump while a backlog remains".
- [ ] **Steps 2–4:** mac ctest, Linux and validate.sh pass.
- [ ] **Step 5:** Commit `M6b: UI notes through the bridge; drain requests pumps`.

### Task 4: Modulation publish in the snapshot

**Files:**
- Modify: `core/synth/ui_snapshot.h`, `core/synth/synth.cpp`, `core/synth/part.h|.cpp`, `core/mod/part_modulation.h` (only if an accessor is needed)
- Test: `tests/test_ui_snapshot.cpp`

**Interfaces:**
- Produces, in `UiSnapshot`:
  - `float modValue[kNumParts][kNumDestinations]`: the final normalized value (0..1) at each destination for the last-started voice, or the part for per-part destinations. Pitch is in semitones / 48 + 0.5. Code 0 and the unused codes hold NaN.
  - `float modOutMin[kNumParts][kNumModSlots][kMaxModOutputs]` and `modOutMax[...]`: the min and max since the last `uiSnapshot` call, read-and-reset like `partPeak`. For a per-voice slot they come from the last-started voice.
- Published once per chunk, with relaxed atomic stores and no allocation. Cost is measured against the existing benchmark: under 0.5 % of the 128-sample block at 48 kHz for both parts.

- [ ] **Step 1: Failing tests:**
  - "modValue shows the route's effect": init patch, hold a note, set Mod 2's route amount → `modValue[0][FilterP1]` differs from the base Filter P1 value by the expected envelope × 0.3, within 1e-3.
  - "per-part destinations use the part value".
  - "mod output min/max are read-and-reset".
  - "unused destinations are NaN".
  - "no allocation in the publish": use the existing allocation-counter pattern.
- [ ] **Steps 2–4:** Benchmark entry `[.benchmark] modulation publish`; mac and Linux pass.
- [ ] **Step 5:** Commit `M6b: publish modulated values and mod outputs`.

### Task 5: C API for routes, names, notes, modulation and version

**Files:**
- Modify: `core-api/include/eg13/core_api.h`, `plugin/src/core_api.cpp|.h`, `plugin/src/eg13_plugin.cpp` (CLAP `state.mark_dirty`), `tests/core_api_c_check.c`
- Test: `tests/test_core_api.cpp`

**Interfaces:**
- Produces:
  - `typedef struct EG13Route { uint16_t source, destination, via; double amount; } EG13Route;`
  - New entries in the `EG13CoreAPI` table:
    - `bool (*route_get)(void*, uint32_t part, uint32_t route, EG13Route* out)`. The amount is the host param value.
    - `bool (*route_set)(void*, uint32_t part, uint32_t route, uint16_t source, uint16_t destination, uint16_t via)`. It calls `Synth::setRoute` and then `host->state->mark_dirty`. The amount is edited through the existing begin/edit/end on the route-amount param.
    - `bool (*source_name)(void*, uint32_t part, uint16_t code, char*, uint32_t)`. It returns false for unused or reserved codes. Mod outputs are named after the slot's engine, e.g. "Mod 1 Envelope", "Mod 3 LFO Out 2".
    - `bool (*destination_name)(void*, uint32_t part, uint16_t code, char*, uint32_t)`. It is engine-aware, e.g. "Filter: Cutoff", and returns false for codes the current engine hides.
    - `uint16_t (*destination_for_param)(void*, uint32_t id)`, which returns 0 when the param isn't a modulation destination.
    - `void (*note_on)(void*, uint32_t part_hint, int32_t key, float velocity)`, `void (*note_off)(void*, int32_t key)` and `void (*all_notes_off)(void*)`.
    - `bool (*version)(void*, char*, uint32_t)`: e.g. "0.6.0 (abc1234)" from the CMake project version and the git short hash.
  - `EG13Snapshot` gains `float mod_value[EG13_NUM_PARTS][EG13_NUM_DESTINATIONS]`, `mod_out_min[...]` and `mod_out_max[EG13_NUM_PARTS][4][8]`, with `EG13_NUM_DESTINATIONS = 144`. It is static_asserted against the core.
  - `route_set` rejects out-of-range part, route or codes, returning false and changing nothing.

- [ ] **Step 1: Failing tests:**
  - "route_set then route_get round-trips and marks dirty".
  - "route_set rejects bad codes".
  - "names follow the engine".
  - "destination_for_param maps Filter P1 to 33".
  - "note_on reaches the synth via drain".
  - "snapshot carries mod values".
  - `core_api_c_check.c` still compiles as C11 with -Wpedantic -Werror.
- [ ] **Steps 2–4:** mac, Linux and validate.sh pass.
- [ ] **Step 5:** Commit `M6b: C API for routes, names, notes, modulation and version`.

### Task 6: Swift model for routes, modulation and notes, with generated tables

**Files:**
- Create:
  - `ui/EG13UI/Sources/EG13UI/Model/EG13Route.swift`
  - `Model/EG13ModTrace.swift`
  - `tools/ui_tables/dump_ui_tables.cpp` (CMake target `eg13_ui_tables`). It writes every slot's engine names, P names and the value strings of each P, in the format of `EngineParamInfo::format`, as JSON.
  - `ui/EG13UI/Sources/EG13UIFixtures/core-tables.json` (a new library target with resources, used only by the preview and the tests)
- Modify: `EG13Core.swift`, `EG13CoreAPIClient.swift`, `EG13Synth.swift`, `EG13Param.swift`, `EG13EngineCatalog.swift` (now loaded from the fixture JSON), `Tests/EG13UITests/EG13ValueStrings.swift` (replaced by the JSON), `EG13FakeCore.swift`, `EG13PreviewCore.swift`, `CMakeLists.txt`
- Test: `Tests/EG13UITests/EG13RouteModelTests.swift`, plus a ctest `ui_tables_up_to_date` that runs `eg13_ui_tables` to a temp file and diffs it against the committed JSON.

**Interfaces:**
- `EG13Core` gains the Task 5 calls in Swift form:
  - `route(part:index:) -> EG13Route`
  - `setRoute(part:index:source:destination:via:) -> Bool`
  - `sourceName`, `destinationName`, `destination(forParam:)`
  - `noteOn(partHint:key:velocity:)`, `noteOff(key:)`, `allNotesOff()`
  - `version`
- `EG13DisplayFrame` gains `modValues: [[Float]]`, `modOutMin` and `modOutMax`.
- `EG13Param` gains `modulatedNormalized: Double?` (nil when unmodulated) and `modulationRange: ClosedRange<Double>?`. Both are written only when they change by more than 0.001, to keep isolation.
- `EG13Synth` gains:
  - `routes(part:) -> [EG13Route]`, which reloads on `engineGeneration` change and after its own `setRoute`;
  - `routes(into id:, part:) -> [EG13Route]`;
  - `firstFreeRoute(part:) -> Int?`;
  - `modulate(id:from source:)`, which uses the first free route at amount +0.25 and returns false when the matrix is full;
  - `removeRoute(part:index:)`;
  - `sources(part:) -> [(code: UInt16, name: String)]` and `destinations(part:)`;
  - `modTrace(part:slot:) -> EG13ModTrace` (60 frames of min/max);
  - `noteOn(key:velocity:)` (part hint = `selectedPart`), `noteOff(key:)`, `releaseAllNotes()` (called in `shutdown()`).

- [ ] **Step 1: Failing tests:**
  - "modulate fills the first free route at +25 %".
  - "Matrix full disables modulate".
  - "Routes into hidden P's show no chip or ring".
  - "Generation change reloads routes".
  - "A modulation change to X does not invalidate Y" (withObservationTracking).
  - "mod trace keeps 60 frames".
  - "shutdown releases held notes".
- [ ] **Steps 2–4:** swift test, mac ctest including `ui_tables_up_to_date`, and Linux pass.
- [ ] **Step 5:** Commit `M6b: Swift model for routes, modulation and notes; generated UI tables`.

### Task 7: Modulation visuals

**Files:**
- Modify: `Style/EG13KnobStyle.swift` (modulation ring and dot), `Controls/EG13Knob.swift`, `Views/EG13ModuleCard.swift` (motion chips in the header strip)
- Create: `Views/EG13MotionChip.swift`, `Views/EG13ModTraceView.swift`
- Modify: the mod mini-card in `Views/EG13ModuleCard.swift` (trace), and `Sources/EG13UIPreview/*`: a "Modulation" page that animates the preview core's mod values and outputs, plus `--snapshot-modulation <dir>`
- Test: `Tests/EG13UITests/EG13ModulationViewTests.swift` at model level: "chips list every visible route into the card in route order" and "ring range is the clamped sum of amounts".

- [ ] **Step 1:** Write the failing tests, then implement. Drawing is `Canvas`- or `Shape`-based, with no per-frame allocation in `body`.
- [ ] **Step 2:** Measure with the preview's `--drag-load`-style flag `--mod-load`: the init patch with a held chord, 3 modulated knobs and 3 traces animating. **Budget: under 10 % of one core.** Record the number in `docs/manual/interface.md`. If it's over budget, report BLOCKED with a profile, and don't loosen the budget.
- [ ] **Step 3:** Manual section "Modulation display" in `interface.md`. Commit `M6b: modulation ring, motion chips and mod traces`.

### Task 7a: Owner design gate: modulation visuals

- [ ] **Step 1:** Render `docs/superpowers/m6-design/modulation-r1/`: Part A and Part B with a held chord, a close-up of a ring, a card header with chips, and a mod column with traces, all at 100 % and 75 %.
- [ ] **Step 2: STOP for the owner.** Record the feedback in `m6-resume.md` as "Modulation design log, round N".
- [ ] **Step 3:** Apply the feedback in style and view files only, re-run the Task 6 and 7 tests, and repeat until approved.
- [ ] **Step 4:** Commit each round as `M6b design: modulation round N`. Record the approval in the log and in spec §8.

### Task 8: Matrix editor and "Modulate from…"

**Files:**
- Create: `Views/EG13MatrixEditor.swift` (12 route rows), `Controls/EG13RouteMenu.swift` (an LCD-style menu for source, via and destination)
- Modify: `Views/EG13RootView.swift` (the MODS / MATRIX switch in the right column; remove the bottom matrix row), `Model/EG13Controls.swift` (control list: route rows replace the matrix knobs), `Controls/EG13Knob.swift` (context menu), `Views/EG13MatrixAmounts.swift` (delete)
- Test: `EG13LayoutTests` (reachability now includes the route menus), plus "context menu offers Modulate from and Remove modulation".

- [ ] **Step 1:** Write the failing tests. Every route-amount param must still be reachable exactly once, now through a matrix row.
- [ ] **Step 2:** Implement.
  - A destination menu groups by card (Source, Filter, Shaper, Part FX, Mod 1–4, Voice), lists only visible P's, and uses engine names.
  - A cleared route is source None, destination None, via None, amount 0.
  - A chip click (Task 7) sets the right column to MATRIX and scrolls to that row.
- [ ] **Step 3:** Manual sections "Matrix" and "Modulate from". Commit `M6b: matrix editor and Modulate from`.

### Task 9: Keyboard and focus navigation

**Files:**
- Create: `Views/EG13Keyboard.swift`, `Views/EG13KeyboardBar.swift` (toggle and octave −/+)
- Modify: `Views/EG13RootView.swift`, `Model/EG13KeyMap.swift`, `Controls/EG13ControlSurface.swift` (focus ring, Return and Delete), `Entry/EG13UIController.swift` (the hosting view accepts first responder)
- Test: `EG13KeyboardTests` ("key under point maps to note and velocity", "octave shift clamps to C0–C8", "release on drag-off and on hide") and `EG13FocusTests` ("tab order covers every control in reading order": walk the control list).

- [ ] **Step 1:** Write the failing tests, then implement to the planner decisions 7 and 8.
  - A drag across keys plays legato: the next note's note-on comes before the previous note's note-off.
  - Hiding the keyboard releases any held key.
- [ ] **Step 2:** Manual: in a host, check that Tab, the arrows and Return reach the editor. Some hosts take keys; write down what each host does, and document it.
- [ ] **Step 3:** Manual sections "Keyboard" and "Keyboard control". Commit `M6b: keyboard and keyboard focus navigation`.

### Task 10: Settings and About

**Files:**
- Create: `Model/EG13Settings.swift` (a Codable struct, plus a store reading and writing the JSON path from decision 9, atomically), `Views/EG13SettingsSheet.swift`
- Modify: `Views/EG13HeaderView.swift` (the cog opens the sheet; the popover is removed), `Entry/EG13Entry.swift` (the initial zoom comes from settings when the plugin passes 0), `plugin/src/gui.cpp` (pass 0 = "use the UI default")
- Test: `EG13SettingsTests`:
  - "Corrupt settings load as defaults";
  - "unknown keys are ignored";
  - "saving writes atomically";
  - "the default zoom applies to new editors only".

- [ ] **Step 1:** Write the failing tests, then implement. Settings are app-wide and never stored in the project.
- [ ] **Step 2:** Manual section "Settings and About". Commit `M6b: settings and About`.

### Task 10a: Owner design gate: matrix, keyboard, focus, settings

- [ ] **Step 1:** Render `docs/superpowers/m6-design/m6b-layout-r1/`:
  - the editor with MATRIX shown, and with the keyboard open;
  - a focused knob;
  - the settings and About sheets;
  - the knob context menu content, rendered on its own.
- [ ] **Step 2: STOP for the owner.** Record the feedback as "M6b layout design log, round N".
- [ ] **Step 3:** Apply the feedback and re-run the Tasks 6–10 tests until the owner approves.
- [ ] **Step 4:** Commit each round as `M6b design: layout round N`. Record the approval in the log and in spec §8.

### Task 11: README, manual, validators and costs

**Files:**
- Modify: `README.md`, `docs/manual/interface.md`, `docs/manual/limitations.md` (only the out-of-scope items remain)

- [ ] **Step 1:** Run `scripts/validate.sh` and `check-objc-prefix.sh` on .clap, .vst3 and .component, the mac and Swift suites, and the Docker Linux check. All must pass.
- [ ] **Step 2:** Record the costs:
  - idle and mod-load main thread (budget 10 %);
  - the publish benchmark (budget 0.5 %);
  - the drain benchmark (budget 0.1 %).
- [ ] **Step 3:** Commit `M6b: README, manual and validators`.

## Self-review notes

- **Spec coverage:**
  - Live modulation display: Tasks 4–7.
  - Matrix editing and "Modulate from…": Tasks 5, 6 and 8.
  - Keyboard: Tasks 2, 3 and 9.
  - Focus navigation: Task 9.
  - Settings and About: Task 10.
  - The latency mode and soft clip "in settings" are met by the approved header MASTER group (decision 9).
- **M6a follow-ups:**
  - Generated tables: Task 6.
  - Pump re-request: Task 3.
  - Drag CPU: Task 7's budget covers the same hot path.
  - CI Xcode pin: Task 1.
- **Names used across tasks:**
  - `uiNoteOn`, `uiNoteOff`, `uiAllNotesOff`, `UiEvent::NoteOn`, `UiEvent::NoteOff`;
  - `modValue`, `modOutMin`, `modOutMax`, `EG13Route`, `route_get`, `route_set`, `destination_for_param`;
  - `EG13Param.modulatedNormalized`, `modulationRange`, `EG13ModTrace`, `modulate(id:from:)`.
