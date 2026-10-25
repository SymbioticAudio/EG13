import AppKit
import SwiftUI
import EG13UI
import SymbioticTokens

// EG13UIPreview: the editor and the Controls page in a plain window over a fake plugin.
//   swift run --package-path ui/EG13UI EG13UIPreview                    interactive window
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot <dir>   write the Controls page PNGs and exit
//   swift run --package-path ui/EG13UI EG13UIPreview --snapshot-editor <dir>   write the layout PNGs and exit

private struct EG13PreviewRoot: View {
    enum Page: String, CaseIterable { case controls = "Controls", editor = "Editor" }
    @State private var page: Page
    @State private var zoom = 1.0
    let demo: EG13ControlsDemo
    @State private var variant: EG13EditorVariant
    let synth: EG13Synth

    init(core: EG13PreviewCore, page: Page, variant: EG13EditorVariant) {
        _variant = State(initialValue: variant)
        _page = State(initialValue: page)
        demo = EG13ControlsDemo(core: core)
        synth = EG13Synth(core: core, kind: .instrument)
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
                Picker("Variant", selection: $variant) {
                    ForEach(EG13EditorVariant.allCases, id: \.self) { Text("Mods \($0.rawValue)").tag($0) }
                }
                .pickerStyle(.segmented).frame(width: 160).labelsHidden()
                Spacer()
            }
            .padding(8)
            ScrollView([.horizontal, .vertical]) {
                Group {
                    switch page {
                    case .controls: EG13ControlsPage(demo: demo)
                    case .editor: EG13RootView(synth: synth, variant: variant)
                    }
                }
                .scaleEffect(zoom, anchor: .topLeading)
                .frame(width: contentSize.width * zoom, height: contentSize.height * zoom, alignment: .topLeading)
                .symbioticTheme(EG13Theme.theme)
            }
        }
        .onReceive(Timer.publish(every: 1.0 / 30, on: .main, in: .common).autoconnect()) { _ in synth.tick() }
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

    let app = NSApplication.shared
    app.setActivationPolicy(.regular)
    let variant: EG13EditorVariant = arguments.contains("--variant-b") ? .modsPage : .modsColumn
    let page: EG13PreviewRoot.Page = arguments.contains("--editor") ? .editor : .controls
    let host = NSHostingView(rootView: EG13PreviewRoot(core: .standard(), page: page, variant: variant))
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
