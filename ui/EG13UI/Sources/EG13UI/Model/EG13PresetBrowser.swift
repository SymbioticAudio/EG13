import Foundation
import Observation

/// The preset browser behind the header: the list (factory presets, then the User folder), the
/// name shown, the edited mark, and load, step, save, rename and delete (M7 design §4.3).
///
/// What is loaded is kept as the "browser string" the plugin saves in the project (the format is
/// owned here; the plugin only stores it, and seeds it with the first-load preset on a new
/// instrument):
///
///     {"version":1,"loaded":[{"part":-1|0|1,"bank":"…","name":"…","kind":"performance"|"patch"}],"edited":false}
///
/// It is written after every load, save and change of the edited mark.
@MainActor
@Observable
public final class EG13PresetBrowser {
    /// One preset a part's sound came from. part -1 is a Performance (both parts).
    struct Loaded: Codable, Equatable {
        var part: Int
        var bank: String
        var name: String
        var kind: EG13PresetEntry.Kind
    }

    private struct Persisted: Codable {
        var version = 1
        var loaded: [Loaded] = []
        var edited = false
    }

    /// The list in order: Bank 1, Bank 2 (or FX for EG13 FX) as the plugin lists them, then the
    /// User presets by name, ignoring case.
    public private(set) var entries: [EG13PresetEntry] = []
    /// What the parts' sounds came from; empty is Init.
    private(set) var loaded: [Loaded] = []
    public private(set) var isEdited = false
    /// The last problem or warning (a preset refused, files skipped, a save that failed), or nil.
    public private(set) var message: String?

    /// The name the header shows:
    /// - nothing loaded, or an unreadable browser string: "Init";
    /// - a Performance (one entry for both parts): its name;
    /// - otherwise one "A: name" and/or "B: name" per part that has a preset, joined with " / ".
    ///   Loading a Patch into one part after a Performance turns the Performance into a name per
    ///   part first, so the result is "A: Performance / B: Patch".
    public var displayName: String {
        guard !loaded.isEmpty else { return "Init" }
        if loaded.count == 1, loaded[0].part < 0 { return loaded[0].name }
        return loaded.sorted { $0.part < $1.part }
            .map { "\($0.part == 0 ? "A" : "B"): \($0.name)" }.joined(separator: " / ")
    }

    /// The part a Patch loads into when no part is named: the editor's shown part (set by EG13Synth).
    @ObservationIgnored var shownPart: () -> Int = { 0 }

    @ObservationIgnored private let core: any EG13Core
    @ObservationIgnored private let kind: EG13Synth.Kind
    @ObservationIgnored private let library: EG13PresetLibrary
    @ObservationIgnored private var factory: [EG13PresetEntry] = []

    private var pluginName: String { kind == .effect ? "effect" : "instrument" }

    /// The User folder: `$EG13_PRESETS_PATH` if set (debug builds only: tests), else
    /// ~/Library/Application Support/SymbioticAudio/EG13/Presets. EG13 FX keeps its own in `FX/`
    /// inside it (see `defaultUserURL(for:)`).
    public static var defaultUserURL: URL {
        if let path = EG13DebugOverride.path("EG13_PRESETS_PATH") { return URL(fileURLWithPath: path) }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return support.appendingPathComponent("SymbioticAudio/EG13/Presets")
    }

    public static func defaultUserURL(for kind: EG13Synth.Kind) -> URL {
        kind == .effect ? defaultUserURL.appendingPathComponent("FX") : defaultUserURL
    }

    /// folder: the User folder; nil keeps no User presets and writes nothing (tests and previews).
    public init(core: any EG13Core, kind: EG13Synth.Kind, folder: URL? = nil) {
        self.core = core
        self.kind = kind
        library = EG13PresetLibrary(folder: folder)
        factory = core.factoryPresets()
        restore(core.browserState())
        reloadUserFolder()
    }

    // MARK: List

    /// Re-reads the User folder (the dropdown does this when it opens). Files that are not
    /// presets, are from a newer format or are for the other plugin are skipped and named in `message`.
    /// keepingMessage: a message already shown stays unless files were skipped (the header re-reads
    /// when the pointer reaches the bar, which must not wipe the message of a load).
    public func reloadUserFolder(keepingMessage: Bool = false) {
        let scan = library.scan(plugin: pluginName)
        entries = factory + scan.entries
        if !scan.skipped.isEmpty {
            message = Self.skippedMessage(scan.skipped)
        } else if !keepingMessage {
            message = nil
        }
    }

    /// Clears the message (the header shows one for a few seconds).
    public func dismissMessage() { message = nil }

    /// The list entry the shown part's sound came from (a Performance counts for both parts), or
    /// nil for Init or a name no longer in the list.
    var currentEntry: EG13PresetEntry? { currentIndex().map { entries[$0] } }

