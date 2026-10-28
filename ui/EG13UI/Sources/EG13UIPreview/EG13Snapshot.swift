import AppKit
import EG13CBridge
import EG13UIFixtures
import SwiftUI
import EG13UI
import SymbioticTokens

/// Renders pages to PNGs with ImageRenderer: no window, no screen-recording permission.
@MainActor
enum EG13Snapshot {
    static let scales: [(name: String, zoom: CGFloat)] = [("75", 0.75), ("100", 1), ("200", 2)]

    /// Writes `content` (laid out at `size`) at `zoom`, scaled like the plugin's window, so a PNG is
    /// 75, 100 or 200% of the base size and vector-sharp at each.
    static func write<Content: View>(_ content: Content, size: CGSize, zoom: CGFloat, to path: String) -> Bool {
        let view = content
            .scaleEffect(zoom, anchor: .topLeading)
            .frame(width: size.width * zoom, height: size.height * zoom, alignment: .topLeading)
            .symbioticTheme(EG13Theme.theme)
            .environment(\.eg13InteractiveControls, false)
            .environment(\.colorScheme, .light)
        let renderer = ImageRenderer(content: view)
        renderer.scale = 1
        guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff), let png = rep.representation(using: .png, properties: [:])
        else {
            FileHandle.standardError.write(Data("render failed: \(path)\n".utf8))
            return false
        }
        do { try png.write(to: URL(fileURLWithPath: path)) } catch {
            FileHandle.standardError.write(Data("cannot write \(path): \(error)\n".utf8))
            return false
        }
        print("wrote \(path) (\(rep.pixelsWide)x\(rep.pixelsHigh))")
        return true
    }

    /// Writes `content` at its natural size on the window grey (contact sheets, whose height depends
    /// on how tall the cards are).
    static func writeFitted<Content: View>(_ content: Content, to path: String, scale: CGFloat = 1) -> Bool {
        let view = content
            .padding(16)
            .background(EG13Theme.theme.surface.window.color)
            .fixedSize()
            .symbioticTheme(EG13Theme.theme)
            .environment(\.eg13InteractiveControls, false)
            .environment(\.colorScheme, .light)
        let renderer = ImageRenderer(content: view)
        renderer.scale = scale
        guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
              let rep = NSBitmapImageRep(data: tiff), let png = rep.representation(using: .png, properties: [:])
        else {
            FileHandle.standardError.write(Data("render failed: \(path)\n".utf8))
            return false
        }
        do { try png.write(to: URL(fileURLWithPath: path)) } catch { return false }
        print("wrote \(path) (\(rep.pixelsWide)x\(rep.pixelsHigh))")
        return true
    }

    private static func makeDirectory(_ directory: String) -> Bool {
        do {
            try FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true)
            return true
        } catch {
            FileHandle.standardError.write(Data("cannot create \(directory): \(error)\n".utf8))
            return false
        }
    }

    /// The Controls page at 75, 100 and 200%.
    static func run(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let demo = EG13ControlsDemo(core: .standard())
        for (name, zoom) in scales {
            let page = EG13ControlsPage(demo: demo)
            guard write(page, size: EG13ControlsPage.size, zoom: zoom, to: "\(directory)/controls-\(name).png") else { return 1 }
        }
        return 0
    }

    /// The modulation design set (gate 6a): both parts with modulation shown, Part A at 75 %, a mark
    /// close-up at 200 %, card headers with the Motion button, and the Mods column with traces. The preview
    /// core's animation is a function of its tick count, so the same run draws the same PNGs.
    static func runModulation(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.modulationDemo()
        let synth = EG13Synth(core: core, kind: .instrument)
        // 70 ticks: past the 30 frames a trace holds, and 0.33 s into an envelope cycle.
        for _ in 0..<70 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        synth.selectedPart = 0
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-a.png") && ok
        ok = write(EG13RootView(synth: synth), size: base, zoom: 0.75, to: "\(directory)/editor-a-75.png") && ok
        synth.selectedPart = 1
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-b.png") && ok
        synth.selectedPart = 0
        ok = writeFitted(EG13MarkSheet(synth: synth), to: "\(directory)/mark.png", scale: 2) && ok
        ok = writeFitted(EG13MotionSheet(synth: synth), to: "\(directory)/motion-menu.png", scale: 2) && ok
        ok = writeFitted(EG13ModCards(synth: synth, part: 0, width: EG13Layout.modCardWidth, columnHeight: 0),
                         to: "\(directory)/traces.png", scale: 2) && ok
        return ok ? 0 : 1
    }

    /// The M7 design-library requests: each reusable component on its own at 100 % and 200 %, named
    /// `<component>-100.png` / `<component>-200.png`. The mod animation is a function of the tick count, so a
    /// rerun draws the same PNGs.
    static func runDesignRequests(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.modulationDemo()
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<70 { synth.tick() }
        synth.selectedPart = 0
        let voices = core.demoParam("Voices", value: 5, min: 1, max: 8, stepped: true)
        let engine = core.demoParam("Engine", value: 2, max: 7, stepped: true)
        var ok = true
        for (name, scale) in [("100", CGFloat(1)), ("200", CGFloat(2))] {
            ok = writeFitted(EG13KnobCapSheet(), to: "\(directory)/knob-cap-\(name).png", scale: scale) && ok
            ok = writeFitted(EG13MarkRequestSheet(), to: "\(directory)/mod-wave-mark-\(name).png", scale: scale) && ok
            ok = writeFitted(EG13LCDRequestSheet(synth: synth, voices: voices, engine: engine), to: "\(directory)/lcd-readout-\(name).png", scale: scale) && ok
            ok = writeFitted(EG13ModuleCard(synth: synth, kind: EG13_SLOT_KIND_MOD1, part: 0, number: 1,
                                            width: EG13Layout.modCardWidth),
                             to: "\(directory)/mini-trace-\(name).png", scale: scale) && ok
            ok = write(EG13RootView(synth: synth), size: EG13Layout.baseSize, zoom: scale,
                       to: "\(directory)/card-columns-\(name).png") && ok
        }
        return ok ? 0 : 1
    }

    /// The matrix editor: the right column on MATRIX at 100 % (Part A, route 3 highlighted as after a
    /// Motion row) and Part B, with the modulation demo's routes. For checking, not for the gate.
    static func runMatrix(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.modulationDemo()
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<70 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        for part in 0..<EG13ParamID.parts {
            synth.selectedPart = part
            let column = EG13RightColumnState()
            column.open(route: 2)
            ok = write(EG13RootView(synth: synth, column: column), size: base, zoom: 1,
                       to: "\(directory)/matrix-\(part == 0 ? "a" : "b").png") && ok
        }
        return ok ? 0 : 1
    }

    /// The Part settings popover for both parts in Split mode, with ranges set so both bands and the
    /// marker show. For checking, not for the gate.
    static func runKeyRange(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.standard()
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<4 { synth.tick() }
        let values: [(UInt32, Double)] = [
            (EG13ParamID.master(.mode), Double(EG13Mode.split.rawValue)),
            (EG13ParamID.master(.splitNote), 60),
            (EG13ParamID.part(0, .keyLow), 24), (EG13ParamID.part(0, .keyHigh), 71),
            (EG13ParamID.part(1, .keyLow), 60), (EG13ParamID.part(1, .keyHigh), 108),
        ]
        for (id, value) in values {
            synth.begin(id)
            synth.edit(id, value)
            synth.end(id)
        }
        return write(EG13PartSettingsSheet(synth: synth), size: EG13PartSettingsSheet.size, zoom: 2,
                     to: "\(directory)/key-range.png") ? 0 : 1
    }

    /// Gate 9a, round 1 (M6b layout): the editor with MATRIX and with MODS shown, Part A at 75 %, the Part
    /// settings popover in Split and in Layer, the Settings and About sheets, and the knob context menu.
    static func runLayoutRound1(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.modulationDemo()
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<70 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        func matrix() -> EG13RightColumnState { let c = EG13RightColumnState(); c.pane = .matrix; return c }
        ok = write(EG13RootView(synth: synth, column: matrix()), size: base, zoom: 1,
                   to: "\(directory)/editor-matrix.png") && ok
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-mods.png") && ok
        ok = write(EG13RootView(synth: synth, column: matrix()), size: base, zoom: 0.75,
                   to: "\(directory)/editor-matrix-75.png") && ok
        ok = write(EG13RootView(synth: synth, settingsTab: .settings), size: base, zoom: 1,
                   to: "\(directory)/settings.png") && ok
        ok = write(EG13RootView(synth: synth, settingsTab: .about), size: base, zoom: 1,
                   to: "\(directory)/about.png") && ok
        // The Part settings popover: Split (A low, B high, a marker) and Layer (both over most of the range).
        let split: [(UInt32, Double)] = [
            (EG13ParamID.master(.mode), Double(EG13Mode.split.rawValue)), (EG13ParamID.master(.splitNote), 60),
            (EG13ParamID.part(0, .keyLow), 24), (EG13ParamID.part(0, .keyHigh), 71),
            (EG13ParamID.part(1, .keyLow), 60), (EG13ParamID.part(1, .keyHigh), 108),
        ]
        let layer: [(UInt32, Double)] = [
            (EG13ParamID.master(.mode), Double(EG13Mode.layer.rawValue)),
            (EG13ParamID.part(0, .keyLow), 21), (EG13ParamID.part(0, .keyHigh), 108),
            (EG13ParamID.part(1, .keyLow), 48), (EG13ParamID.part(1, .keyHigh), 96),
        ]
        for (name, values) in [("split", split), ("layer", layer)] {
            let partCore = EG13PreviewCore.standard()
            let partSynth = EG13Synth(core: partCore, kind: .instrument)
            for _ in 0..<4 { partSynth.tick() }
            for (id, value) in values {
                partSynth.begin(id)
                partSynth.edit(id, value)
                partSynth.end(id)
            }
            ok = write(EG13PartSettingsSheet(synth: partSynth), size: EG13PartSettingsSheet.size, zoom: 1.5,
                       to: "\(directory)/part-settings-\(name).png") && ok
        }
        if let routed = synth.params.first(where: { $0.isRouted }), let menu = synth.contextMenu(for: routed.id) {
            ok = writeFitted(EG13ContextMenuSheet(model: menu, knob: routed.name), to: "\(directory)/context-menu.png",
                             scale: 2) && ok
        } else {
            FileHandle.standardError.write(Data("no routed knob for the context menu\n".utf8))
            ok = false
        }
        return ok ? 0 : 1
    }

    /// Layout round 2: the left column spread over its height, the grouped "Modulate from" menu, and a busy patch.
    static func runLayoutRound2(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        var ok = writeLayoutEditors(into: directory)
        let core = EG13PreviewCore.modulationDemo()
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<70 { synth.tick() }
        if let routed = synth.params.first(where: { $0.isRouted }), let menu = synth.contextMenu(for: routed.id) {
            ok = writeFitted(EG13ContextMenuSheet(model: menu, knob: routed.name), to: "\(directory)/context-menu.png",
                             scale: 2) && ok
        } else {
            ok = false
        }
        withExtendedLifetime(core) {}
        return ok ? 0 : 1
    }

    /// The layout round 3 set: the editor with the MATRIX view, the Mods view and a busy patch.
    static func runLayoutRound3(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        return writeLayoutEditors(into: directory) ? 0 : 1
    }

    /// The layout round 4 set: the same three editors, the bottom bar one gap under the taller column.
    static func runLayoutRound4(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        return writeLayoutEditors(into: directory) ? 0 : 1
    }

    /// The layout round 5 set: the same three editors, the shorter column spread to the taller one's height.
    static func runLayoutRound5(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        return writeLayoutEditors(into: directory) ? 0 : 1
    }

    /// editor-matrix.png, editor-mods.png and editor-busy.png (Modal Synth with 16 knobs as the Source,
    /// Granular Delay with 15, the most a Part FX has, as the Part FX, the tallest engine in every mod card).
    private static func writeLayoutEditors(into directory: String) -> Bool {
        let core = EG13PreviewCore.modulationDemo()  // the synth holds its core unowned: keep it alive
        let synth = EG13Synth(core: core, kind: .instrument)
        for _ in 0..<70 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        func matrix() -> EG13RightColumnState { let c = EG13RightColumnState(); c.pane = .matrix; return c }
        ok = write(EG13RootView(synth: synth, column: matrix()), size: base, zoom: 1,
                   to: "\(directory)/editor-matrix.png") && ok
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-mods.png") && ok
        // Busy: Modal Synth (16 knobs) as the source, Granular Delay (15, the most a Part FX has) as the Part FX.
        let busyCore = EG13PreviewCore.modulationDemo()
        busyCore.select(EG13_SLOT_KIND_SOURCE, engine: 2, part: 0)
        busyCore.select(EG13_SLOT_KIND_PART_FX, engine: 2, part: 0)
        // Every mod card the tallest too, so the Mods column is taller than the strip and scrolls.
        for kind in [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4] {
            busyCore.select(kind, engine: 3, part: 0)
        }
        let busy = EG13Synth(core: busyCore, kind: .instrument)
        for _ in 0..<70 { busy.tick() }
        ok = write(EG13RootView(synth: busy), size: base, zoom: 1, to: "\(directory)/editor-busy.png") && ok
        withExtendedLifetime((core, busyCore)) {}
        return ok
    }

    /// The preset bar design set (M7 gate 5a): the header at 75, 100 and 200 %, the dropdown for the
    /// instrument and EG13 FX (a drawn copy of the macOS menu), an edited name, a name too long for
    /// the bar, and Save As with its clash question.
    static func runPresets(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let work = FileManager.default.temporaryDirectory.appendingPathComponent("eg13-presets-snapshot-\(UUID().uuidString)")
        defer { try? FileManager.default.removeItem(at: work) }
        func state(_ loaded: [(Int, String, String, String)], edited: Bool = false) -> String {
            let items = loaded.map { "{\"part\":\($0.0),\"bank\":\"\($0.1)\",\"name\":\"\($0.2)\",\"kind\":\"\($0.3)\"}" }
            return "{\"version\":1,\"loaded\":[\(items.joined(separator: ","))],\"edited\":\(edited)}"
        }
        /// A synth over a preview core with two User presets in a folder of its own.
        func make(fx: Bool = false, _ browser: String) -> (EG13PreviewCore, EG13Synth)? {
            let core = EG13PreviewCore.standard()
            core.fxList = fx
            core.browser = browser
            let folder = work.appendingPathComponent(fx ? "fx" : "instrument")
            try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let files = fx ? [("Warm Wash", nil as Int?), ("Dark Spring", 0)] : [("My Pad", nil), ("Bass test", 1)]
            for (name, part) in files {
                let url = folder.appendingPathComponent(name).appendingPathExtension(part == nil ? "eg13perf" : "eg13patch")
                try? core.savePreset(part: part, name: name).write(to: url, atomically: true, encoding: .utf8)
            }
            let synth = EG13Synth(core: core, kind: fx ? .effect : .instrument, presetsFolder: folder)
            for _ in 0..<12 { synth.tick() }
            return (core, synth)
        }
        let perf = "performance", patch = "patch"
        guard let (c1, glass) = make(state([(-1, "Bank 1", "Glass Tide", perf)])),
              let (c2, mine) = make(state([(-1, "User", "My Pad", perf)])),
              let (c3, fx) = make(fx: true, state([(-1, "FX", "Ring Shimmer", perf)])),
              let (c4, edited) = make(state([(-1, "Bank 1", "Glass Tide", perf)], edited: true)),
              let (c5, long) = make(state([(0, "Bank 2", "catnap \u{2014} pluck side", patch),
                                           (1, "Bank 2", "catnap \u{2014} pad side with a very long extended name", patch)],
                                          edited: true))
        else { return 1 }
        let base = EG13Layout.baseSize
        let strip = CGSize(width: base.width, height: EG13Layout.headerHeight)
        var ok = true
        ok = write(EG13HeaderPreview(synth: glass), size: strip, zoom: 1, to: "\(directory)/header-a.png") && ok
        ok = write(EG13HeaderPreview(synth: glass), size: strip, zoom: 0.75, to: "\(directory)/header-75.png") && ok
        ok = write(EG13HeaderPreview(synth: glass), size: strip, zoom: 2, to: "\(directory)/header-200.png") && ok
        ok = write(EG13HeaderPreview(synth: edited), size: strip, zoom: 1, to: "\(directory)/edited.png") && ok
        long.selectedPart = 0
        ok = write(EG13HeaderPreview(synth: long), size: strip, zoom: 1, to: "\(directory)/long-name.png") && ok
        let sheet = CGSize(width: base.width, height: 640)
        ok = write(EG13PresetSheet(synth: mine, content: EG13PresetBarSnapshots.menu(mine.presets, openPatch: "Bowed Bells")),
                   size: sheet, zoom: 1, to: "\(directory)/menu-instrument.png") && ok
        ok = write(EG13PresetSheet(synth: fx, content: EG13PresetBarSnapshots.menu(fx.presets, openPatch: "Tape Smear")),
                   size: CGSize(width: base.width, height: 420), zoom: 1, to: "\(directory)/menu-fx.png") && ok
        let dialogSize = CGSize(width: base.width, height: 250)
        ok = write(EG13PresetSheet(synth: mine, content: EG13PresetBarSnapshots.saveAs(
                    mine.presets, name: "Evening Glass", scope: .whole, clash: false)),
                   size: dialogSize, zoom: 1, to: "\(directory)/save-as.png") && ok
        ok = write(EG13PresetSheet(synth: mine, content: EG13PresetBarSnapshots.saveAs(
                    mine.presets, name: "My Pad", scope: .whole, clash: true)),
                   size: dialogSize, zoom: 1, to: "\(directory)/clash.png") && ok
        withExtendedLifetime((c1, c2, c3, c4, c5)) {}
        return ok ? 0 : 1
    }

    /// The layout design set: both parts at 100%, Part A at 75%, the header close-up, a contact sheet
    /// of every engine for each slot kind, and the worst case (the tallest engines in every slot) as
    /// the full height of both columns, with the visible strip marked.
    static func runEditor(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.standard()
        let synth = EG13Synth(core: core, kind: .instrument)
        // Meters need a few frames to show a level.
        for _ in 0..<12 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        synth.selectedPart = 0
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-a.png") && ok
        ok = write(EG13RootView(synth: synth), size: base, zoom: 0.75, to: "\(directory)/editor-a-75.png") && ok
        ok = write(EG13HeaderPreview(synth: synth), size: CGSize(width: base.width, height: EG13Layout.headerHeight),
                   zoom: 1, to: "\(directory)/master.png") && ok
        synth.selectedPart = 1
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-b.png") && ok
        for sheet in EG13ContactSheet.all {
            ok = writeFitted(sheet, to: "\(directory)/\(sheet.fileName).png") && ok
        }
        // Worst case: the engine with the most knobs in every slot, Part A.
        let worst = EG13PreviewCore.standard()
        worst.select(EG13_SLOT_KIND_SOURCE, engine: 2, part: 0)
        worst.select(EG13_SLOT_KIND_FILTER, engine: 1, part: 0)
        worst.select(EG13_SLOT_KIND_SHAPER, engine: 2, part: 0)
        worst.select(EG13_SLOT_KIND_PART_FX, engine: 2, part: 0)
        for kind in [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3, EG13_SLOT_KIND_MOD4] {
            worst.select(kind, engine: 3, part: 0)
        }
        let worstSynth = EG13Synth(core: worst, kind: .instrument)
        for _ in 0..<12 { worstSynth.tick() }
        ok = writeFitted(EG13WorstCaseSheet(synth: worstSynth), to: "\(directory)/worst-case-a.png") && ok
        return ok ? 0 : 1
    }
}

