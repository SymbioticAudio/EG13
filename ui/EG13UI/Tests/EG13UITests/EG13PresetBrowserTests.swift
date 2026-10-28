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

    /// The two tabs' names, A then B.
    static func names(_ browser: EG13PresetBrowser) -> [String] { [browser.partName(0), browser.partName(1)] }
    static func edited(_ browser: EG13PresetBrowser) -> [Bool] { [browser.isEdited(part: 0), browser.isEdited(part: 1)] }

    @Test("the browser has no step: the header has no arrows (owner redesign 2026-10-27)")
    func noStep() {
        // Compile-time: EG13PresetBrowser.step(_:) is gone. Loading goes through the menu only.
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        #expect(Self.names(browser) == ["Init", "Init"])
    }

    @Test("each tab names what its part holds: a Patch by name, a Performance as name·A / name·B, else Init")
    func partNames() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let perf = Self.factory("Bank 1", "Harbour Lights", .performance, 0)
        let patch = Self.factory("Bank 1", "Comb Choir", .patch, 1)
        let other = Self.factory("Bank 2", "Tin Choir", .patch, 2)
        let (_, browser) = Self.make(factory: [perf, patch, other], folder: folder)
        #expect(Self.names(browser) == ["Init", "Init"])
        browser.load(patch, into: .part(0))
        #expect(Self.names(browser) == ["Comb Choir", "Init"])
        browser.load(other, into: .part(1))
        #expect(Self.names(browser) == ["Comb Choir", "Tin Choir"])
        browser.load(perf)
        #expect(Self.names(browser) == ["Harbour Lights\u{00B7}A", "Harbour Lights\u{00B7}B"])
        // A Patch later replaces only that part's name.
        browser.load(patch, into: .part(1))
        #expect(Self.names(browser) == ["Harbour Lights\u{00B7}A", "Comb Choir"])
        browser.loadInit()
        #expect(Self.names(browser) == ["Init", "Init"])
    }

    @Test("a tab's help is the full name, and a credited preset's credit and licence")
    func partHelp() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let catnap = EG13PresetEntry(bank: "Bank 2", name: "catnap", kind: .performance, credit: Self.catnapCredit,
                                     licence: "CC BY 4.0", source: .factory(1), json: "{}")
        let plain = Self.factory("Bank 1", "A very long name that the tab cuts", .patch, 0)
        let (_, browser) = Self.make(factory: [plain, catnap], folder: folder)
        #expect(browser.partHelp(0) == "Init")
        browser.load(catnap)
        #expect(browser.partHelp(1) == "catnap\u{00B7}B\n\(catnap.attribution!)")
        browser.load(plain, into: .part(0))
        #expect(browser.partHelp(0) == "A very long name that the tab cuts")
    }

    @Test("an editor edit marks its own part, loading clears it, and host-only value changes don't")
    func editedMark() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.full()
        core.factory = [Self.factory("Bank 1", "One", .performance, 0), Self.factory("Bank 1", "Pat", .patch, 1)]
        let synth = EG13Synth(core: core, kind: .instrument, presetsFolder: folder)
        synth.presets.load(synth.presets.entries[0])
        #expect(Self.edited(synth.presets) == [false, false])
        // The host changes a value: a snapshot, no gesture.
        core.values[0] = 0.3
        synth.tick()
        #expect(Self.edited(synth.presets) == [false, false])
        synth.edit(EG13ParamID.part(1, .pan), 0.5)
        #expect(Self.edited(synth.presets) == [false, true])
        #expect(core.browser.contains("\"edited\":[false,true]"))
        // A Patch into B clears B only; A stays as it was.
        synth.edit(EG13ParamID.part(0, .pan), 0.5)
        synth.presets.load(synth.presets.entries[1], into: .part(1))
        #expect(Self.edited(synth.presets) == [true, false])
        synth.presets.load(synth.presets.entries[0])
        #expect(Self.edited(synth.presets) == [false, false])
        #expect(core.browser.contains("\"edited\":[false,false]"))
        // A route change from the editor counts too, for its part.
        _ = synth.setRoute(part: 1, index: 0, source: 1, destination: 1, via: 0)
        #expect(Self.edited(synth.presets) == [false, true])
    }

    @Test("a master control that is part of the sound (Mode) marks both parts")
    func masterMarksBoth() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.full()
        core.factory = [Self.factory("Bank 1", "One", .performance, 0)]
        let synth = EG13Synth(core: core, kind: .instrument, presetsFolder: folder)
        synth.presets.load(synth.presets.entries[0])
        synth.edit(EG13ParamID.master(.mode), 1)
        #expect(Self.edited(synth.presets) == [true, true])
    }

    @Test("a setup control (master Level, MIDI channel) does not set the edited mark")
    func setupControlsDoNotMark() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.full()
        core.factory = [Self.factory("Bank 1", "One", .performance, 0)]
        let synth = EG13Synth(core: core, kind: .instrument, presetsFolder: folder)
        synth.presets.load(synth.presets.entries[0])
        synth.edit(EG13ParamID.master(.level), 0.5)
        synth.edit(EG13ParamID.part(1, .midiChannel), 5)
        #expect(Self.edited(synth.presets) == [false, false])
        #expect(!core.browser.contains("true"))
        // The sound still marks it.
        synth.edit(EG13ParamID.part(0, .pan), 0.5)
        #expect(Self.edited(synth.presets) == [true, false])
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
        #expect(Self.names(browser) == ["Glass\u{00B7}A", "Glass\u{00B7}B"])
        // Now the loaded preset is the User one: Save overwrites it in place.
        #expect(browser.save() == .saved)
        #expect(browser.entries.count == 2)
        // Init has nothing to save under; Save As is needed.
        browser.loadInit()
        #expect(browser.save() == .refused("Use Save As to name this sound."))
        _ = core
    }

    @Test("Save on a factory preset whose User copy exists opens Save As asking to replace it")
    func saveFactoryTwice() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, browser) = Self.make(factory: [Self.factory("Bank 1", "Glass", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        // First time: no dialog, the User copy is made.
        #expect(EG13PresetDialog.save(browser) == nil)
        #expect(Self.names(browser) == ["Glass\u{00B7}A", "Glass\u{00B7}B"])
        let url = folder.appendingPathComponent("Glass.eg13perf")
        try? "marker".write(to: url, atomically: true, encoding: .utf8)
        // Reload the factory preset and save again: the clash opens the dialog at the Replace question.
        browser.load(browser.entries[0])
        browser.markEdited(part: 0)
        var dialog = EG13PresetDialog.save(browser)
        #expect(dialog?.mode == .saveAs)
        #expect(dialog?.name == "Glass")
        #expect(dialog?.clash == true)
        #expect(dialog?.action == "Replace")
        #expect(dialog?.prompt?.contains("already exists") == true)
        #expect((try? String(contentsOf: url, encoding: .utf8)) == "marker")  // nothing written yet
        // Confirming replaces the User copy and clears the mark.
        #expect(dialog?.submit(browser) == true)
        #expect((try? String(contentsOf: url, encoding: .utf8)) != "marker")
        #expect(Self.edited(browser) == [false, false])
        _ = dialog
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
        #expect(Self.names(browser) == ["Glass", "Glass\u{00B7}B"])
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
        #expect(browser.partName(0) == "A/B: C 🎹\u{00B7}A")
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
        #expect(browser.partName(0) == "Good\u{00B7}A")
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
        #expect(browser.partName(0) == "One\u{00B7}A")
        #expect(browser.message == "This preset is for EG13 FX.")
        core.applyResult = EG13PresetApplyResult(status: .warnings, message: "An engine was missing.")
        browser.load(browser.entries[1])
        #expect(browser.partName(0) == "Two\u{00B7}A")
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
        #expect(browser.partName(1) == "New\u{00B7}B")
        #expect(!FileManager.default.fileExists(atPath: folder.appendingPathComponent("Old.eg13perf").path))
        browser.delete(browser.entries[0])
        #expect(browser.entries.map(\.name) == ["Other"])
        #expect(browser.rename(browser.entries[0], to: "") != .saved)
    }

    @Test("browser state restores the names and per-part edited marks; an empty, old or unknown state is Init")
    func restoreState() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (_, empty) = Self.make(folder: folder)
        #expect(Self.names(empty) == ["Init", "Init"])
        #expect(Self.edited(empty) == [false, false])
        let saved = #"{"version":2,"loaded":[{"part":-1,"bank":"Bank 1","name":"Glass","kind":"performance"}],"edited":[false,true]}"#
        let (_, restored) = Self.make(browser: saved, folder: folder)
        #expect(Self.names(restored) == ["Glass\u{00B7}A", "Glass\u{00B7}B"])
        #expect(Self.edited(restored) == [false, true])
        let parts = #"{"version":2,"loaded":[{"part":0,"bank":"User","name":"x","kind":"patch"},{"part":1,"bank":"User","name":"y","kind":"patch"}],"edited":[true,false]}"#
        let (_, two) = Self.make(browser: parts, folder: folder)
        #expect(Self.names(two) == ["x", "y"])
        #expect(Self.edited(two) == [true, false])
        // The format before the per-part mark (version 1), junk and a newer version all read as Init.
        let old = #"{"version":1,"loaded":[{"part":-1,"bank":"Bank 1","name":"Glass","kind":"performance"}],"edited":true}"#
        for junk in ["not json", "{}", old, #"{"version":3,"loaded":[],"edited":[true,true]}"#,
                     #"{"version":2,"loaded":[],"edited":true}"#] {
            let (_, broken) = Self.make(browser: junk, folder: folder)
            #expect(Self.names(broken) == ["Init", "Init"])
            #expect(Self.edited(broken) == [false, false])
        }
    }

    @Test("every load, save and edit-flag change writes the browser state, and a repeat edit writes once")
    func persistence() throws {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        browser.markEdited(part: 1)
        browser.markEdited(part: 1)
        #expect(core.browserWrites.count == 2)
        browser.saveAs(name: "Mine", scope: .whole)
        #expect(core.browserWrites.count == 3)
        let last = try #require(JSONSerialization.jsonObject(with: Data(core.browserWrites[2].utf8)) as? [String: Any])
        #expect(last["version"] as? Int == 2)
        #expect(last["edited"] as? [Bool] == [false, false])
        let loaded = try #require(last["loaded"] as? [[String: Any]])
        #expect(loaded.first?["name"] as? String == "Mine")
        #expect(loaded.first?["bank"] as? String == "User")
        // A new browser over the same string shows the same name.
        let again = EG13PresetBrowser(core: core, kind: .instrument, folder: folder)
        #expect(again.partName(1) == "Mine\u{00B7}B")
    }

    @Test("loadInit asks the plugin, clears the edited mark and shows Init")
    func initPreset() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0)], folder: folder)
        browser.load(browser.entries[0])
        browser.markEdited(part: nil)
        browser.loadInit()
        #expect(core.initCalls == 1)
        #expect(Self.names(browser) == ["Init", "Init"])
        #expect(Self.edited(browser) == [false, false])
        #expect(core.browser.contains("\"loaded\":[]") && core.browser.contains("\"edited\":[false,false]"))
        // A refused Init changes nothing.
        browser.load(browser.entries[0])
        core.applyResult = EG13PresetApplyResult(status: .invalid, message: "no")
        browser.loadInit()
        #expect(browser.partName(0) == "One\u{00B7}A")
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
        #expect(browser.partName(0) == "glass\u{00B7}A")
    }

    // MARK: Menu (header dropdown)

    @Test("menu groups by bank, marks kinds, ticks what A and B hold (a Performance once) and keeps User listed")
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
        browser.shownPart = { 1 }  // the tick does not follow the shown part
        #expect(browser.menu.groups[0].rows.map(\.ticked) == [false, true])
        browser.load(list[2])
        #expect(browser.menu.groups.flatMap(\.rows).map(\.ticked) == [false, false, true])
        // A Performance in A and a Patch in B: both ticked.
        browser.load(list[1], into: .part(1))
        #expect(browser.menu.groups.flatMap(\.rows).map(\.ticked) == [false, true, true])
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
        #expect(Self.names(synth.presets) == ["Init", "Init"])
        core.browser = #"{"version":2,"loaded":[{"part":-1,"bank":"Bank 1","name":"One","kind":"performance"}],"edited":[true,false]}"#
        synth.tick()  // no new generation: nothing re-read
        #expect(Self.names(synth.presets) == ["Init", "Init"])
        core.generation += 1  // the host's state load bumps the generation
        synth.tick()
        #expect(Self.names(synth.presets) == ["One\u{00B7}A", "One\u{00B7}B"])
        #expect(Self.edited(synth.presets) == [true, false])
        _ = browser
    }

    static let catnapCredit = "~zero-g:catnap by plantay (patchstorage.com/zero-gcatnap-vcv-rack-jam)"

    @Test("a credited preset shows credit and licence; EG13's own and uncredited presets show none")
    func attribution() {
        let catnap = EG13PresetEntry(bank: "Bank 2", name: "catnap", kind: .performance, credit: Self.catnapCredit,
                                     licence: "CC BY 4.0", source: .factory(0))
        #expect(catnap.attribution == "\(Self.catnapCredit) \u{2014} CC BY 4.0")
        let own = EG13PresetEntry(bank: "Bank 1", name: "First Light", kind: .performance, credit: "EG13 factory",
                                  licence: "GPL-3.0-or-later", source: .factory(1))
        #expect(own.attribution == nil)
        #expect(Self.factory("Bank 1", "Plain", .patch, 2).attribution == nil)
        let noLicence = EG13PresetEntry(bank: "User", name: "Shared", kind: .patch, credit: "A Friend",
                                        source: .user(URL(fileURLWithPath: "/tmp/x.eg13patch")))
        #expect(noLicence.attribution == "A Friend")
    }

    @Test("loading a credited preset shows its credit and licence as the message; the menu row carries it as help")
    func attributionOnLoadAndMenu() {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let catnap = EG13PresetEntry(bank: "Bank 2", name: "catnap", kind: .performance, credit: Self.catnapCredit,
                                     licence: "CC BY 4.0", source: .factory(1), json: "{}")
        let (core, browser) = Self.make(factory: [Self.factory("Bank 1", "One", .performance, 0), catnap], folder: folder)
        browser.load(catnap)
        #expect(browser.message == catnap.attribution)
        browser.load(browser.entries[0])
        #expect(browser.message == nil)
        // A warning from the plugin comes first; the credit stays on the menu row.
        core.applyResult = EG13PresetApplyResult(status: .warnings, message: "Some settings were left out.")
        browser.load(catnap)
        #expect(browser.message == "Some settings were left out.")
        let rows = browser.menu.groups.flatMap(\.rows)
        #expect(rows.first { $0.entry.name == "catnap" }?.help == catnap.attribution)
        #expect(rows.first { $0.entry.name == "One" }?.help == nil)
    }

    @Test("a User file's credit and licence are read")
    func userFileLicence() throws {
        let folder = Self.tempFolder()
        defer { try? FileManager.default.removeItem(at: folder) }
        let core = EG13FakeCore.standard()
        let url = Self.writeUser(core, in: folder, file: "shared.eg13perf", name: "Shared")
        var root = try #require(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
        root["credit"] = "A Friend"
        root["licence"] = "CC BY 4.0"
        try JSONSerialization.data(withJSONObject: root).write(to: url)
        let (_, browser) = Self.make(folder: folder)
        let entry = try #require(browser.entries.first)
        #expect(entry.licence == "CC BY 4.0")
        #expect(entry.attribution == "A Friend \u{2014} CC BY 4.0")
    }
}
