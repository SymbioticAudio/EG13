# M6a: UI foundation and part editor (implementation plan)

> **For agentic workers:** REQUIRED SUB-SKILL: use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task by task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** EG13 opens its own SwiftUI window in CLAP, AU and VST3 hosts. The window shows one part at a time and lets you edit every parameter of that part and the master. It also shows the meters.

**Architecture:**
- A SwiftPM package, `ui/EG13UI`, is built as a static library and linked into the plugin. It is reached only through two C headers: `core_api.h` (plugin → UI function table) and `ui_entry.h` (UI → plugin entry points).
- The plugin implements the CLAP `gui` extension for Cocoa. clap-wrapper maps it to AU and VST3 views.
- Parameter edits travel UI → main thread → an SPSC queue → the audio thread. There they become `setParam` calls, plus CLAP gesture and value events back to the host.
- The UI polls a snapshot at display rate.

**Tech stack:** C++17, CLAP 1.x and clap-wrapper v0.16.0, Swift 6 (tools 6.0) and SwiftUI on macOS 14+, symbiotic-design 0.1.0, Catch2, XCTest via `swift test`.

**Spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md` §8, as amended by Task 1 with the owner decisions in `docs/superpowers/m6-resume.md`.

**Split (planner's decision, for owner review):** M6 is split into two plans.
- **M6a (this plan):** the plumbing, the build, the controls, the one-part layout with every control, and the meters.
- **M6b (next plan):** live modulation display (modulated values on knobs, the motion chips, mod shape displays); matrix editing ("Modulate from…", adding and removing routes, choosing sources and destinations); the collapsible keyboard (AudioKit Keyboard if it can be styled, otherwise our own); keyboard focus navigation; settings and about.
- **Why split:** each half is independently shippable. The M6b items need new audio-to-UI data paths, which are easier to design once M6a's snapshot path exists. Preset browsing in the header belongs to M7.

**Design iteration (owner, 2026-10-24): the UI design is not final.**
- Tasks 2 to 6 are design-independent plumbing.
- Every look-and-layout choice lives in `EG13Palette`, `EG13KnobStyle` and the view files, so changing the design never touches the plumbing.
- Two owner gates stop execution until the owner signs off: **Task 7a**, after the controls; and **Task 9a**, after the layout.
- Each gate shows the design in the preview app (Task 7) and in a host. Feedback rounds follow until the owner approves.
- Feedback that changes §8 updates the spec in the same round.

## Global Constraints

- License GPL-3.0. Commit as `Westlee Latta <symbioticaudio@proton.me>` with the session trailers. Never rewrite pushed history.
- Never use "Mutable Instruments" as a product name, in the UI or in the docs.
- symbiotic-design: use its tokens, type and icons; never fork or copy them. Missing tokens or icons are requested in `~/workspace/symbiotic-design/HANDOFF.md` under "Requests from Mutable/EG13". EG13 owns its colour palette locally. No SF Symbols.
- Dependencies are pinned with `exact:`. M6a uses symbiotic-design `0.1.0` only: Tokens and Icons, with Brand behind `#if canImport(SymbioticBrand)`. AudioKit Controls is dropped. AudioKit Keyboard and Tonic are not used in M6a.
- macOS deployment target 14.0, matching `CMAKE_OSX_DEPLOYMENT_TARGET`. The Linux build and CI build no UI and stay green.
- Audio thread: no allocation, no locks, no Swift. The UI never calls `Synth` from the audio thread.
- Every ObjC-visible class EG13 ships is prefixed `EG13`, and module aliases rename shared packages, so two Symbiotic plugins in one host never clash. `scripts/check-objc-prefix.sh` checks this.
- Every user-facing behaviour is documented in `docs/manual/` in the same task.
- Window 1200 × 760 points at 100 %, scalable from 75 to 200 %.

## Review Focus

