import AppKit
import EG13CBridge
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
    static func writeFitted<Content: View>(_ content: Content, to path: String) -> Bool {
        let view = content
            .padding(16)
            .background(EG13Theme.theme.surface.window.color)
            .fixedSize()
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

    /// The layout design set. For each variant (A: Mods in a column, B: Mods on their own page): both
    /// parts at 100% and Part A at 75%; B also shows its Mods page. Then the header close-up, the
    /// Part settings popover, a contact sheet of every engine for each slot kind, and the worst case
    /// (the tallest engines in every slot).
    static func runEditor(into directory: String) -> Int32 {
        guard makeDirectory(directory) else { return 1 }
        let core = EG13PreviewCore.standard()
        let synth = EG13Synth(core: core, kind: .instrument)
        // Meters need a few frames to show a level.
        for _ in 0..<12 { synth.tick() }
        let base = EG13Layout.baseSize
        var ok = true
        for variant in EG13EditorVariant.allCases {
            let v = variant.rawValue
            synth.showsMods = false
            synth.selectedPart = 0
            ok = write(EG13RootView(synth: synth, variant: variant), size: base, zoom: 1,
                       to: "\(directory)/editor-a-\(v).png") && ok
            ok = write(EG13RootView(synth: synth, variant: variant), size: base, zoom: 0.75,
                       to: "\(directory)/editor-a-75-\(v).png") && ok
            if variant == .modsPage {
                synth.showsMods = true
                ok = write(EG13RootView(synth: synth, variant: variant), size: base, zoom: 1,
                           to: "\(directory)/editor-a-mods-\(v).png") && ok
                synth.showsMods = false
            }
            synth.selectedPart = 1
            ok = write(EG13RootView(synth: synth, variant: variant), size: base, zoom: 1,
                       to: "\(directory)/editor-b-\(v).png") && ok
        }
        synth.selectedPart = 0
        ok = write(EG13HeaderPreview(synth: synth), size: CGSize(width: base.width, height: EG13Layout.headerHeight),
                   zoom: 1, to: "\(directory)/master.png") && ok
        ok = write(EG13PartSettingsSheet(synth: synth), size: EG13PartSettingsSheet.size, zoom: 1,
                   to: "\(directory)/part-settings.png") && ok
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
        ok = write(EG13RootView(synth: worstSynth, variant: .modsColumn), size: base, zoom: 1,
                   to: "\(directory)/worst-case-A.png") && ok
        return ok ? 0 : 1
    }
}

/// The header at the editor's width, for the close-up of the master settings.
struct EG13HeaderPreview: View {
    let synth: EG13Synth
    var body: some View {
        EG13RootView(synth: synth, variant: .modsPage)
            .frame(width: EG13Layout.baseSize.width, height: EG13Layout.headerHeight, alignment: .top)
            .clipped()
    }
}

/// The Part settings popover's content for both parts, side by side on the window grey.
struct EG13PartSettingsSheet: View {
    let synth: EG13Synth
    static let size = CGSize(width: 1000, height: 380)
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
                                       width: EG13Layout.modCardWidth(.modsColumn))
                    } else {
                        EG13ModuleCard(synth: synths[index], kind: kind, part: 0,
                                       width: EG13Layout.slotCardWidth(.modsColumn))
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
