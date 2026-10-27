import EG13CBridge
import Foundation
import Testing
@testable import EG13UI

/// The app-wide settings file: defaults for anything unreadable, atomic writes, written only on a
/// change, and a default zoom that only new editors use.
@MainActor
struct EG13SettingsTests {
    let directory: URL
    var file: URL { directory.appendingPathComponent("settings.json") }

    init() throws {
        directory = FileManager.default.temporaryDirectory
            .appendingPathComponent("eg13-settings-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    }

    private func write(_ text: String) throws { try Data(text.utf8).write(to: file) }
    private func names() -> [String] {
        ((try? FileManager.default.contentsOfDirectory(atPath: directory.path)) ?? []).sorted()
    }

    @Test("Defaults: 100 % and modulation display on")
    func defaults() {
        let store = EG13SettingsStore(url: file)
        #expect(store.settings == EG13Settings())
        #expect(store.settings.defaultWindowPercent == 100)
        #expect(store.settings.modulationDisplay)
        #expect(store.settings.defaultZoom == 1.0)
    }

    @Test("Corrupt settings load as defaults")
    func corruptLoadsAsDefaults() throws {
        for text in ["", "{", "not json", "[1,2,3]", "{\"defaultWindowPercent\": \"big\"}", "\u{0}\u{1}"] {
            try write(text)
            #expect(EG13SettingsStore(url: file).settings == EG13Settings(), "for \(text)")
        }
        // A percent that is not one of the five sizes is treated as unreadable too.
        try write("{\"schemaVersion\":1,\"defaultWindowPercent\":333,\"modulationDisplay\":false}")
        #expect(EG13SettingsStore(url: file).settings == EG13Settings())
    }

    @Test("A newer file loads as defaults")
    func newerFileLoadsAsDefaults() throws {
        try write("{\"schemaVersion\":\(EG13Settings.schemaVersion + 1),\"defaultWindowPercent\":150,\"modulationDisplay\":false}")
        #expect(EG13SettingsStore(url: file).settings == EG13Settings())
    }

    @Test("Unknown keys are ignored, known ones are read")
    func unknownKeysAreIgnored() throws {
        try write("{\"schemaVersion\":1,\"defaultWindowPercent\":150,\"modulationDisplay\":false,\"futureThing\":{\"a\":1}}")
        let store = EG13SettingsStore(url: file)
        #expect(store.settings.defaultWindowPercent == 150)
        #expect(!store.settings.modulationDisplay)
        // A missing key keeps its default.
        try write("{\"defaultWindowPercent\":75}")
        let partial = EG13SettingsStore(url: file)
        #expect(partial.settings.defaultWindowPercent == 75)
        #expect(partial.settings.modulationDisplay)
    }

    @Test("The file is written only when the user changes a setting")
    func writesOnlyOnChange() throws {
        let store = EG13SettingsStore(url: file)
        store.reload()
        #expect(names().isEmpty)
        store.update { $0.modulationDisplay = true }   // same value
        store.update { $0.defaultWindowPercent = 100 }  // same value
        #expect(names().isEmpty)
        store.update { $0.defaultWindowPercent = 150 }
        #expect(names() == ["settings.json"])
        let reread = EG13SettingsStore(url: file)
        #expect(reread.settings.defaultWindowPercent == 150)
        let before = try Data(contentsOf: file)
        store.update { $0.defaultWindowPercent = 150 }
        #expect(try Data(contentsOf: file) == before)
    }

    @Test("Saving writes atomically: no temp file is left, and a failed save keeps the old file")
    func savingIsAtomic() throws {
        let store = EG13SettingsStore(url: file)
        store.update { $0.defaultWindowPercent = 125 }
        store.update { $0.modulationDisplay = false }
        #expect(names() == ["settings.json"])  // the temp files were renamed away
        let good = try Data(contentsOf: file)

        // The directory refuses new files: the write fails, and the old file is untouched.
        try FileManager.default.setAttributes([.posixPermissions: 0o500], ofItemAtPath: directory.path)
        defer { try? FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: directory.path) }
        store.update { $0.defaultWindowPercent = 200 }
        #expect(try Data(contentsOf: file) == good)
        #expect(names() == ["settings.json"])
        #expect(store.settings.defaultWindowPercent == 200)  // the session still uses it
        #expect(store.saveError != nil, "the failure is reported, not silent")
        // The next write that works clears the message.
        try FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: directory.path)
        store.update { $0.defaultWindowPercent = 150 }
        #expect(store.saveError == nil)
    }

    @Test("The settings file can be set from code, in any build")
    func settingsURLSeam() {
        let url = directory.appendingPathComponent("seam.json")
        EG13SettingsLocation.testingURL = url
        defer { EG13SettingsLocation.testingURL = nil }
        #expect(EG13SettingsStore.defaultURL == url)
    }

    @Test("The path overrides exist in debug builds only")
    func overridesAreDebugOnly() {
        setenv("EG13_SETTINGS_PATH", "/nonexistent/eg13-test/override.json", 1)
        defer { unsetenv("EG13_SETTINGS_PATH") }
        #if DEBUG
        #expect(EG13SettingsStore.defaultURL.path == "/nonexistent/eg13-test/override.json")
        #else
        #expect(EG13SettingsStore.defaultURL.path != "/nonexistent/eg13-test/override.json")
        #endif
    }

    @Test("A missing directory is created on the first save")
    func createsDirectory() {
        let nested = directory.appendingPathComponent("a/b/settings.json")
        let store = EG13SettingsStore(url: nested)
        store.update { $0.modulationDisplay = false }
        #expect(FileManager.default.fileExists(atPath: nested.path))
    }

