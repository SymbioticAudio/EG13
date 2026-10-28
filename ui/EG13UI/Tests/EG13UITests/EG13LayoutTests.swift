import EG13CBridge
import Testing
@testable import EG13UI

/// Task 9: the part editor shows every parameter, once. The views render from
/// `EG13Synth.editorControls`, so these tests need no view inspection.
@MainActor
struct EG13LayoutTests {
    let core = EG13FakeCore.full()
    let synth: EG13Synth

    init() { synth = EG13Synth(core: core, kind: .instrument) }
    init(kind: EG13Synth.Kind) { synth = EG13Synth(core: core, kind: kind) }
    init(settings: EG13SettingsStore) { synth = EG13Synth(core: core, kind: .instrument, settings: settings) }

    static let slotKinds: [EG13SlotKind] = [
        EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER, EG13_SLOT_KIND_MOD1,
        EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4, EG13_SLOT_KIND_PART_FX,
    ]

    /// Every id the plugin has for one part and the master, minus the P's the engines do not use
    /// and, in the instrument, master Dry (it has no audio input, so Dry does nothing there).
    func expectedIDs(part: Int) -> Set<UInt32> {
        var ids = Set<UInt32>()
        for index in 0..<core.paramCount() {
            let id = core.descriptors[index].id
            let owner = id >> 16
            guard owner == UInt32(part) || owner == 0xFF else { continue }
            let name = core.paramName(at: index)
            let slot = (id >> 8) & 0xFF
            let isP = (1...8).contains(slot) && (id & 0xFF) >= 1 && name == EG13Slot.unusedName
            let isHiddenDry = synth.kind == .instrument && id == EG13ParamID.master(.dry)
            if !isP && !isHiddenDry { ids.insert(id) }
        }
        return ids
    }

