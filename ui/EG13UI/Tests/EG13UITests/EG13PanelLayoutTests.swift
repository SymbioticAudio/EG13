import AppKit
import EG13CBridge
import Testing
@testable import EG13UI

/// Layout round 2: the knob sizes and emphasis come from the real module panels, the part's accent
/// is the only emphasis colour, every label fits its knob, and every engine of the plugin has data.
@MainActor
struct EG13PanelLayoutTests {
    let core = EG13FakeCore.fullCatalog()
    let synth: EG13Synth

    init() { synth = EG13Synth(core: core, kind: .instrument) }

    static let slotKinds: [EG13SlotKind] = [
        EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER, EG13_SLOT_KIND_MOD1,
        EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4, EG13_SLOT_KIND_PART_FX,
    ]

    func spec(_ kind: EG13SlotKind, engine: Int, name: String, part: Int = 0) -> EG13ControlSpec? {
        core.select(kind, engine: engine, part: part)
        synth.tick()
        let slot = synth.slot(kind, part: part)
        let controls = synth.slotControls(kind, part: part)
        guard let index = slot.descriptors.firstIndex(where: { synth.name($0.id) == name }) else { return nil }
        return controls.params.first { $0.id == slot.descriptors[index].id }
    }

    @Test("Every engine of every slot has panel data, one entry per P")
    func everyEngineHasEntry() {
        for kind in Self.slotKinds {
            for engine in EG13EngineCatalog.engines(kind: Int(kind.rawValue)) where !engine.params.isEmpty {
                #expect(EG13PanelLayout.hasEntry(kind: kind, engine: engine.name), "\(kind.rawValue) \(engine.name)")
                #expect(EG13PanelLayout.entryCount(kind: kind, engine: engine.name) == engine.params.count,
                        "\(kind.rawValue) \(engine.name)")
            }
        }
    }

    @Test("Every parameter is reachable once, for every real engine of every slot, on both parts")
    func reachableWithRealEngines() {
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                let count = core.slotEngines[Int(kind.rawValue)]!.count
                for engine in 0..<count {
                    core.select(kind, engine: engine, part: part)
                    synth.tick()
                    let ids = synth.editorControls(part: part).all.map(\.id)
                    #expect(ids.count == Set(ids).count, "duplicate: slot \(kind.rawValue) engine \(engine)")
                    for id in ids { #expect(synth.name(id) != EG13Slot.unusedName) }
                }
                core.select(kind, engine: 0, part: part)
            }
        }
    }

    @Test("Sizes follow the panels: Clouds' Position, Size and Pitch large; its Density medium; Freeze small")
    func cloudsSizes() {
        func size(_ name: String) -> EG13KnobSize? {
            guard case .knob(let size)? = spec(EG13_SLOT_KIND_PART_FX, engine: 1, name: name)?.kind else { return nil }
            return size
        }
        #expect(size("Position") == .large && size("Size") == .large && size("Pitch") == .large)
        #expect(size("Density") == .medium && size("Texture") == .medium && size("In Gain") == .medium)
        #expect(size("Freeze") == .small)
    }

    @Test("Emphasis follows the panels' large knobs, always in the part's accent")
    func emphasisFromPanels() {
        func emphasised(_ kind: EG13SlotKind, _ engine: Int) -> [String] {
            core.select(kind, engine: engine, part: 0)
            synth.tick()
            return synth.slotControls(kind, part: 0).params.filter { $0.emphasis != .plain }.map { synth.name($0.id) }
        }
        #expect(emphasised(EG13_SLOT_KIND_SOURCE, 0) == ["Harmonics", "Frequency"])      // Plaits
        #expect(emphasised(EG13_SLOT_KIND_SOURCE, 2) == ["Flow", "Mallet", "Geometry", "Brightness"])  // Elements
        #expect(emphasised(EG13_SLOT_KIND_SOURCE, 3) == ["Structure", "Frequency"])      // Rings
        #expect(emphasised(EG13_SLOT_KIND_FILTER, 1) == ["Frequency", "Resonance"])      // Ripples
        #expect(emphasised(EG13_SLOT_KIND_SHAPER, 1) == ["Algorithm"])                   // Warps
        #expect(emphasised(EG13_SLOT_KIND_MOD1, 2) == ["Frequency", "Shape"])             // Tides 2
        #expect(emphasised(EG13_SLOT_KIND_MOD1, 3) == ["T Rate", "X Spread"])             // Marbles
        #expect(emphasised(EG13_SLOT_KIND_PART_FX, 1) == ["Position", "Size", "Pitch"])  // Clouds
        // Every emphasised knob is plain `.accent`: no colour is stored with it.
        for kind in Self.slotKinds {
            for engine in 0..<core.slotEngines[Int(kind.rawValue)]!.count {
                core.select(kind, engine: engine, part: 0)
                synth.tick()
                #expect(synth.slotControls(kind, part: 0).params.allSatisfy { $0.emphasis == .accent || $0.emphasis == .plain })
            }
        }
        let controls = synth.editorControls(part: 0)
        #expect(controls.partBar[0].emphasis == .accent)
        #expect(controls.masterBar.allSatisfy { $0.emphasis == .black })
        #expect(controls.matrix.allSatisfy { $0.emphasis == .black })
    }

    @Test("Review I1: the emphasis colour is the shown part's accent, teal for A and pink for B")
    func emphasisColourIsPartAccent() {
        for part in 0..<EG13ParamID.parts {
            let style = EG13Palette.knobStyle(for: .accent, part: part)
            #expect(style.accent == partAccent(part))
            #expect(style.accentInk == EG13Palette.part(part).ink.color)
            #expect(style.cap == .accent && style.label == .chip)
        }
        #expect(partAccent(0) != partAccent(1))
    }

    @Test("A mod knob is never large")
    func modsNeverLarge() {
        for kind in [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD4] {
            for engine in 0..<core.slotEngines[Int(kind.rawValue)]!.count {
                core.select(kind, engine: engine, part: 0)
                synth.tick()
                for spec in synth.slotControls(kind, part: 0).params {
                    #expect(spec.kind != .knob(.large))
                }
            }
        }
    }

    /// Whether every label of an engine's visible knobs fits in a cell at the label font, on one
    /// line or two. The cell is the narrowest one the card gives (`slotMinCell`, `modMinCell`).
    @Test("Every label of every engine fits its knob without being cut or shrunk")
    func labelsFit() {
        for kind in Self.slotKinds {
            let minCell = EG13ParamID.isModSlot(kind) ? EG13Layout.modMinCell : EG13Layout.slotMinCell
            let width = EG13Layout.labelWidth(cell: minCell)
            for engine in EG13EngineCatalog.engines(kind: Int(kind.rawValue)) {
                for name in engine.params where name != EG13Slot.unusedName {
                    let label = EG13ParamLabels.short(name)
                    let fit = EG13LabelFit.lines(label, width: width)
                    #expect(fit.fits, "\(engine.name) \(name) -> \(label) in \(width) pt")
                    #expect(fit.lines.count <= 2)
                }
            }
        }
        // Part Level, Pan, Glide, Dry, Out sit in cells of 70 pt or more.
        for name in ["Level", "Pan", "Glide", "Dry"] {
            #expect(EG13LabelFit.lines(EG13ParamLabels.short(name), width: EG13Layout.labelWidth(cell: 64)).fits)
        }
    }

    @Test("Abbreviations are one table: no engine label is the full name unless it is short")
    func abbreviationsAreTabled() {
        for kind in Self.slotKinds {
            for engine in EG13EngineCatalog.engines(kind: Int(kind.rawValue)) {
                for name in engine.params where name != EG13Slot.unusedName {
                    #expect(EG13ParamLabels.table[name] != nil, "no short label for \(name) (\(engine.name))")
                }
            }
        }
    }

    @Test("Every engine name fits the engine menu's LCD")
    func engineNamesFit() {
        let theme = EG13Theme.theme
        let font = NSFont.monospacedSystemFont(ofSize: max(theme.type.label.size, 12), weight: .bold)
        let room = EG13Layout.enginePickerWidth - 2 * theme.spacing.xs
        for kind in 0..<8 {
            for engine in EG13EngineCatalog.engines(kind: kind) {
                let width = (engine.name.uppercased() as NSString).size(withAttributes: [.font: font]).width
                #expect(width <= room, "\(engine.name): \(width) > \(room)")
            }
        }
    }

    @Test("Knob rows are balanced and as wide as the card allows, up to the cell maximum")
    func gridShape() {
        let wide = EG13Layout.gridShape(count: 16, width: 686, minCell: 64)
        #expect(wide.columns == 8 && wide.cell == 85)      // two rows of 8, not 10 and 6
        let few = EG13Layout.gridShape(count: 3, width: 686, minCell: 64)
        #expect(few.columns == 3 && few.cell == EG13Layout.maxCell)
        let one = EG13Layout.gridShape(count: 8, width: 1164, minCell: 50)
        #expect(one.columns == 8)
        #expect(EG13Layout.gridShape(count: 13, width: 458, minCell: 50).columns == 7)
    }

    @Test("Every slot with a choice of engines has an engine-menu control")
    func enginePickerIsASpec() {
        for part in 0..<EG13ParamID.parts {
            let controls = synth.editorControls(part: part)
            for slot in controls.cards + controls.mods {
                #expect(slot.engine?.kind == .enginePicker(slot.kind))
                #expect(controls.all.contains { $0.id == EG13ParamID.slot(part, slot.kind, 0) })
            }
        }
    }
}