    @Test("A store without a file (tests, previews) never touches the disk")
    func inMemoryStore() {
        let store = EG13SettingsStore(url: nil)
        store.update { $0.defaultWindowPercent = 75 }
        #expect(store.settings.defaultWindowPercent == 75)
        #expect(names().isEmpty)
    }

    @Test("The default zoom applies to new editors only")
    func defaultZoomAppliesToNewEditorsOnly() {
        let store = EG13SettingsStore(url: file)
        let core = EG13FakeCore.full()
        let synth = EG13Synth(core: core, kind: .instrument)
        defer { withExtendedLifetime(core) {} }
        // 0 means "use the default from settings"; an explicit zoom is kept.
        #expect(store.initialZoom(requested: 0) == 1.0)
        let first = EG13PluginView(synth: synth, zoom: CGFloat(store.initialZoom(requested: 0)))
        #expect(first.frame.width == EG13Layout.baseSize.width)

        store.update { $0.defaultWindowPercent = 150 }
        #expect(first.frame.width == EG13Layout.baseSize.width)  // the open editor does not move
        #expect(store.initialZoom(requested: 0) == 1.5)
        #expect(store.initialZoom(requested: 0.75) == 0.75)
        let second = EG13PluginView(synth: synth, zoom: CGFloat(store.initialZoom(requested: 0)))
        #expect(second.frame.width == EG13Layout.baseSize.width * 1.5)
        #expect(first.frame.width == EG13Layout.baseSize.width)
    }

    @Test("Modulation display off stops the modulation refresh and clears the marks")
    func modulationDisplayOff() {
        let store = EG13SettingsStore(url: nil)
        let core = EG13FakeCore.full()
        defer { withExtendedLifetime(core) {} }
        let synth = EG13Synth(core: core, kind: .instrument, settings: store)
        #expect(synth.showsModulation)
        store.update { $0.modulationDisplay = false }
        #expect(!synth.showsModulation)
        let before = synth.modulationRefreshCount
        for _ in 0..<10 { synth.tick() }
        #expect(synth.modulationRefreshCount == before)
        store.update { $0.modulationDisplay = true }
        for _ in 0..<10 { synth.tick() }
        #expect(synth.modulationRefreshCount > before)
    }
}

@MainActor
struct EG13SettingsOverlayTests {
    @Test("an open overlay disables the editor behind it and hides it from VoiceOver")
    func openOverlayTrapsInput() {
        var state = EG13SettingsOverlayState()
        #expect(state.contentIsInteractive && !state.contentIsHiddenFromAccessibility)
        state.open()
        #expect(state.isOpen && state.tab == .settings)
        #expect(!state.contentIsInteractive && state.contentIsHiddenFromAccessibility)
        state.tab = .about
        state.open()
        #expect(state.tab == .about, "opening again keeps the tab")
        state.close()
        #expect(state.contentIsInteractive && !state.contentIsHiddenFromAccessibility)
    }

    @Test("Escape closes an open overlay and is ignored when closed")
    func escapeCloses() {
        var state = EG13SettingsOverlayState()
        let whenClosed = state.handleEscape()
        #expect(!whenClosed)
        state.open()
        let whenOpen = state.handleEscape()
        #expect(whenOpen)
        #expect(!state.isOpen)
    }

    @Test("with the modulation display off the marks, MOTION buttons and traces are hidden, and return")
    func modulationOffHidesAndRestores() {
        let store = EG13SettingsStore(url: nil)
        let core = EG13FakeCore.fullCatalog()
        for part in 0..<2 {
            core.select(EG13_SLOT_KIND_FILTER, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD1, engine: 1, part: part)
        }
        let synth = EG13Synth(core: core, kind: .instrument, settings: store)
        let filterP1 = EG13ParamID.slot(0, EG13_SLOT_KIND_FILTER, 1)
        core.routes[0][0] = .init(source: EG13ModulationViewTests.modWheel, destination: EG13ModulationViewTests.filterP1, via: 0)
        core.generation += 1
        synth.tickModulation()
        let trace = synth.modTrace(part: 0, slot: 0)
        #expect(synth.param(filterP1)?.isRouted == true)
        #expect(synth.showsMotionButton(kind: EG13_SLOT_KIND_FILTER, part: 0))
        #expect(trace.frameCount > 0)

        store.update { $0.modulationDisplay = false }
        synth.tick()
        #expect(synth.param(filterP1)?.isRouted == false, "marks hidden")
        #expect(!synth.showsMotionButton(kind: EG13_SLOT_KIND_FILTER, part: 0), "MOTION button hidden")
        #expect(trace.frameCount == 0, "trace cleared")
        for _ in 0..<10 { synth.tick() }
        #expect(trace.frameCount == 0, "trace does not advance")
        #expect(synth.param(filterP1)?.isRouted == false)

        // Every parameter stays reachable, and the matrix keeps its menus, with the display off.
        for part in 0..<EG13ParamID.parts {
            let controls = synth.editorControls(part: part).all.map(\.id)
            #expect(controls.count == Set(controls).count)
            #expect(controls.contains(filterP1) == (part == 0))
            #expect(synth.routeMenus(part: part).count == 3 * EG13ParamID.routeCount)
        }

        store.update { $0.modulationDisplay = true }
        synth.tickModulation()
        #expect(synth.param(filterP1)?.isRouted == true, "marks return")
        #expect(synth.showsMotionButton(kind: EG13_SLOT_KIND_FILTER, part: 0), "MOTION button returns")
        #expect(trace.frameCount > 0, "trace runs again")
    }
}