/// Both card columns at their full height, as the scrolling columns hold them, with a line where
/// the visible strip ends (an image render cannot draw a scroll view).
struct EG13WorstCaseSheet: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text("Worst case, Part A: the tallest engine in every slot. The columns scroll below the line.")
                .font(.system(size: 16, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            HStack(alignment: .top, spacing: EG13Layout.gap) {
                EG13SlotCards(synth: synth, part: 0, width: EG13Layout.slotCardWidth, columnHeight: 0)
                EG13ModCards(synth: synth, part: 0, width: EG13Layout.modCardWidth, columnHeight: 0)
            }
            .overlay(alignment: .top) {
                Rectangle().fill(EG13Palette.black.color).frame(height: 2).offset(y: EG13Layout.stripHeight)
            }
        }
    }
}

/// The header at the editor's width, for the close-up of the master settings.
struct EG13HeaderPreview: View {
    let synth: EG13Synth
    var body: some View {
        EG13RootView(synth: synth)
            .frame(width: EG13Layout.baseSize.width, height: EG13Layout.headerHeight, alignment: .top)
            .clipped()
    }
}

/// The Part settings popover's content for both parts, side by side on the window grey.
struct EG13PartSettingsSheet: View {
    let synth: EG13Synth
    static let size = CGSize(width: 860, height: 520)
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(alignment: .top, spacing: theme.spacing.l) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in
                EG13PartSettingsView(synth: synth, part: part)
                    .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
            }
        }
        .padding(theme.spacing.l)
        .frame(width: Self.size.width, height: Self.size.height, alignment: .topLeading)
        .background(theme.surface.window.color)
    }
}

