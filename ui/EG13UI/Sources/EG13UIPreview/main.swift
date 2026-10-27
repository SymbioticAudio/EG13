import AppKit
import SwiftUI
import EG13UI
import SymbioticTokens

// EG13UIPreview: the editor and the Controls page in a plain window over a fake plugin.
//   swift run --package-path ui/EG13UI EG13UIPreview                    interactive window
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot <dir>   write the Controls page PNGs and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-editor <dir>   write the layout PNGs and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-modulation <dir>   write the modulation PNGs and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-matrix <dir>   write the matrix editor PNGs and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-key-range <dir>   write the Part settings popover PNG and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r1 <dir>   write the M6b layout round 1 PNGs (matrix, Part settings, Settings/About, context menu) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r2 <dir>   write the M6b layout round 2 PNGs (matrix, mods, busy patch, grouped context menu) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r3 <dir>   write the M6b layout round 3 PNGs (matrix, mods, busy patch) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r4 <dir>   write the M6b layout round 4 PNGs (matrix, mods, busy patch) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r5 <dir>   write the M6b layout round 5 PNGs (matrix, mods, busy patch) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-presets <dir>   write the M7 preset bar PNGs (header at 75/100/200 %, dropdown, edited, long name, Save As, clash) and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --modulation [--mod-load]   modulation page (animated mods); --mod-load is the init patch with a chord, 3 modulated knobs and 3 traces (for CPU sampling)
//   swift run --package-path ui/EG13UI EG13UIPreview --editor [--drag-load]   editor page; --drag-load sweeps Part A's Level 30 times a second (for CPU sampling)
//   swift run --package-path ui/EG13UI EG13UIPreview --editor-static   editor page over a static patch with no notes held: the mods produce nothing, as the plugin idles (for CPU sampling)

private struct EG13PreviewRoot: View {
    enum Page: String, CaseIterable { case controls = "Controls", editor = "Editor", modulation = "Modulation" }
    @State private var page: Page
    @State private var zoom = 1.0
    let demo: EG13ControlsDemo
    let synth: EG13Synth
    /// The Modulation page: the same editor over a core that animates its mods and holds a chord.
    let modulationSynth: EG13Synth
    let dragLoad: Bool

    init(core: EG13PreviewCore, modulationCore: EG13PreviewCore, page: Page, dragLoad: Bool = false) {
        self.dragLoad = dragLoad
        _page = State(initialValue: page)
        demo = EG13ControlsDemo(core: core)
        synth = EG13Synth(core: core, kind: .instrument)
        modulationSynth = EG13Synth(core: modulationCore, kind: .instrument)
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Picker("Page", selection: $page) {
                    ForEach(Page.allCases, id: \.self) { Text($0.rawValue).tag($0) }
                }
                .pickerStyle(.segmented).frame(width: 200).labelsHidden()
                Picker("Zoom", selection: $zoom) {
                    Text("75%").tag(0.75); Text("100%").tag(1.0); Text("200%").tag(2.0)
                }
                .pickerStyle(.segmented).frame(width: 220).labelsHidden()
                Spacer()
            }
            .padding(8)
            ScrollView([.horizontal, .vertical]) {
                Group {
                    switch page {
                    case .controls: EG13ControlsPage(demo: demo)
                    case .editor: EG13RootView(synth: synth)
                    case .modulation: EG13RootView(synth: modulationSynth)
                    }
                }
                .scaleEffect(zoom, anchor: .topLeading)
                .frame(width: contentSize.width * zoom, height: contentSize.height * zoom, alignment: .topLeading)
                .symbioticTheme(EG13Theme.theme)
            }
        }
        .onReceive(Timer.publish(every: 1.0 / 30, on: .main, in: .common).autoconnect()) { _ in
            (page == .modulation ? modulationSynth : synth).tick()
        }
        .onReceive(Timer.publish(every: 1.0 / 30, on: .main, in: .common).autoconnect()) { date in
            // A held knob: one gesture left open, a triangle sweep through the range at 30 Hz.
            guard dragLoad, let level = synth.partParam(0, .level) else { return }
            let phase = date.timeIntervalSinceReferenceDate.truncatingRemainder(dividingBy: 2) / 2
            if !synth.isEditing(level.id) { synth.begin(level.id) }
            synth.edit(level.id, level.value(forNormalized: phase < 0.5 ? 2 * phase : 2 - 2 * phase))
        }
    }

    private var contentSize: CGSize {
        page == .controls ? EG13ControlsPage.size : EG13Layout.baseSize
    }
}

MainActor.assumeIsolated {
    let arguments = CommandLine.arguments
    if let flag = arguments.firstIndex(of: "--snapshot") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.run(into: arguments[flag + 1]))
    }
    if let flag = arguments.firstIndex(of: "--snapshot-editor") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-editor <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runEditor(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-modulation") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-modulation <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runModulation(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-matrix") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-matrix <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runMatrix(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-key-range") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-key-range <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runKeyRange(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-layout-r1") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-layout-r1 <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runLayoutRound1(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-layout-r2") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-layout-r2 <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runLayoutRound2(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-layout-r3") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-layout-r3 <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runLayoutRound3(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-layout-r4") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-layout-r4 <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runLayoutRound4(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-layout-r5") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-layout-r5 <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runLayoutRound5(into: arguments[flag + 1]))
    }

    if let flag = arguments.firstIndex(of: "--snapshot-presets") {
        guard flag + 1 < arguments.count else {
            FileHandle.standardError.write(Data("usage: EG13UIPreview [--snapshot-presets <dir>]\n".utf8))
            exit(2)
        }
        _ = NSApplication.shared
        exit(EG13Snapshot.runPresets(into: arguments[flag + 1]))
    }

    let app = NSApplication.shared
    app.setActivationPolicy(.regular)
    let modLoad = arguments.contains("--mod-load")
    let staticIdle = arguments.contains("--editor-static")
    let page: EG13PreviewRoot.Page = modLoad || arguments.contains("--modulation") ? .modulation
        : arguments.contains("--editor") || staticIdle ? .editor : .controls
    let modulationCore: EG13PreviewCore = modLoad ? .modulationLoad() : .modulationDemo()
    let core = EG13PreviewCore.standard()
    core.staticIdle = staticIdle
    let host = NSHostingView(rootView: EG13PreviewRoot(core: core, modulationCore: modulationCore, page: page,
                                                       dragLoad: arguments.contains("--drag-load")))
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 900),
                          styleMask: [.titled, .closable, .miniaturizable, .resizable],
                          backing: .buffered, defer: false)
    window.title = "EG13 UI preview"
    window.contentView = host
    window.center()
    window.makeKeyAndOrderFront(nil)
    app.activate(ignoringOtherApps: true)
    final class Delegate: NSObject, NSApplicationDelegate {
        func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
    }
    let delegate = Delegate()
    app.delegate = delegate
    app.run()
}