    /// Whether Save has one preset to act on: something is loaded and it is one preset, not a name per part.
    var canSave: Bool { loaded.count == 1 }

    /// Reads the project's browser string again. The host loading a project or an undo step
    /// changes it underneath the editor; the synth calls this when the engine generation changes.
    func refreshFromHost() {
        restore(core.browserState())
    }

    private static func skippedMessage(_ skipped: [String]) -> String {
        let shown = skipped.prefix(3).joined(separator: "; ")
        let more = skipped.count > 3 ? " (and \(skipped.count - 3) more)" : ""
        return "Skipped \(skipped.count) file\(skipped.count == 1 ? "" : "s") in the User folder: \(shown)\(more)"
    }

    // MARK: Load

    /// Loads a preset. A Performance replaces both parts; a Patch goes into `target`, or the shown
    /// part when nil (or `.whole`). A preset the plugin refuses leaves the sound and the name as
    /// they were and sets `message`.
    public func load(_ entry: EG13PresetEntry, into target: EG13PresetTarget? = nil) {
        guard let json = text(of: entry) else {
            message = "Could not read \(entry.name)."
            return
        }
        let resolved: EG13PresetTarget
        if entry.kind == .performance {
            resolved = .whole
        } else if case .part(let part)? = target {
            resolved = .part(part)
        } else {
            resolved = .part(shownPart())
        }
        let result = core.applyPreset(json: json, target: resolved)
        guard result.status != .invalid else {
            message = result.message.isEmpty ? "\(entry.name) could not be loaded." : result.message
            return
        }
        message = result.status == .warnings && !result.message.isEmpty ? result.message : nil
        record(Loaded(part: Int(resolved.cPart), bank: entry.bank, name: entry.name, kind: entry.kind))
        isEdited = false
        persist()
    }

    /// Steps through the list in order, across bank boundaries, wrapping from the last to the
    /// first. A Patch loads into the shown part. From Init, or a name no longer in the list, forward
    /// goes to the first preset and back to the last.
    public func step(_ delta: Int) {
        guard !entries.isEmpty else { return }
        let count = entries.count
        let next: Int
        if let current = currentIndex() {
            next = ((current + delta) % count + count) % count
        } else {
            next = delta >= 0 ? 0 : count - 1
        }
        load(entries[next], into: nil)
    }

    /// The list position of what the shown part came from.
    private func currentIndex() -> Int? {
        let shown = shownPart()
        guard let info = loaded.first(where: { $0.part == shown }) ?? loaded.first(where: { $0.part < 0 }) else {
            return nil
        }
        return entries.firstIndex { $0.bank == info.bank && $0.name == info.name && $0.kind == info.kind }
    }

    /// Loads the Init sound into both parts (the plugin builds it) and shows "Init".
    public func loadInit() {
        let result = core.applyInit()
        guard result.status != .invalid else {
            message = result.message.isEmpty ? "Init could not be loaded." : result.message
            return
        }
        message = nil
        loaded = []
        isEdited = false
        persist()
    }

    private func text(of entry: EG13PresetEntry) -> String? {
        switch entry.source {
        case .factory: return entry.json
        case .user(let url): return (try? Data(contentsOf: url)).map { String(decoding: $0, as: UTF8.self) }
        }
    }

    // MARK: Edited mark

    /// An editor edit (a knob or route change by the user) happened. Host value changes seen in
    /// snapshots never call this.
    public func markEdited() {
        guard !isEdited else { return }
        isEdited = true
        persist()
    }

    // MARK: Save

    /// Saves the loaded preset in place if it is a single User preset. A factory preset is read-only,
    /// so this acts as Save As into User under its own name (a clash is reported, as in `saveAs`).
    /// Init, or a name for each part, has nothing single to save: use Save As.
    @discardableResult
    public func save() -> EG13SaveOutcome {
        guard loaded.count == 1, let info = loaded.first else {
            return refuse(loaded.isEmpty ? "Use Save As to name this sound." : "Use Save As to choose what to save.")
        }
        let scope: EG13PresetTarget = info.part < 0 ? .whole : .part(info.part)
        let user = entries.first { !$0.isFactory && $0.bank == info.bank && $0.name == info.name && $0.kind == info.kind }
        if let user, case .user(let url) = user.source {
            return write(name: info.name, scope: scope, to: url)
        }
        return saveAs(name: info.name, scope: scope)
    }