/// One slot's card for every engine the slot has, one under another (Part A's colours).
struct EG13ContactSheet: View {
    let fileName: String
    let title: String
    let kind: EG13SlotKind
    let number: Int?
    /// One synth per engine, each over a core with that engine selected. The cores are
    /// held here: a synth does not keep its core alive.
    let cores: [EG13PreviewCore]
    let synths: [EG13Synth]
    @Environment(\.symbioticTheme) private var theme

    init(fileName: String, title: String, kind: EG13SlotKind, number: Int? = nil) {
        self.fileName = fileName
        self.title = title
        self.kind = kind
        self.number = number
        var cores: [EG13PreviewCore] = [], synths: [EG13Synth] = []
        for engine in 0..<EG13EngineCatalog.engines(kind: Int(kind.rawValue)).count {
            let core = EG13PreviewCore.standard()
            core.select(kind, engine: engine, part: 0)
            cores.append(core)
            synths.append(EG13Synth(core: core, kind: .instrument))
        }
        self.cores = cores
        self.synths = synths
    }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("\(title): every engine, Part A")
                .font(.system(size: 20, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            VStack(alignment: .leading, spacing: EG13Layout.gap) {
                ForEach(synths.indices, id: \.self) { index in
                    if let number {
                        EG13ModuleCard(synth: synths[index], kind: kind, part: 0, number: number,
                                       width: EG13Layout.modCardWidth)
                    } else {
                        EG13ModuleCard(synth: synths[index], kind: kind, part: 0,
                                       width: EG13Layout.slotCardWidth)
                    }
                }
            }
        }
    }

