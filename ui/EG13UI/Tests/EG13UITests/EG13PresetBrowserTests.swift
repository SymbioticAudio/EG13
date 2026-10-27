import Foundation
import Testing
@testable import EG13UI

@MainActor
@Suite("Preset browser")
struct EG13PresetBrowserTests {
    /// A fresh folder per test; each test removes it with `defer`.
    static func tempFolder() -> URL {
        let url = FileManager.default.temporaryDirectory.appendingPathComponent("eg13-presets-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
        return url
    }

    static func factory(_ bank: String, _ name: String, _ kind: EG13PresetEntry.Kind, _ index: Int) -> EG13PresetEntry {
        EG13PresetEntry(bank: bank, name: name, kind: kind, source: .factory(index), json: "{\"factory\":\"\(name)\"}")
    }

    /// A user file made the way the plugin writes one.
    @discardableResult
    static func writeUser(_ core: EG13FakeCore, in folder: URL, file: String, name: String, part: Int? = nil) -> URL {
        let url = folder.appendingPathComponent(file)
        try? core.savePreset(part: part, name: name).write(to: url, atomically: true, encoding: .utf8)
        return url
    }

    static func make(factory: [EG13PresetEntry] = [], kind: EG13Synth.Kind = .instrument,
                     browser: String = "", folder: URL) -> (EG13FakeCore, EG13PresetBrowser) {
        let core = EG13FakeCore.standard()
        core.factory = factory
        core.browser = browser
        core.pluginKind = kind == .effect ? "effect" : "instrument"
        return (core, EG13PresetBrowser(core: core, kind: kind, folder: folder))
    }

    @Test("list order is Bank 1, Bank 2, User for the instrument and FX, User for EG13 FX")
    func listOrder() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.standard()
        Self.writeUser(core, in: folder, file: "zeta.eg13perf", name: "Zeta")
        Self.writeUser(core, in: folder, file: "alpha.eg13patch", name: "Alpha", part: 0)
        let list = [Self.factory("Bank 1", "One", .performance, 0), Self.factory("Bank 2", "Two", .patch, 1)]
        let (_, instrument) = Self.make(factory: list, folder: folder)
        #expect(instrument.entries.map(\.name) == ["One", "Two", "Alpha", "Zeta"])
        #expect(instrument.entries.map(\.bank) == ["Bank 1", "Bank 2", "User", "User"])

        let fxFolder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: fxFolder) }
        let fxCore = EG13FakeCore.standard()
        fxCore.pluginKind = "effect"
        Self.writeUser(fxCore, in: fxFolder, file: "wash.eg13perf", name: "My Wash")
        let (_, fx) = Self.make(factory: [Self.factory("FX", "Wash", .performance, 0)], kind: .effect, folder: fxFolder)
        #expect(fx.entries.map(\.bank) == ["FX", "User"])
        #expect(EG13PresetBrowser.defaultUserURL(for: .effect).lastPathComponent == "FX")
    }

    @Test("the default folder follows EG13_PRESETS_PATH")
    func defaultFolder() {
        setenv("EG13_PRESETS_PATH", "/nonexistent/eg13-test/presets", 1)
        defer { unsetenv("EG13_PRESETS_PATH") }
        #expect(EG13PresetBrowser.defaultUserURL.path == "/nonexistent/eg13-test/presets")
        #expect(EG13PresetBrowser.defaultUserURL(for: .effect).path == "/nonexistent/eg13-test/presets/FX")
    }

    @Test("step wraps from the last User preset to the first Bank 1 preset and loads Patches into the shown part")
    func stepWraps() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.standard()
        Self.writeUser(core, in: folder, file: "mine.eg13perf", name: "Mine")
        let list = [Self.factory("Bank 1", "One", .patch, 0), Self.factory("Bank 2", "Two", .performance, 1)]
        let (fake, browser) = Self.make(factory: list, folder: folder)
        var shown = 1
        browser.shownPart = { shown }
        browser.step(1)  // from Init: the first
        #expect(fake.applied.last?.target == .part(1))
        #expect(browser.displayName == "B: One")
        browser.step(1)
        #expect(fake.applied.last?.target == .whole)
        #expect(browser.displayName == "Two")
        browser.step(1)
        #expect(browser.displayName == "Mine")
        browser.step(1)  // wraps to the first Bank 1 preset, a Patch, into the shown part
        #expect(fake.applied.last?.target == .part(1))
        #expect(browser.displayName == "A: Mine / B: One")
        shown = 0
        browser.step(-1)  // from what part A holds, back across the wrap
        #expect(browser.displayName == "Two")
        #expect(fake.applied.count == 5)
    }

    @Test("step from Init goes back to the last preset")
    func stepBackFromInit() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0),
                                               Self.factory("Bank 1", "Two", .performance, 1)], folder: folder)
        browser.step(-1)
        #expect(browser.displayName == "Two")
    }

    @Test("a Patch after a Performance names each part, and a Patch into A then B shows both")
    func displayNames() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let perf = Self.factory("Bank 1", "Perf", .performance, 0)
        let patch = Self.factory("Bank 1", "Patch", .patch, 1)
        let (_, browser) = Self.make(factory: [perf, patch], folder: folder)
        #expect(browser.displayName == "Init")
        browser.load(patch, into: .part(0))
        #expect(browser.displayName == "A: Patch")
        browser.load(patch, into: .part(1))
        #expect(browser.displayName == "A: Patch / B: Patch")
        browser.load(perf)
        #expect(browser.displayName == "Perf")
        browser.load(patch, into: .part(1))
        #expect(browser.displayName == "A: Perf / B: Patch")
    }

    @Test("an editor edit marks edited, loading clears it, and host-only value changes don't")
    func editedMark() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.full()
        core.factory = [Self.factory("Bank 1", "One", .performance, 0)]
        let synth = EG13Synth(core: core, kind: .instrument, presetsFolder: folder)
        synth.presets.load(synth.presets.entries[0])
        #expect(!synth.presets.isEdited)
        // The host changes a value: a snapshot, no gesture.
        core.values[0] = 0.3
        synth.tick()
        #expect(!synth.presets.isEdited)
        synth.edit(EG13ParamID.master(.level), 0.5)
        #expect(synth.presets.isEdited)
        #expect(core.browser.contains("\"edited\":true"))
        synth.presets.load(synth.presets.entries[0])
        #expect(!synth.presets.isEdited)
        #expect(core.browser.contains("\"edited\":false"))
        // A route change from the editor counts too.
        _ = synth.setRoute(part: 0, index: 0, source: 1, destination: 1, via: 0)
        #expect(synth.presets.isEdited)
    }

    @Test("Save on a factory preset acts as Save As into User")
    func saveFactory() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "Glass", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        #expect(browser.save() == .saved)
        #expect(FileManager.default.fileExists(atPath: folder.appendingPathComponent("Glass.eg13perf").path))
        #expect(browser.entries.map(\.bank) == ["Bank 1", "User"])
        #expect(browser.displayName == "Glass")
        // Now the loaded preset is the User one: Save overwrites it in place.
        #expect(browser.save() == .saved)
        #expect(browser.entries.count == 2)
        // Init has nothing to save under; Save As is needed.
        browser.loadInit()
        #expect(browser.save() == .refused("Use Save As to name this sound."))
        _ = core
    }

    @Test("Save As to an existing name reports a clash and replaces only after confirmation")
    func saveAsClash() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(folder: folder)
        #expect(browser.saveAs(name: "Glass", scope: .whole) == .saved)
        let url = folder.appendingPathComponent("Glass.eg13perf")
        try? "marker".write(to: url, atomically: true, encoding: .utf8)
        #expect(browser.saveAs(name: "glass", scope: .whole) == .clash)
        #expect((try? String(contentsOf: url, encoding: .utf8)) == "marker")
        #expect(browser.saveAs(name: "Glass", scope: .whole, replace: true) == .saved)
        #expect((try? String(contentsOf: url, encoding: .utf8)) != "marker")
        // A Patch of the same name is a different preset.
        #expect(browser.saveAs(name: "Glass", scope: .part(0)) == .saved)
        #expect(browser.entries.count == 2)
        #expect(browser.displayName == "A: Glass / B: Glass")
        _ = core
    }

    @Test("names with / : and emoji save to a sanitised file name and keep the name; an empty name is refused")
    func sanitisedNames() throws {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(folder: folder)
        #expect(browser.saveAs(name: "  ", scope: .whole) == .refused("Enter a name for the preset."))
        #expect(browser.saveAs(name: "", scope: .whole) != .saved)
        #expect(browser.saveAs(name: "A/B: C 🎹", scope: .whole) == .saved)
        let files = try FileManager.default.contentsOfDirectory(atPath: folder.path)
        #expect(files == ["A-B- C 🎹.eg13perf"])
        #expect(browser.entries.map(\.name) == ["A/B: C 🎹"])
        #expect(browser.displayName == "A/B: C 🎹")
        // The folder holds nothing but that file (no path escapes).
        #expect(EG13PresetLibrary.fileName(for: "../../etc") == "-..-etc")
        #expect(EG13PresetLibrary.fileName(for: "...") == "Untitled")
        #expect(EG13PresetLibrary.fileName(for: "a\u{0}b\nc") == "a-b-c")
    }

    @Test("unreadable or foreign files are skipped and listed in message")
    func foreignFiles() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.standard()
        Self.writeUser(core, in: folder, file: "good.eg13perf", name: "Good")
        try? "{\"eg13Preset\":1,".write(to: folder.appendingPathComponent("truncated.eg13perf"), atomically: true, encoding: .utf8)
        try? "hello".write(to: folder.appendingPathComponent("notes.eg13patch"), atomically: true, encoding: .utf8)
        try? "{\"eg13Preset\":2,\"kind\":\"patch\",\"plugin\":\"instrument\",\"state\":{\"params\":{}}}"
            .write(to: folder.appendingPathComponent("newer.eg13patch"), atomically: true, encoding: .utf8)
        let fx = EG13FakeCore.standard()
        fx.pluginKind = "effect"
        Self.writeUser(fx, in: folder, file: "fx.eg13perf", name: "An FX")
        try? "x".write(to: folder.appendingPathComponent("readme.txt"), atomically: true, encoding: .utf8)
        let (_, browser) = Self.make(folder: folder)
        #expect(browser.entries.map(\.name) == ["Good"])
        let message = browser.message ?? ""
        #expect(message.contains("Skipped 4 files"))
        #expect(message.contains("fx.eg13perf: is for EG13 FX"))
        #expect(message.contains("(and 1 more)"))
        #expect(!message.contains("readme.txt"))
        // The rest of the list works.
        browser.load(browser.entries[0])
        #expect(browser.displayName == "Good")
    }

    @Test("a preset the plugin refuses leaves the name alone and says why")
    func refusedPreset() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let list = [Self.factory("Bank 1", "One", .performance, 0), Self.factory("Bank 1", "Two", .performance, 1)]
        let (core, browser) = Self.make(factory: list, folder: folder)
        browser.load(browser.entries[0])
        core.applyResult = EG13PresetApplyResult(status: .invalid, message: "This preset is for EG13 FX.")
        browser.load(browser.entries[1])
        #expect(browser.displayName == "One")
        #expect(browser.message == "This preset is for EG13 FX.")
        core.applyResult = EG13PresetApplyResult(status: .warnings, message: "An engine was missing.")
        browser.load(browser.entries[1])
        #expect(browser.displayName == "Two")
        #expect(browser.message == "An engine was missing.")
    }

    @Test("User presets sort by name, case-insensitively")
    func sorting() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.standard()
        for (file, name) in [("1.eg13perf", "banana"), ("2.eg13perf", "Cherry"), ("3.eg13perf", "apple"), ("4.eg13perf", "Banana2")] {
            Self.writeUser(core, in: folder, file: file, name: name)
        }
        let (_, browser) = Self.make(folder: folder)
        #expect(browser.entries.map(\.name) == ["apple", "banana", "Banana2", "Cherry"])
    }

    @Test("rename moves the file and the name, and delete removes it")
    func renameDelete() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(folder: folder)
        browser.saveAs(name: "Old", scope: .whole)
        browser.saveAs(name: "Other", scope: .whole)
        browser.saveAs(name: "Old", scope: .whole, replace: true)
        let old = browser.entries.first { $0.name == "Old" }!
        #expect(browser.rename(old, to: "Other") == .clash)
        #expect(browser.rename(old, to: "New") == .saved)
        #expect(browser.entries.map(\.name) == ["New", "Other"])
        #expect(browser.displayName == "New")
        #expect(!FileManager.default.fileExists(atPath: folder.appendingPathComponent("Old.eg13perf").path))
        browser.delete(browser.entries[0])
        #expect(browser.entries.map(\.name) == ["Other"])
        #expect(browser.rename(browser.entries[0], to: "") != .saved)
    }

    @Test("browser state restores the name and edited mark, and an empty state shows Init")
    func restoreState() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, empty) = Self.make(folder: folder)
        #expect(empty.displayName == "Init")
        #expect(!empty.isEdited)
        let saved = "{\"version\":1,\"loaded\":[{\"part\":-1,\"bank\":\"Bank 1\",\"name\":\"Glass\",\"kind\":\"performance\"}],\"edited\":true}"
        let (_, restored) = Self.make(browser: saved, folder: folder)
        #expect(restored.displayName == "Glass")
        #expect(restored.isEdited)
        let parts = "{\"version\":1,\"loaded\":[{\"part\":0,\"bank\":\"User\",\"name\":\"x\",\"kind\":\"patch\"},{\"part\":1,\"bank\":\"User\",\"name\":\"y\",\"kind\":\"patch\"}],\"edited\":false}"
        let (_, two) = Self.make(browser: parts, folder: folder)
        #expect(two.displayName == "A: x / B: y")
        for junk in ["not json", "{}", "{\"version\":2,\"loaded\":[],\"edited\":true}"] {
            let (_, broken) = Self.make(browser: junk, folder: folder)
            #expect(broken.displayName == "Init")
            #expect(!broken.isEdited)
        }
    }

    @Test("every load, save and edit-flag change writes the browser state, and a repeat edit writes once")
    func persistence() throws {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        browser.markEdited()
        browser.markEdited()
        #expect(core.browserWrites.count == 2)
        browser.saveAs(name: "Mine", scope: .whole)
        #expect(core.browserWrites.count == 3)
        let last = try #require(JSONSerialization.jsonObject(with: Data(core.browserWrites[2].utf8)) as? [String: Any])
        #expect(last["version"] as? Int == 1)
        #expect(last["edited"] as? Bool == false)
        let loaded = try #require(last["loaded"] as? [[String: Any]])
        #expect(loaded.first?["name"] as? String == "Mine")
        #expect(loaded.first?["bank"] as? String == "User")
        // A new browser over the same string shows the same name.
        let again = EG13PresetBrowser(core: core, kind: .instrument, folder: folder)
        #expect(again.displayName == "Mine")
    }

    @Test("loadInit asks the plugin, clears the edited mark and shows Init")
    func initPreset() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        browser.markEdited()
        browser.loadInit()
        #expect(core.initCalls == 1)
        #expect(browser.displayName == "Init")
        #expect(!browser.isEdited)
        #expect(core.browser.contains("\"loaded\":[]") && core.browser.contains("\"edited\":false"))
        // A refused Init changes nothing.
        browser.load(browser.entries[0])
        core.applyResult = EG13PresetApplyResult(status: .invalid, message: "no")
        browser.loadInit()
        #expect(browser.displayName == "One")
        #expect(browser.message == "no")
    }

    @Test("a case-only rename keeps the preset")
    func caseOnlyRename() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(folder: folder)
        browser.saveAs(name: "Glass", scope: .whole)
        #expect(browser.rename(browser.entries[0], to: "glass") == .saved)
        let files = (try? FileManager.default.contentsOfDirectory(atPath: folder.path)) ?? []
        #expect(files == ["glass.eg13perf"])
        #expect(browser.entries.map(\.name) == ["glass"])
        #expect(browser.displayName == "glass")
    }

    // MARK: Menu (header dropdown)

    @Test("menu groups by bank, marks kinds, ticks the shown part's preset and keeps User listed when empty")
    func menuGroups() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let list = [Self.factory("Bank 1", "Perf", .performance, 0), Self.factory("Bank 1", "Patch", .patch, 1),
                    Self.factory("Bank 2", "catnap", .performance, 2)]
        let (_, browser) = Self.make(factory: list, folder: folder)
        var menu = browser.menu
        #expect(menu.groups.map(\.title) == ["Bank 1", "Bank 2", "User"])
        #expect(menu.groups[0].rows.map(\.mark) == ["A+B", "A|B"])
        #expect(menu.groups[2].rows.isEmpty)
        #expect(menu.groups.flatMap(\.rows).allSatisfy { !$0.ticked })
        browser.load(list[1], into: .part(0))
        menu = browser.menu
        #expect(menu.groups[0].rows.map(\.ticked) == [false, true])
        browser.shownPart = { 1 }  // part B holds nothing from the list
        #expect(browser.menu.groups.flatMap(\.rows).allSatisfy { !$0.ticked })
    }

    @Test("menu enables Save, Rename and Delete only where they apply")
    func menuEnabledStates() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let perf = Self.factory("Bank 1", "Perf", .performance, 0)
        let patch = Self.factory("Bank 1", "Patch", .patch, 1)
        let (_, browser) = Self.make(factory: [perf, patch], folder: folder)
        // Init: only Save As.
        var menu = browser.menu
        #expect(!menu.canSave && menu.canSaveAs && !menu.canRename && !menu.canDelete)
        // A factory Performance: Save (as a User copy) and Save As, but no Rename or Delete.
        browser.load(perf)
        menu = browser.menu
        #expect(menu.canSave && menu.canSaveAs && !menu.canRename && !menu.canDelete)
        // A saved User preset: everything.
        #expect(browser.saveAs(name: "Mine", scope: .whole) == .saved)
        menu = browser.menu
        #expect(menu.canSave && menu.canRename && menu.canDelete)
        // A name per part: Save has no single preset, Rename and Delete follow the shown part.
        browser.load(patch, into: .part(1))
        browser.shownPart = { 1 }
        menu = browser.menu
        #expect(!menu.canSave && !menu.canRename && !menu.canDelete)
        browser.shownPart = { 0 }
        menu = browser.menu
        #expect(!menu.canSave && menu.canRename && menu.canDelete)
    }

    @Test("a host load changes the browser string; the next engine generation re-reads it")
    func hostLoadRereadsBrowserString() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        let synth = EG13Synth(core: core, kind: .instrument, presetsFolder: folder)
        #expect(synth.presets.displayName == "Init")
        core.browser = #"{"version":1,"loaded":[{"part":-1,"bank":"Bank 1","name":"One","kind":"performance"}],"edited":true}"#
        synth.tick()  // no new generation: nothing re-read
        #expect(synth.presets.displayName == "Init")
        core.generation += 1  // the host's state load bumps the generation
        synth.tick()
        #expect(synth.presets.displayName == "One")
        #expect(synth.presets.isEdited)
        _ = browser
    }
}