    /// Saves the sound as a User preset: a Performance for `.whole`, a Patch of that part for
    /// `.part`. A preset of that name and kind that exists is reported as `.clash` and kept unless
    /// `replace` is true. An empty name is refused. The file name is `name` made safe (see
    /// `EG13PresetLibrary.fileName(for:)`); the preset keeps `name` as typed (trimmed).
    @discardableResult
    public func saveAs(name: String, scope: EG13PresetTarget, replace: Bool = false) -> EG13SaveOutcome {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return refuse("Enter a name for the preset.") }
        let presetKind: EG13PresetEntry.Kind = scope == .whole ? .performance : .patch
        guard let url = library.url(name: trimmed, kind: presetKind) else {
            return refuse("There is no folder to save presets in.")
        }
        let sameName = entries.contains {
            !$0.isFactory && $0.kind == presetKind && $0.name.caseInsensitiveCompare(trimmed) == .orderedSame
        }
        if !replace, sameName || library.exists(url) { return .clash }
        return write(name: trimmed, scope: scope, to: url)
    }

    private func write(name: String, scope: EG13PresetTarget, to url: URL) -> EG13SaveOutcome {
        var part: Int?
        if case .part(let index) = scope { part = index }
        let json = core.savePreset(part: part, name: name)
        guard !json.isEmpty else { return refuse("The sound could not be saved.") }
        do {
            try library.write(json, to: url)
        } catch {
            return refuse("Could not write \(url.lastPathComponent): \(error.localizedDescription)")
        }
        record(Loaded(part: part ?? -1, bank: "User", name: name, kind: part == nil ? .performance : .patch))
        isEdited = false
        persist()
        reloadUserFolder()
        return .saved
    }

    /// Renames a User preset: the file and its `name`. A clash with another preset's file is
    /// reported and nothing changes unless `replace` is true.
    @discardableResult
    public func rename(_ entry: EG13PresetEntry, to newName: String, replace: Bool = false) -> EG13SaveOutcome {
        guard case .user(let url) = entry.source else { return refuse("Factory presets cannot be renamed.") }
        let trimmed = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return refuse("Enter a name for the preset.") }
        guard let target = library.url(name: trimmed, kind: entry.kind) else { return refuse("There is no folder.") }
        let samePlace = target.standardizedFileURL.path.caseInsensitiveCompare(url.standardizedFileURL.path) == .orderedSame
        if !replace, !samePlace, library.exists(target) { return .clash }
        guard let text = library.renamedText(of: url, to: trimmed) else { return refuse("Could not read \(entry.name).") }
        do {
            if samePlace {
                // Only the case differs: on a case-insensitive file system the two names are one
                // file, so write it in place and move it to the new name (never delete it).
                try library.write(text, to: url)
                if target.lastPathComponent != url.lastPathComponent { try library.move(url, to: target) }
            } else {
                try library.write(text, to: target)
                try library.delete(url)
            }
        } catch {
            return refuse("Could not rename \(entry.name): \(error.localizedDescription)")
        }
        var changed = false
        for index in loaded.indices where loaded[index].bank == "User" && loaded[index].name == entry.name
            && loaded[index].kind == entry.kind {
            loaded[index].name = trimmed
            changed = true
        }
        if changed { persist() }
        reloadUserFolder()
        return .saved
    }

    /// Deletes a User preset's file. The sound stays as it is, and keeps showing the name.
    public func delete(_ entry: EG13PresetEntry) {
        guard case .user(let url) = entry.source else {
            message = "Factory presets cannot be deleted."
            return
        }
        do {
            try library.delete(url)
        } catch {
            message = "Could not delete \(entry.name): \(error.localizedDescription)"
            return
        }
        reloadUserFolder()
    }

    private func refuse(_ reason: String) -> EG13SaveOutcome {
        message = reason
        return .refused(reason)
    }

    // MARK: Browser string

    /// Notes where a part's sound came from. A Patch into one part after a Performance gives both
    /// parts the Performance's name first, then replaces that part's.
    private func record(_ info: Loaded) {
        if info.part < 0 {
            loaded = [info]
            return
        }
        if let whole = loaded.first(where: { $0.part < 0 }) {
            loaded = (0..<EG13ParamID.parts).map { Loaded(part: $0, bank: whole.bank, name: whole.name, kind: whole.kind) }
        }
        loaded.removeAll { $0.part == info.part }
        loaded.append(info)
        loaded.sort { $0.part < $1.part }
    }

    private func persist() {
        let state = Persisted(loaded: loaded, edited: isEdited)
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        guard let data = try? encoder.encode(state) else { return }
        core.setBrowserState(String(decoding: data, as: UTF8.self))
    }

    /// Reads the project's browser string; an empty, unreadable or newer one is Init.
    private func restore(_ text: String) {
        guard let state = try? JSONDecoder().decode(Persisted.self, from: Data(text.utf8)), state.version == 1 else {
            loaded = []
            isEdited = false
            return
        }
        loaded = state.loaded.filter { (-1...1).contains($0.part) }
        isEdited = state.edited
    }
}