    @MainActor
    static var all: [EG13ContactSheet] {
        [
            EG13ContactSheet(fileName: "source-engines", title: "Source", kind: EG13_SLOT_KIND_SOURCE),
            EG13ContactSheet(fileName: "filter-engines", title: "Filter", kind: EG13_SLOT_KIND_FILTER),
            EG13ContactSheet(fileName: "shaper-engines", title: "Shaper", kind: EG13_SLOT_KIND_SHAPER),
            EG13ContactSheet(fileName: "partfx-engines", title: "Part FX", kind: EG13_SLOT_KIND_PART_FX),
            EG13ContactSheet(fileName: "mods-engines", title: "Mod slot", kind: EG13_SLOT_KIND_MOD1, number: 1),
        ]
    }
}

/// The mark close-up: the Filter card, then one knob per cap colour with the modulation mark at 75 %,
/// 100 % and 200 %, set at both ends of the range and in the middle so the pointer's clearance shows.
struct EG13MarkSheet: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    private let values: [(String, Double)] = [("min", 0), ("25 %", 0.25), ("mid", 0.5), ("75 %", 0.75), ("max", 1)]
    private let caps: [(String, EG13ParamEmphasis)] = [("white", .plain), ("pink", .pink), ("teal", .teal), ("black", .black)]
    private func row(_ zoom: CGFloat) -> some View {
        let d = EG13Layout.knobDiameter(.large)
        return VStack(alignment: .leading, spacing: theme.spacing.xs) {
            Text("\(Int(zoom * 100)) %")
                .font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
            ForEach(caps, id: \.0) { cap in
                HStack(spacing: theme.spacing.m) {
                    ForEach(values, id: \.0) { value in
                        EG13KnobShape(normalized: value.1, style: EG13Palette.knobStyle(for: cap.1),
                                      size: .large, showsModulationMark: true)
                            .frame(width: d, height: d)
                            .scaleEffect(zoom)
                            .frame(width: d * zoom, height: d * zoom)
                    }
                }
            }
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("Modulation mark: white, pink, teal, black; min, 25 %, mid, 75 %, max")
                .font(.system(size: 16, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            EG13ModuleCard(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: 0, width: 420)
            row(0.75)
            row(1)
            row(2)
        }
        .frame(width: 640, alignment: .leading)
    }
}

/// The knob cap request: the three sizes in each cap colour at a few values, then the same knobs
/// with another plugin's colours injected (the layers and proportions are the same; only the slots change).
struct EG13KnobCapSheet: View {
    @Environment(\.symbioticTheme) private var theme
    private let caps: [(String, EG13ParamEmphasis)] = [("white", .plain), ("pink", .pink), ("teal", .teal), ("black", .black)]
    private let sizes: [EG13KnobSize] = [.large, .medium, .small]

    private func recoloured(_ cap: EG13ParamEmphasis.Cap) -> EG13KnobStyle {
        var style = EG13Palette.knobStyle(for: .plain)
        switch cap {
        case .pink:
            style.cap = .accent
            style.accent = Color(red: 0.95, green: 0.62, blue: 0.1)
            style.accentInk = Color(red: 0.62, green: 0.38, blue: 0.0)
            style.label = .chip
        case .teal:
            style.cap = .accent
            style.accent = Color(red: 0.3, green: 0.5, blue: 0.95)
            style.accentInk = Color(red: 0.16, green: 0.3, blue: 0.7)
            style.label = .chip
        default: break
        }
        return style
    }

    private func line(_ title: String, style: @escaping (EG13ParamEmphasis) -> EG13KnobStyle) -> some View {
        VStack(alignment: .leading, spacing: theme.spacing.xs) {
            Text(title).font(theme.type.caption.font.weight(.bold)).foregroundStyle(theme.text.secondary.color)
            ForEach(caps, id: \.0) { cap in
                HStack(spacing: theme.spacing.m) {
                    ForEach(sizes, id: \.self) { size in
                        ForEach([0.0, 0.35, 0.8], id: \.self) { value in
                            let d = EG13Layout.knobDiameter(size)
                            EG13KnobShape(normalized: value, style: style(cap.1), size: size)
                                .frame(width: d, height: d)
                                .frame(width: 52, height: 52)
                        }
                    }
                }
            }
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            line("EG13 colours: rows white, pink, teal, black cap; large, medium, small at 0, 35, 80 %") { EG13Palette.knobStyle(for: $0) }
            line("Another plugin's colours injected (same geometry): white, amber, blue, black") { emphasis in
                var style = recoloured(emphasis.cap)
                if emphasis.cap == .black { style = EG13Palette.knobStyle(for: .black) }
                return style
            }
        }
        .padding(theme.spacing.m)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
    }
}

/// The modulation wave mark on its own: each cap colour with the pointer up (mark in the bottom third)
/// and down (mark in the top third), large and medium.
struct EG13MarkRequestSheet: View {
    @Environment(\.symbioticTheme) private var theme
    private let caps: [EG13ParamEmphasis] = [.plain, .pink, .teal, .black]
    private let values: [(String, Double)] = [("pointer up, 50 %", 0.5), ("up, 75 %", 0.75), ("down, 5 %", 0.05), ("down, 95 %", 0.95)]

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.xs) {
            ForEach([EG13KnobSize.large, .medium], id: \.self) { size in
                let d = EG13Layout.knobDiameter(size) * 1.5
                HStack(alignment: .top, spacing: theme.spacing.l) {
                    ForEach(values, id: \.0) { value in
                        VStack(spacing: theme.spacing.xs) {
                            HStack(spacing: theme.spacing.s) {
                                ForEach(caps.indices, id: \.self) { i in
                                    EG13KnobShape(normalized: value.1, style: EG13Palette.knobStyle(for: caps[i]),
                                                  size: size, showsModulationMark: true)
                                        .frame(width: EG13Layout.knobDiameter(size), height: EG13Layout.knobDiameter(size))
                                }
                            }
                            Text(value.0).font(.system(size: 10, design: .monospaced)).foregroundStyle(theme.text.secondary.color)
                        }
                    }
                }
                .frame(minHeight: d / 1.5)
            }
        }
        .padding(theme.spacing.m)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
    }
}