1. **Open and close the editor many times, and run two instances at once.** Expect no leak, no crash and no duplicate-class warning. The display timer must stop when the view is destroyed. Tests: Task 6 "Editor create/destroy 50 times leaves no live UI objects", Task 4 "Two bridges are independent".
2. **The view closes mid-drag.** Every begin gesture is still matched by an end gesture to the host. Test: Task 3 "Destroying the bridge mid-gesture emits the end gesture".
3. **The engine is switched while the editor is open,** by the UI, host automation or state load. Knob names and the number of visible knobs follow within one frame, and unused P's are hidden. Test: Task 8 "Engine change refreshes descriptors and hides unused P's".
4. **The host changes a value** (automation, undo or state load). The knob follows without echoing an edit back to the host. Test: Task 8 "Snapshot updates do not emit edits".
5. **The host offers an odd size, or a size outside the limits.** `adjust_size` keeps the 1200:760 aspect ratio, clamped to 75–200 %. Test: Task 6 "adjust_size keeps the aspect and clamps".

---

### Task 1: Spec §8 and the M6 split

**Files:**
- Modify: `docs/superpowers/specs/2026-10-20-eg13-design.md` (§8, and the §11 M6 row)
- Modify: `docs/superpowers/m6-resume.md`
- Create: `docs/manual/interface.md`

- [ ] **Step 1: Rewrite §8 with the owner decisions from `m6-resume.md`.** They supersede the "always dark" commit.
  - **Palette:** EG13-owned and panel-modelled, light panels on a mid-grey window. Part A is teal, Part B pink.
  - **Layout:** "Columns + motion chips".
  - **Knob look.**
  - **Controls:** custom EG13 SwiftUI controls, AudioKit Controls dropped.
  - **Header:** the Mode control sits in the header beside the A/B tabs. The latency mode goes in settings (M6b); until then it stays a host parameter.
  - **Unused P's:** hidden, so a slot shows `EngineInfo::paramCount` knobs.
  - **UI split:** M6a/M6b as above.
- [ ] **Step 2:** Mark `m6-resume.md` as resumed, pointing at this plan. Write the `interface.md` skeleton: window, scaling, part tabs, columns and bottom bar. Later tasks fill in the sections.
- [ ] **Step 3: Commit** `M6a: spec §8 with the owner's UI decisions; M6 split`.

### Task 2: UI snapshot data in the core

**Files:**
- Modify: `core/synth/synth.h|.cpp`
- Create: `core/synth/ui_snapshot.h`, `tests/test_ui_snapshot.cpp`

**Interfaces:**
- Produces:
  - `struct UiSnapshot { std::array<float, kParamCount> values; float partPeak[kNumParts][2]; int activeVoices[kNumParts]; uint32_t engineGeneration; };`
  - `void Synth::uiSnapshot(UiSnapshot* out) const` (main thread).
  - The audio thread writes the part peaks as relaxed atomics, once per block. Each value is the absolute peak of that part's output after level and pan, held as a max until the next read: read-and-reset uses `exchange(0)`.
  - `engineGeneration` increments on every engine switch: UI, host or state load.

- [ ] **Step 1: Failing tests.**
  - "Part peaks report each part's output peak and reset on read": one note on Part A only. Part A's peak is > 0, Part B's == 0. A second read with no audio gives 0.
  - "Snapshot values follow setParam and loadParams".
  - "Engine generation changes on every engine switch, including state load".
  - "uiSnapshot allocates nothing" (ScopedAllocationCounter).
- [ ] **Step 2: Run** `ctest --preset mac -R "UiSnapshot|Part peaks"`. Expected: FAIL. **Step 3: Implement.** **Step 4: Run** the whole suite on mac and in the Docker Linux check. Expected: PASS.
- [ ] **Step 5: Commit** `M6a: UI snapshot (values, part peaks, voices, engine generation)`.

### Task 3: UI bridge and SPSC queue

