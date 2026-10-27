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
    init(settings: EG13SettingsStore) { synth = EG13Synth(core: core, kind: .instrument, settings: settings) }

    static let slotKinds: [EG13SlotKind] = [
        EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER, EG13_SLOT_KIND_MOD1,
        EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4, EG13_SLOT_KIND_PART_FX,
    ]

    /// Every id the plugin has for one part and the master, minus the P's the engines do not use.
    func expectedIDs(part: Int) -> Set<UInt32> {
        var ids = Set<UInt32>()
        for index in 0..<core.paramCount() {
            let id = core.descriptors[index].id
            let owner = id >> 16
            guard owner == UInt32(part) || owner == 0xFF else { continue }
            let name = core.paramName(at: index)
            let slot = (id >> 8) & 0xFF
            let isP = (1...8).contains(slot) && (id & 0xFF) >= 1 && name == EG13Slot.unusedName
            if !isP { ids.insert(id) }
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

    @Test("With every P used, a part shows 158 controls and the master 7")
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
            #expect(controls.filter { $0 >> 16 == 0xFF }.count == 7)
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
        #expect(controls.masterBar.map(\.id) == [EG13ParamID.master(.dry), EG13ParamID.master(.level)])
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
}