/// LCD readout and button: value wells, the steppers with minus and plus, the engine menu well, and a
/// card header with the MOTION n button over its popover list with level bars.
struct EG13LCDRequestSheet: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme
    let voices: EG13Param
    let engine: EG13Param

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            HStack(spacing: theme.spacing.m) {
                EG13LCDText(text: "Value 0.62").frame(width: 88, height: 26)
                EG13LCDText(text: "Focused", isFocused: true).frame(width: 88, height: 26)
                EG13Stepped(param: voices)
                EG13EnginePicker(names: EG13PreviewCore.engineNames, selector: engine,
                                 selected: 2, accessibilityName: "Engine", wellWidth: EG13Layout.enginePickerWidth)
            }
            EG13ModuleCard(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: 0, width: EG13Layout.slotCardWidth)
            EG13MotionList(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: 0)
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(theme.text.secondary.color.opacity(0.4), lineWidth: 1))
        }
        .padding(theme.spacing.m)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
    }
}

/// The Motion list of the Filter card on its own (a popover cannot be rendered to an image), under
/// the card whose header holds the button.
struct EG13MotionSheet: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("Motion list of the Filter card, Part A (shown at 200 %)")
                .font(.system(size: 16, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            EG13ModuleCard(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: 0, width: EG13Layout.slotCardWidth)
            EG13MotionList(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: 0)
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .overlay(RoundedRectangle(cornerRadius: 6).stroke(theme.text.secondary.color.opacity(0.4), lineWidth: 1))
        }
    }
}

