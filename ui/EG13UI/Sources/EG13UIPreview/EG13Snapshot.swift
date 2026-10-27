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
        ok = writeFitted(EG13ModCards(synth: synth, part: 0, width: EG13Layout.modCardWidth),
                         to: "\(directory)/traces.png", scale: 2) && ok
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
                EG13SlotCards(synth: synth, part: 0, width: EG13Layout.slotCardWidth)
                EG13ModCards(synth: synth, part: 0, width: EG13Layout.modCardWidth)
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
    static let size = CGSize(width: 1000, height: 440)
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