    func assertReachable(part: Int, _ note: String = "") {
        let controls = synth.editorControls(part: part).all.map(\.id)
        let expected = expectedIDs(part: part)
        #expect(controls.count == Set(controls).count, "a control appears twice \(note)")
        #expect(Set(controls) == expected,
                "missing \(expected.subtracting(controls).sorted()) extra \(Set(controls).subtracting(expected).sorted()) \(note)")
        // The matrix: each route's amount is a control and its source, via and destination are menus.
        let menus = synth.routeMenus(part: part)
        #expect(menus.count == 3 * EG13ParamID.routeCount && Set(menus.map(\.id)).count == menus.count, "route menus \(note)")
        let amounts = (0..<EG13ParamID.routeCount).map { EG13ParamID.routeAmount(part, $0) }
        #expect(amounts.allSatisfy { id in controls.filter { $0 == id }.count == 1 }, "route amounts once \(note)")
    }

    @Test("Every parameter is reachable exactly once with the modulation display off, too")
    func reachableWithModulationOff() {
        let store = EG13SettingsStore(url: nil)
        store.update { $0.modulationDisplay = false }
        let off = EG13LayoutTests(settings: store)
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                let count = off.core.slotEngines[Int(kind.rawValue)]!.count
                for engine in 0..<count {
                    off.core.select(kind, engine: engine, part: part)
                    off.synth.tick()
                    off.assertReachable(part: part, "display off, slot \(kind.rawValue) engine \(engine)")
                }
                off.core.select(kind, engine: 0, part: part)
            }
        }
    }

    @Test("The fake exposes all 158 per-part and 7 master parameters")
    func fakeTableIsComplete() {
        #expect(core.paramCount() == 2 * 158 + 7)
    }

    @Test("Every parameter of a part and the master is reachable from exactly one control")
    func everyParameterReachableOnce() {
        // Every engine of every slot, one slot at a time, on both parts.
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                let count = core.slotEngines[Int(kind.rawValue)]!.count
                for engine in 0..<count {
                    core.select(kind, engine: engine, part: part)
                    synth.tick()
                    assertReachable(part: part, "slot \(kind.rawValue) engine \(engine) part \(part)")
                    assertReachable(part: 1 - part, "slot \(kind.rawValue) engine \(engine) other part")
                }
                core.select(kind, engine: 0, part: part)
            }
        }
    }

    @Test("EG13 FX: every parameter, master Dry included, is reachable exactly once")
    func effectReachable() {
        let fx = EG13LayoutTests(kind: .effect)
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                fx.core.select(kind, engine: fx.core.slotEngines[Int(kind.rawValue)]!.count - 1, part: part)
            }
        }
        fx.synth.tick()
        for part in 0..<EG13ParamID.parts {
            fx.assertReachable(part: part, "EG13 FX")
            #expect(fx.synth.editorControls(part: part).all.filter { $0.id >> 16 == 0xFF }.count == 7)
        }
    }

    @Test("With every P used, a part shows 158 controls and the instrument's master 6 (no Dry)")
    func fullPartCount() {
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                let count = core.slotEngines[Int(kind.rawValue)]!.count
                // The last engine of each slot uses all of the slot's P's.
                core.select(kind, engine: count - 1, part: part)
            }
        }
        synth.tick()
        for part in 0..<EG13ParamID.parts {
            let controls = synth.editorControls(part: part).all.map(\.id)
            #expect(controls.filter { $0 >> 16 == UInt32(part) }.count == 158)
            #expect(controls.filter { $0 >> 16 == 0xFF }.count == 6)
            #expect(Set(controls) == expectedIDs(part: part))
        }
    }

    @Test("Part FX Input and the other part settings are in the Part settings popover")
    func partSettingsPopover() {
        let controls = synth.editorControls(part: 1)
        let settings = Set(controls.partSettings.map(\.id))
        #expect(settings.contains(EG13ParamID.part(1, .partFxInput)))
        #expect(controls.partSettings.count == 17)
        #expect(controls.partBar.map(\.id) == [.level, .pan, .glide, .voiceMode, .voiceCount].map { EG13ParamID.part(1, $0) })
        // The instrument has no audio input, so no Dry; EG13 FX has both.
        #expect(controls.masterBar.map(\.id) == [EG13ParamID.master(.level)])
        #expect(EG13LayoutTests(kind: .effect).synth.editorControls(part: 1).masterBar.map(\.id)
                    == [EG13ParamID.master(.dry), EG13ParamID.master(.level)])
        #expect(controls.header.map(\.id) == [.mode, .latencyMode, .splitNote, .voiceGain, .softClip].map { EG13ParamID.master($0) })
        #expect(controls.matrix.count == 12)
    }

    @Test("Unused P's are hidden, including a gap inside an engine's range")
    func unusedHidden() {
        core.select(EG13_SLOT_KIND_MOD1, engine: 2, part: 0)  // the gapped engine: 6 P's, 2 unused
        synth.tick()
        let mod = synth.editorControls(part: 0).mods[0]
        #expect(mod.params.count == 4)
        #expect(mod.scope != nil)
        core.select(EG13_SLOT_KIND_FILTER, engine: 0, part: 0)  // Off: no P's at all
        synth.tick()
        #expect(synth.editorControls(part: 0).cards[1].params.isEmpty)
    }

    @Test("Part tabs switch every card to that part")
    func partTabsSwitchCards() {
        core.select(EG13_SLOT_KIND_SOURCE, engine: 1, part: 1)  // Part B on another engine
        synth.tick()
        synth.selectedPart = 0
        let a = synth.editorControls(part: synth.selectedPart)
        synth.selectedPart = 1
        let b = synth.editorControls(part: synth.selectedPart)
        #expect(a.part == 0 && b.part == 1)
        for (cardA, cardB) in zip(a.cards + a.mods, b.cards + b.mods) {
            #expect(cardA.all.allSatisfy { $0.id >> 16 == 0 })
            #expect(cardB.all.allSatisfy { $0.id >> 16 == 1 })
        }
        #expect(a.cards[0].params.count == 8)   // Part A: Macro
        #expect(b.cards[0].params.count == 7)   // Part B: Digital
        #expect(a.partBar.allSatisfy { $0.id >> 16 == 0 } && b.partBar.allSatisfy { $0.id >> 16 == 1 })
        #expect(a.matrix.allSatisfy { $0.id >> 16 == 0 } && b.matrix.allSatisfy { $0.id >> 16 == 1 })
        // The master controls are the same on both tabs.
        #expect(a.masterBar == b.masterBar && a.header == b.header)
    }

    @Test("Input Source: the instrument offers Off and Other part; EG13 FX offers all three")
    func inputSourceChoices() {
        let id = EG13ParamID.part(0, .inputSource)
        let spec = synth.editorControls(part: 0).partSettings.first { $0.id == id }
        let choices = spec?.choices
        #expect(choices?.options.map(\.label) == ["Off", "Other part"])
        #expect(choices?.options.map(\.value) == [0, 1])
        // A stored Both behaves as Other part in the instrument, so it shows as Other part.
        #expect(choices?.index(of: 2) == 1 && choices?.label(for: 2) == "Other part")
        #expect(choices?.index(of: 0) == 0 && choices?.label(for: 0) == "Off")
        let fx = EG13LayoutTests(kind: .effect).synth.editorControls(part: 0).partSettings.first { $0.id == id }
        #expect(fx != nil && fx?.choices == nil)
    }

    @Test("Input Source in the instrument steps between Off and Other part only")
    func inputSourceStepping() {
        let id = EG13ParamID.part(0, .inputSource)
        let param = synth.param(id)!
        let choices = synth.editorControls(part: 0).partSettings.first { $0.id == id }!.choices!
        var stepper = param.makeInteraction(choices: choices)
        func step(_ steps: Int) {
            stepper.increment(by: steps)
            synth.tick()  // the snapshot catches up, as the editor's 30 Hz tick does
        }
        step(1)
        #expect(core.calls.suffix(3) == [.begin(id), .edit(id, 1), .end(id)])
        let before = core.calls.count
        step(1)  // Other part is the last option: no edit
        #expect(core.calls.count == before)
        step(-1)
        #expect(core.calls.suffix(3) == [.begin(id), .edit(id, 0), .end(id)])
        // From a stored Both (shown as Other part), up stays and down goes to Off.
        core.values[core.descriptors.firstIndex { $0.id == id }!] = 2
        synth.tick()
        #expect(param.value == 2 && param.text(choices: choices) == "Other part")
        let held = core.calls.count
        step(1)
        #expect(core.calls.count == held)
        step(-1)
        #expect(core.calls.suffix(3) == [.begin(id), .edit(id, 0), .end(id)])
        // Double-click resets to the parameter's default, Off.
        step(1)
        stepper.mouseDown(clickCount: 2)
        #expect(core.calls.suffix(3) == [.begin(id), .edit(id, 0), .end(id)])
    }
}