/// The knob context menu's content as a list (a real NSMenu cannot be rendered to an image): the menu
/// with "Modulate from" and the removals, and the submenu of sources beside it.
struct EG13ContextMenuSheet: View {
    let model: EG13ContextMenuModel
    let knob: String
    @Environment(\.symbioticTheme) private var theme

    private func item(_ title: String, enabled: Bool = true, arrow: Bool = false) -> some View {
        HStack {
            Text(title)
            Spacer(minLength: theme.spacing.l)
            if arrow { Text("\u{25B8}") }
        }
        .font(.system(size: 13))
        .foregroundStyle(enabled ? Color.black : Color.black.opacity(0.35))
        .padding(.horizontal, 12)
        .padding(.vertical, 3)
    }

    private func menu<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 0, content: content)
            .padding(.vertical, 5)
            .frame(width: 280)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color(white: 0.96)))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color.black.opacity(0.2), lineWidth: 1))
    }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("Context menu of the \(knob) knob (right-click or ctrl-click); the submenu opens from Modulate from")
                .font(.system(size: 14, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            HStack(alignment: .top, spacing: 6) {
                menu {
                    item(model.modulateTitle, enabled: model.modulateEnabled, arrow: model.modulateEnabled)
                    if !model.removals.isEmpty {
                        Divider().padding(.vertical, 4)
                        ForEach(model.removals.indices, id: \.self) { item(model.removals[$0].title) }
                    }
                }
                if model.modulateEnabled {
                    // Modulate from > Keys / Controllers / Mods, with Controllers and Mods opened.
                    let groups = model.groups
                    menu { ForEach(groups.indices, id: \.self) { item(groups[$0].title, arrow: true) } }
                    if let controllers = groups.first(where: { $0.title == EG13SourceGrouping.controllersTitle }) {
                        menu { ForEach(controllers.items.indices, id: \.self) { item(controllers.items[$0].name) } }
                    }
                    if let mods = groups.first(where: { $0.title == EG13SourceGrouping.modsTitle }) {
                        menu { ForEach(mods.children.indices, id: \.self) { item(mods.children[$0].title, arrow: true) } }
                        if let first = mods.children.first {
                            menu { ForEach(first.items.indices, id: \.self) { item(first.items[$0].name) } }
                        }
                    }
                }
            }
        }
    }
}

/// The header with something drawn under the preset bar (a menu or a dialog), on the window grey.
struct EG13PresetSheet<Content: View>: View {
    let synth: EG13Synth
    let content: Content
    /// Where the bar's left edge is in the header (measured from a header snapshot).
    var barX: CGFloat { synth.kind == .effect ? 306 : 284 }

    var body: some View {
        ZStack(alignment: .topLeading) {
            EG13Theme.theme.surface.window.color
            EG13HeaderPreview(synth: synth)
            content.offset(x: barX, y: EG13Layout.headerHeight - 12)
        }
    }
}