**Files:**
- Create: `core/util/spsc_queue.h` (from `docs/superpowers/m6-spike/core/util/spsc_queue.h`), `plugin/src/ui_bridge.h|.cpp` (from the spike, rewritten against the current API), `tests/test_spsc_queue.cpp`, `tests/test_ui_bridge.cpp`
- Modify: `plugin/src/eg13_plugin.cpp` (drain in `process` and `paramsFlush`), `tests/CMakeLists.txt` (the bridge test links `eg13-impl`'s sources, or the bridge moves into a small `eg13_ui_bridge` library both link)

**Interfaces:**
- Produces:
  - `struct UiEvent { enum class Type : uint8_t { BeginEdit, Edit, EndEdit }; Type type; uint32_t id; double value; };`
  - `class UiBridge { public: explicit UiBridge(Synth&, std::function<void()> requestFlush); void beginEdit(uint32_t id); void edit(uint32_t id, double v); void endEdit(uint32_t id); void pump(); void drain(const clap_output_events_t* out); void closeAllGestures(); };`
  - Threads: main for everything except `drain`, which runs on the audio thread or in `paramsFlush`.
  - The queue is `SpscQueue<UiEvent, 1024>`. Overflow lands in a main-thread `std::deque` and is retried on `pump()`. `requestFlush` calls `host->request_flush` when the plugin is not processing.
  - `drain` applies `setParam` and emits `CLAP_EVENT_PARAM_GESTURE_BEGIN/END` and `CLAP_EVENT_PARAM_VALUE` to `out`, in queue order.
  - The bridge tracks open gestures. `closeAllGestures()`, also called from the destructor, enqueues EndEdit for each.
  - Notes from the UI are M6b. Leave out the spike's NoteOn/NoteOff.

- [ ] **Step 1: Failing tests:**
  - the spike's 4 SPSC cases;
  - "Edits reach the synth and the host in order" (a fake `clap_output_events_t` records events);
  - "Overflow is retried, not dropped" (2000 edits with no drain, then drains until empty: all 2000 arrive);
  - "Destroying the bridge mid-gesture emits the end gesture";
  - "drain allocates nothing".
- [ ] **Steps 2–4: Run, implement, run.** Expected: PASS on mac and Linux. The bridge has no Apple code.
- [ ] **Step 5: Commit** `M6a: UI bridge with lock-free edit queue and host gestures`.

### Task 4: The C core API

**Files:**
- Create: `core-api/include/eg13/core_api.h`, `core-api/include/eg13/ui_entry.h` (both from the spike, updated), `plugin/src/core_api.cpp|.h`, `tests/test_core_api.cpp`
- Modify: `plugin/CMakeLists.txt` (include dir)

**Interfaces:**
- Produces, pure C:
  - Constants: `EG13_MAX_PARAMS` (= 323, with `static_assert(EG13_MAX_PARAMS == eg13::kParamCount)` in `core_api.cpp`), `EG13_NUM_PARTS 2`, `EG13_TEXT_SIZE 64`.
  - `EG13Slot` mirrors `params::Slot` exactly, Part 0 … Matrix 9. `EG13SlotKind` mirrors `SlotKind`, with a static_assert for each value.
  - `EG13ParamInfo { uint32_t id; uint32_t flags; double min, max, def; }` (`EG13_PARAM_STEPPED = 1`).
  - `EG13Snapshot { uint32_t param_count; double values[EG13_MAX_PARAMS]; float part_peak[2][2]; int32_t active_voices[2]; uint32_t engine_generation; }`.
  - `EG13CoreAPI`, all taking `void* context`: `param_count`, `param_info`, `param_name`, `param_text`, `param_index(id)`, `engine_count(slot_kind)`, `engine_name(slot_kind, engine, buf, size)`, `engine_param_count(slot_kind, engine)`, `current_engine(slot_kind, part)`, `begin_edit(id)`, `edit(id, value)`, `end_edit(id)`, `snapshot(EG13Snapshot*)`, `request_resize(width, height)`.
  - `ui_entry.h` is as in the spike, minus `eg13_ui_link_check`, which moves to Task 5's test.
  - `eg13::makeCoreApi(Plugin&) -> EG13CoreAPI` is implemented over `Synth::paramName`, `formatParam`, `slotEngineInfo`, `uiSnapshot` and `UiBridge`.

- [ ] **Step 1: Failing tests through the function table only:**
  - "param_name follows the engine (Part FX P5: Density with Granular, Spread with Resonator)";
  - "engine_param_count matches EngineInfo for every slot and engine";
  - "edit through the API reaches the synth after a drain";
  - "Two bridges are independent" (two Plugin instances; edits to one leave the other unchanged).
- [ ] **Steps 2–4.** **Step 5: Commit** `M6a: C core API for the UI`.

### Task 5: Build the Swift package into the plugin

**Files:**
- Create: `ui/EG13UI/` (moved from `docs/superpowers/m6-spike/ui-swift/`, then trimmed), `scripts/build-ui.sh`, `scripts/check-objc-prefix.sh`, `cmake/eg13_ui.cmake`
- Modify: `CMakeLists.txt` (option `EG13_BUILD_UI`, default ON on Apple and OFF elsewhere), `plugin/CMakeLists.txt`, `.github/workflows/*` (mac job builds the UI and runs `swift test`)
- Delete: `docs/superpowers/m6-spike/` once its contents have moved. Keep nothing there.

**Interfaces:**
- Package.swift:
  - tools 6.0, `.macOS(.v14)`, product `.library(name: "EG13UI", type: .static)`.
  - Dependencies: `symbiotic-design` `exact: "0.1.0"` only.
  - `moduleAliases: ["SymbioticIcons": "EG13SymbioticIcons", "SymbioticTokens": "EG13SymbioticTokens"]`.
  - Targets: `EG13CBridge` (module map over `core-api/include`), `EG13UI`, `EG13UITests`.
  - Remove the Keyboard and Tonic dependencies, and `Package.resolved` entries for them.
- `scripts/build-ui.sh <config> <archs> <out-dir>` runs `swift build -c <config> --arch …` for each arch in `CMAKE_OSX_ARCHITECTURES`, then lipo-merges into `<out-dir>/libEG13UI.a`.
- `cmake/eg13_ui.cmake`:
  - adds a custom target `eg13_ui` that runs the script whenever a file under `ui/EG13UI/Sources` changes;
  - links `libEG13UI.a` into `eg13-impl`, with `-L$(xcrun --show-sdk-path)/usr/lib/swift`, the toolchain's `usr/lib/swift/macosx`, and `-Wl,-rpath,/usr/lib/swift`;
  - defines `EG13_HAS_UI=1`.
- `check-objc-prefix.sh <bundle>` lists ObjC classes with `nm -m` / `otool -ov` and fails on any class EG13 defines that doesn't start with `EG13` or `_TtC4EG13`, or with an aliased module prefix.
- A C++ test calls `eg13_ui_link_check()`. It is added only when `EG13_HAS_UI`, and returns 1.

- [ ] **Step 1: Failing test.** "The Swift UI links into the plugin" fails until the build links.
- [ ] **Step 2: Implement.** Port the spike's Swift sources and leave them building. Views that no longer compile against the new API are stubbed, and Tasks 7–9 rewrite them.
- [ ] **Step 3: Verify:**
  - `cmake --build --preset mac --target EG13_all eg13_tests && ctest --preset mac`;
  - `swift test --package-path ui/EG13UI`;
  - `scripts/check-objc-prefix.sh build/mac/.../EG13.clap`;
  - the Docker Linux check (UI off).
  All must pass.
- [ ] **Step 4: Commit** `M6a: build the SwiftUI package into the plugin`.

### Task 6: CLAP gui extension

**Files:**
- Create: `plugin/src/gui.cpp|.h`, `tests/test_gui.cpp` (only when `EG13_HAS_UI`)
- Modify: `plugin/src/eg13_plugin.cpp` (`pluginGetExtension` returns `CLAP_EXT_GUI`; `Plugin` owns `std::unique_ptr<UiBridge>` and the UI handle)

**Interfaces:**
- Produces a `clap_plugin_gui`:
  - `is_api_supported`: `CLAP_WINDOW_API_COCOA` and not floating.
  - `create` builds the `UiBridge` and calls `eg13_ui_create(&api, kind, 1.0)`. `destroy` calls `closeAllGestures()`, then `eg13_ui_destroy`, then resets the bridge.
  - `set_scale` returns false; Cocoa works in points.
  - `get_size` returns the current size, 1200 × 760 at 100 %. `can_resize` is true.
  - `get_resize_hints`: `can_resize_horizontally/vertically = true`, `preserve_aspect_ratio = true`, `aspect_ratio_width = 1200`, `aspect_ratio_height = 760`.
  - `adjust_size` keeps 1200:760 and clamps to 900 × 570 … 2400 × 1520.
  - `set_size` calls `eg13_ui_set_size`.
  - `set_parent` calls `eg13_ui_attach(handle, window->cocoa)`.
  - `show` / `hide` call `eg13_ui_set_visible`.
  - The UI's `request_resize` calls `host_gui->request_resize`.
  - `pluginOnMainThread` calls `bridge->pump()`.

- [ ] **Step 1: Failing tests:**
  - "adjust_size keeps the aspect and clamps": 100×100 → 900×570; 5000×1000 → 2400×1520; 1300×900 → aspect kept.
  - "Editor create/destroy 50 times leaves no live UI objects". The Swift side exposes a debug counter, `eg13_ui_live_count()`, under `#if DEBUG`.
- [ ] **Steps 2–4.** Also run `scripts/validate.sh`: clap-validator, auval (`aumu` and `aumf`) and pluginval, all of which open the editor. Expected: pass.
- [ ] **Step 5:** Manual: open the editor in Reaper or Bitwig (CLAP) and in Logic (AU), resize it, and close and reopen it. Record what you saw in the report. Write the `interface.md` scaling section.
- [ ] **Step 6: Commit** `M6a: CLAP gui extension hosting the SwiftUI editor`.

### Task 7: Palette, theme and controls

**Files:**
- Modify or create, under `ui/EG13UI/Sources/EG13UI/`:
  - `Style/EG13Palette.swift`, `Style/EG13Theme.swift`, `Style/EG13KnobStyle.swift`;
  - `Controls/EG13Knob.swift`, `Controls/EG13EnginePicker.swift`, `Controls/EG13Stepped.swift`, `Controls/EG13ControlInteraction.swift`;
  - `Tests/EG13UITests/EG13ControlInteractionTests.swift`.

**Interfaces:**
- Produces:
  - `enum EG13Palette` with exact hex values:
    - teal `#00A1A7`, ink `#007F8A`;
    - pink `#DA4061`, ink `#C42C4E`;
    - panel `#E6E6E6`, header strip `#D5D5D4`;
    - rack `#333333`;
    - LCD `#383838` on `#AFD22C`;
    - window `#8C8C89`.
  - `EG13Theme.make() -> Theme` is a symbiotic-design `Theme` built with its public initialisers. It takes spacing, radius and type from `Theme.studioDark`, and colours from `EG13Palette`.
  - `func partAccent(_ part: Int) -> Color`: 0 is teal, 1 is pink.
  - `struct EG13KnobStyle { var arc: Bool; var arcPlacement: ArcPlacement; var innerRing: Bool; var ticks: Bool; var cap: Cap /* white, accent, black */; var label: LabelStyle /* plain, chip */; var accent: Color }`, default arc inside the skirt band over a faint track, no ticks, white cap.
  - Drawing (`EG13KnobShape`) is kept separate from behaviour (`EG13ControlInteraction`).
  - `EG13ControlInteraction`, a pure value type:
    - vertical drag of 200 pt for the full range; ⇧ is 10× finer;
    - scroll at 1/100 of the range per line;
    - double-click resets to the default;
    - stepped parameters snap to integer steps;
    - every change goes through `begin` / `edit(value)` / `end` callbacks.
  - Accessibility: each knob has `accessibilityLabel` (the param name), `accessibilityValue` (the param text) and `accessibilityAdjustableAction` (± one step, or 1/100).
  - `EG13EnginePicker` is a menu of engine names that edits the slot selector parameter.

- [ ] **Step 1: Failing tests:** "drag 200 pt spans the range"; "shift drag is 10× finer"; "double-click resets to default"; "stepped values snap"; "every gesture is begin…end balanced, including a cancelled drag".
- [ ] **Steps 2–4:** `swift test --package-path ui/EG13UI`. **Step 5: Commit** `M6a: EG13 palette, theme and controls`.

### Task 7a: Owner design gate — controls

**Files:**
- Create: `ui/EG13UI/Sources/EG13UIPreview/main.swift`, an executable target in the package.
  - It opens a window with the editor over `EG13FakeCore`, which needs no DAW.
  - It has a "Controls" page: every knob style option, the engine picker and the stepped controls, in both part accents, at 75, 100 and 200 %.
- Modify: `ui/EG13UI/Package.swift` (the executable product `EG13UIPreview`), and `docs/superpowers/m6-resume.md` (the design log).

- [ ] **Step 1:** Build and run the preview with `swift run --package-path ui/EG13UI EG13UIPreview`. Capture screenshots to `docs/superpowers/m6-design/controls-r1/`.
- [ ] **Step 2: STOP for the owner.** Show the screenshots and the run command. Record the feedback in the design log as round N.
- [ ] **Step 3:** Apply the feedback in the style and control files only, re-run the Task 7 tests, take new screenshots, and repeat Step 2 until the owner approves the controls.
- [ ] **Step 4: Commit each round** as `M6a design: controls round N`. Record the approval, with its date, in the design log and in spec §8.

### Task 8: UI model

**Files:**
- Create or modify: `ui/EG13UI/Sources/EG13UI/Core/EG13Core.swift` (protocol), `EG13CoreAPIClient.swift`, `Model/EG13Synth.swift`, `Model/EG13Slot.swift`, `Model/EG13MeterBallistics.swift`
- Tests: `Tests/EG13UITests/EG13SynthModelTests.swift`, `EG13FakeCore.swift`

**Interfaces:**
- Produces:
  - `@MainActor protocol EG13Core`, mirroring `EG13CoreAPI`.
  - `@MainActor @Observable final class EG13Synth` with:
    - `selectedPart: Int`;
    - `func slot(_ kind: EG13SlotKind, part: Int) -> EG13Slot`, which carries the engine index, the engine names and the visible P descriptors. The visible P count is `engine_param_count`.
    - `func value(_ id: UInt32) -> Double`, `func text(_ id: UInt32) -> String`;
    - `func begin(_ id:)`, `func edit(_ id:, _ v:)`, `func end(_ id:)`;
    - `meters: [[Float]]`, after ballistics: 300 ms decay, 1.5 s peak hold.
  - `func tick()` is driven by a 30 Hz `Timer` the view owns and invalidates on destroy. It reads the snapshot and rebuilds descriptors only when `engine_generation` changes.
  - While the user drags a knob, the knob shows the user's value rather than the snapshot's.

- [ ] **Step 1: Failing tests with the fake core:** "Engine change refreshes descriptors and hides unused P's"; "Snapshot updates do not emit edits"; "a dragged knob ignores the snapshot until end"; "meters decay and hold".
- [ ] **Steps 2–4.** **Step 5: Commit** `M6a: UI model over the core API`.

### Task 9: Part editor layout

**Files:**
- Create or modify under `ui/EG13UI/Sources/EG13UI/Views/`: `EG13RootView.swift`, `HeaderView.swift`, `SlotCard.swift`, `ModsColumn.swift`, `MatrixAmounts.swift`, `BottomBar.swift`
- Modify: `Entry/EG13UIController.swift`, `Entry/EG13PluginView.swift`
- Tests: `Tests/EG13UITests/EG13LayoutTests.swift`

**Layout, per the approved mock** (https://claude.ai/artifact/Dao1y9KMKtEV41Pvd5fwL3, which the owner can open):
- **Header** (rack grey): the EG13 wordmark, which uses `SymbioticBrand` when it can be imported and otherwise text; A/B part tabs in the part accents; the Mode segmented control (master Mode).
- **Columns, left to right:** Source, Filter, Shaper and Part FX cards, then a MODS column.
  - Each slot card has a lowercase engine title on the header strip, an `EG13EnginePicker`, and a knob grid of the visible P's. That is up to 16, 4, 12 and 16 knobs, in 4 columns.
  - The MODS column holds four compact mod cards. Each has an engine picker, a Scope stepped control, and a knob grid.
- **Below the columns:** the matrix as 12 route-amount knobs labelled "Route 1…12". Choosing sources and destinations is M6b.
- **Bottom bar:**
  - part Level, Pan, Glide, Voice Mode and Voices;
  - a "Part settings" popover with the remaining part parameters, Part FX Input included;
  - master Dry and Out;
  - a stereo meter for each part, in its accent colour.
- **Scaling:** the root draws at 1200 × 760, scaled with `scaleEffect` to the host size.

- [ ] **Step 1: Failing tests, at the model level, with the fake core:**
  - "Every parameter of a part and the master is reachable from exactly one control". Walk the view model's control list and compare it with the 158 per-part plus 7 master IDs, minus unused P's.
  - "Part tabs switch every card to that part".
- [ ] **Steps 2–4.** Build the plugin. Manual check in a host: every card shows the right names for each engine.
- [ ] **Step 5: Manual** (`interface.md`: the header, columns, cards, hidden unused controls, bottom bar, part settings popover and meters) and **commit** `M6a: one-part editor with every control`.

### Task 9a: Owner design gate — layout

**Files:**
- Modify: the files under `Views/`, `Style/EG13Palette.swift`, the preview app's "Editor" page, the design log, and spec §8.

- [ ] **Step 1:** Show the full editor to the owner in the preview app and in a host (Reaper or Bitwig, and Logic). Show both parts, every engine type in each slot, and the part settings popover. Screenshots go to `docs/superpowers/m6-design/layout-r1/`.
- [ ] **Step 2: STOP for the owner.** Record the feedback as round N.
- [ ] **Step 3:** Apply the feedback. Re-run the Task 9 reachability test after each round, so every control stays reachable. Repeat until the owner approves.
- [ ] **Step 4: Commit each round** as `M6a design: layout round N`. Record the approval in the design log and spec §8, and update `interface.md` to match.

### Task 10: README, validators and host check

**Files:**
- Modify: `README.md` (building with the UI, Swift and Xcode requirements), `docs/manual/interface.md`, `docs/manual/limitations.md` (what M6b adds)

- [ ] **Step 1:** Run `scripts/validate.sh`, `scripts/check-objc-prefix.sh` on each built bundle (CLAP, VST3, AU), the full mac suite, `swift test`, and the Docker Linux check. All must pass.
- [ ] **Step 2: Measure** the UI's main-thread cost while idle and while dragging a knob, with Instruments or `ps` sampling; budget under 5 % of one core idle. Audio-thread cost of `drain` is measured with the existing benchmark harness, and must be under 0.1 %. Document both.
- [ ] **Step 3: Commit** `M6a: README, manual and validators for the UI`.

## Self-review notes

- **Spec §8 coverage:**
  - Covered in M6a: SwiftUI in the gui extension (Tasks 5–6); symbiotic-design (Task 7); window and scaling (Tasks 6 and 9); the header, slot cards and mod cards (Task 9); plumbing with gestures and the snapshot (Tasks 2–4 and 8); VoiceOver (Task 7).
  - Deferred to M6b: modulation rings and chips, live shape displays, "Modulate from…", the keyboard and focus navigation.
- **Names used across tasks:** `UiSnapshot`, `uiSnapshot`, `engineGeneration` / `engine_generation`, `UiBridge`, `UiEvent`, `EG13CoreAPI`, `engine_param_count`, `EG13Synth`, `EG13Slot`, `EG13ControlInteraction`, `EG13KnobStyle`, `EG13Palette`, `partAccent`.
