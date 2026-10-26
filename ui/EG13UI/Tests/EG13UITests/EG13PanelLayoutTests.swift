import AppKit
import EG13CBridge
import EG13UIFixtures
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

    /// Every id of one part and the master whose P the selected engine uses (not named "(unused)").
    func expectedIDs(part: Int) -> Set<UInt32> {
        var ids = Set<UInt32>()
        for index in 0..<core.paramCount() {
            let id = core.descriptors[index].id
            guard id >> 16 == UInt32(part) || id >> 16 == 0xFF else { continue }
            let slot = (id >> 8) & 0xFF
            let isP = (1...8).contains(slot) && (id & 0xFF) >= 1
            if !(isP && core.paramName(at: index) == EG13Slot.unusedName) { ids.insert(id) }
        }
        return ids
    }

    @Test("With the real engine names, every used parameter is reachable exactly once, on both parts")
    func reachableOnceWithRealEngines() {
        for part in 0..<EG13ParamID.parts {
            for kind in Self.slotKinds {
                let count = core.slotEngines[Int(kind.rawValue)]!.count
                for engine in 0..<count {
                    core.select(kind, engine: engine, part: part)
                    synth.tick()
                    let ids = synth.editorControls(part: part).all.map(\.id)
                    let note = "slot \(kind.rawValue) engine \(engine) part \(part)"
                    #expect(ids.count == Set(ids).count, "duplicate: \(note)")
                    let expected = expectedIDs(part: part)
                    #expect(Set(ids) == expected,
                            "missing \(expected.subtracting(ids).sorted()) extra \(Set(ids).subtracting(expected).sorted()) \(note)")
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

    func caps(_ kind: EG13SlotKind, _ engine: Int, part: Int = 0) -> [String: EG13ParamEmphasis.Cap] {
        core.select(kind, engine: engine, part: part)
        synth.tick()
        var result: [String: EG13ParamEmphasis.Cap] = [:]
        for spec in synth.slotControls(kind, part: part).params { result[synth.name(spec.id)] = spec.emphasis.cap }
        return result
    }

    @Test("Elements' caps match the panel: pink Blow, Flow, Blow Timbre; teal Strike, Mallet, Strike Timbre")
    func elementsColours() {
        let c = caps(EG13_SLOT_KIND_SOURCE, 2)
        #expect(c["Blow"] == .pink && c["Flow"] == .pink && c["Blow Timbre"] == .pink)
        #expect(c["Strike"] == .teal && c["Mallet"] == .teal && c["Strike Timbre"] == .teal)
        for white in ["Contour", "Bow", "Bow Timbre", "Geometry", "Brightness", "Damping", "Position", "Space"] {
            #expect(c[white] == .plain, "\(white)")
        }
    }

    @Test("Other panels: Clouds, Liquid Filter, Shelves, Rings; the fallbacks are white")
    func panelColours() {
        let clouds = caps(EG13_SLOT_KIND_PART_FX, 1)
        #expect(clouds["Position"] == .pink && clouds["Size"] == .teal && clouds["Pitch"] == .plain)
        #expect(clouds["Density"] == .pink && clouds["Texture"] == .teal && clouds["Blend"] == .plain)
        #expect(caps(EG13_SLOT_KIND_FILTER, 1)["Resonance"] == .pink && caps(EG13_SLOT_KIND_FILTER, 1)["Frequency"] == .plain)
        let eq = caps(EG13_SLOT_KIND_SHAPER, 2)
        #expect(eq["Mid 1 Freq"] == .pink && eq["Mid 2 Gain"] == .teal && eq["High Freq"] == .plain)
        #expect(caps(EG13_SLOT_KIND_SOURCE, 3).values.allSatisfy { $0 == .plain })      // Rings
        #expect(caps(EG13_SLOT_KIND_SHAPER, 1).values.allSatisfy { $0 == .plain })      // Warps
        #expect(caps(EG13_SLOT_KIND_SOURCE, 4).values.allSatisfy { $0 == .plain })      // Audio Input
        for kind in [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4] {
            for engine in 0..<core.slotEngines[Int(kind.rawValue)]!.count {
                #expect(caps(kind, engine).values.allSatisfy { $0 == .plain })          // no panel: white
            }
        }
    }

    @Test("Knob colour and size never depend on the part")
    func colourIsPartIndependent() {
        for kind in Self.slotKinds {
            for engine in 0..<core.slotEngines[Int(kind.rawValue)]!.count {
                for part in 0..<EG13ParamID.parts { core.select(kind, engine: engine, part: part) }
                synth.tick()
                let a = synth.slotControls(kind, part: 0).params.map { ($0.kind, $0.emphasis) }
                let b = synth.slotControls(kind, part: 1).params.map { ($0.kind, $0.emphasis) }
                #expect(a.count == b.count)
                #expect(zip(a, b).allSatisfy { $0 == $1 }, "slot \(kind.rawValue) engine \(engine)")
            }
        }
        // The knob style takes no part: one style per cap colour.
        #expect(EG13Palette.knobStyle(for: .pink).accent == EG13Palette.pink.bright.color)
        #expect(EG13Palette.knobStyle(for: .teal).accent == EG13Palette.teal.bright.color)
        #expect(EG13Palette.knobStyle(for: .plain).accent == EG13Palette.whiteKnobArc.color)
        #expect(EG13Palette.knobStyle(for: .black).accent == EG13Palette.blackKnobArc.color)
        #expect(EG13Palette.knobStyle(for: .pink).label == .chip && EG13Palette.knobStyle(for: .plain).label == .plain)
        let controls = synth.editorControls(part: 0)
        #expect(controls.partBar.allSatisfy { $0.emphasis == .plain })
        #expect(controls.masterBar.allSatisfy { $0.emphasis == .black })
        #expect(controls.matrix.allSatisfy { $0.emphasis == .black })
        // Part identity stays in the part colours.
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
                    // Each line is no wider than the cell's text: nothing is cut or overflows.
                    for line in fit.lines { #expect(EG13LabelFit.width(line) <= width, "\(label): \(line)") }
                }
            }
        }
        // The bar and matrix labels sit in cells of 64 pt or more (Level, Pan, Glide, Dry, Out and
        // the matrix's Route 1...12); measure them at 64.
        let width = EG13Layout.labelWidth(cell: 64)
        let labels = ["Level", "Pan", "Glide", "Dry", "Out"] .map { EG13ParamLabels.short($0) }
            + (1...EG13ParamID.routeCount).map { "Route \($0)" }
        for label in labels {
            let fit = EG13LabelFit.lines(label, width: width)
            #expect(fit.fits, "\(label) in \(width) pt")
            for line in fit.lines { #expect(EG13LabelFit.width(line) <= width, "\(label): \(line)") }
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
