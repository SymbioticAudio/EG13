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

    /// The editor and the layout design set: both parts at 100%, Part A at 75%, the Part
    /// settings popover on its own and a contact sheet of every engine for each slot kind.
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
        synth.selectedPart = 1
        ok = write(EG13RootView(synth: synth), size: base, zoom: 1, to: "\(directory)/editor-b.png") && ok
        ok = write(EG13PartSettingsSheet(synth: synth), size: EG13PartSettingsSheet.size, zoom: 1,
                   to: "\(directory)/part-settings.png") && ok
        for sheet in EG13ContactSheet.all {
            ok = write(sheet, size: sheet.size, zoom: 1, to: "\(directory)/\(sheet.fileName).png") && ok
        }
        return ok ? 0 : 1
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

/// One slot's card for every engine the slot has, side by side (Part A's colours).
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
        for engine in 0..<EG13PreviewCore.engines(Int(kind.rawValue)).count {
            let core = EG13PreviewCore.standard()
            core.select(kind, engine: engine, part: 0)
            cores.append(core)
            synths.append(EG13Synth(core: core, kind: .instrument))
        }
        self.cores = cores
        self.synths = synths
    }

    var size: CGSize {
        if number == nil {
            return CGSize(width: CGFloat(synths.count) * (EG13Layout.cardWidth + theme.spacing.s) + 32,
                          height: EG13Layout.columnStripHeight + 76)
        }
        let columns = 2
        let rows = (synths.count + columns - 1) / columns
        return CGSize(width: CGFloat(columns) * (EG13Layout.modsColumnWidth + theme.spacing.s) + 32,
                      height: CGFloat(rows) * (EG13Layout.modCardHeight + theme.spacing.s) + 76)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("\(title): every engine, Part A")
                .font(.system(size: 20, weight: .light))
                .foregroundStyle(theme.text.primary.color)
            if let number {
                let columns = Array(repeating: GridItem(.fixed(EG13Layout.modsColumnWidth), spacing: theme.spacing.s), count: 2)
                LazyVGrid(columns: columns, alignment: .leading, spacing: theme.spacing.s) {
                    ForEach(synths.indices, id: \.self) { index in
                        EG13ModCard(synth: synths[index], kind: kind, number: number, part: 0)
                    }
                }
            } else {
                HStack(alignment: .top, spacing: theme.spacing.s) {
                    ForEach(synths.indices, id: \.self) { index in
                        EG13SlotCard(synth: synths[index], kind: kind, part: 0)
                            .frame(height: EG13Layout.columnStripHeight)
                    }
                }
            }
        }
        .padding(16)
        .frame(width: size.width, height: size.height, alignment: .topLeading)
        .background(theme.surface.window.color)
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
