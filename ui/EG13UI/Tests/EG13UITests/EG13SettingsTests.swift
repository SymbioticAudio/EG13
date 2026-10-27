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
