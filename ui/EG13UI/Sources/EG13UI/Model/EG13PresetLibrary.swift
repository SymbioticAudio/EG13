import Foundation

/// One preset in the browser's list: a factory preset of the plugin or a file in the User folder.
public struct EG13PresetEntry: Identifiable, Hashable, Sendable {
    public enum Kind: String, Codable, Sendable { case performance, patch }
    public enum Source: Hashable, Sendable { case factory(Int), user(URL) }

    public var bank: String
    public var name: String
    public var kind: Kind
    public var note: String
    public var credit: String
    public var licence: String
    public var source: Source
    /// The preset file's text for a factory preset (the plugin owns it); empty for a User file,
    /// which is read when it loads.
    public var json: String

    public init(bank: String, name: String, kind: Kind, note: String = "", credit: String = "",
                licence: String = "", source: Source, json: String = "") {
        self.bank = bank
        self.name = name
        self.kind = kind
        self.note = note
        self.credit = credit
        self.licence = licence
        self.source = source
        self.json = json
    }

    public var id: String {
        switch source {
        case .factory(let index): "factory:\(index)"
        case .user(let url): "user:\(url.path)"
        }
    }

    public var isFactory: Bool {
        if case .factory = source { return true }
        return false
    }

    /// The credit EG13's own factory presets carry; they show no attribution.
    static let ownCredit = "EG13 factory"

    /// "credit — licence" for a preset made from someone else's work (Bank 2, rebuilt from a CC BY
    /// 4.0 patch), or the credit alone when there is no licence. The menu row shows it as help and
    /// loading the preset shows it as the message. nil for EG13's own presets and for presets with
    /// no credit (Save writes only the name).
    public var attribution: String? {
        let who = credit.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !who.isEmpty, who != Self.ownCredit else { return nil }
        let terms = licence.trimmingCharacters(in: .whitespacesAndNewlines)
        return terms.isEmpty ? who : "\(who) \u{2014} \(terms)"
    }
}

/// Where a preset goes: a Performance replaces both parts, a Patch goes into one part.
public enum EG13PresetTarget: Hashable, Sendable {
    case whole
    case part(Int)

    /// The C API's target_part: -1, 0 or 1.
    var cPart: Int32 {
        switch self {
        case .whole: -1
        case .part(let part): Int32(part)
        }
    }
}

/// What applying a preset did (the C API's EG13_PRESET_ results).
public struct EG13PresetApplyResult: Equatable, Sendable {
    public enum Status: Sendable { case ok, warnings, invalid }
    public var status: Status
    /// Why the preset was refused, or what was left out; empty when there is nothing to say.
    public var message: String

    public init(status: Status, message: String = "") {
        self.status = status
        self.message = message
    }
}

/// What Save, Save As and Rename did.
public enum EG13SaveOutcome: Equatable, Sendable {
    case saved
    /// A preset of that name and kind exists; ask, then repeat with `replace: true`.
    case clash
    case refused(String)
}

/// The User preset folder: scanning, file names and writes. Pure file work, no plugin calls.
struct EG13PresetLibrary: Sendable {
    static let performanceExtension = "eg13perf"
    static let patchExtension = "eg13patch"
    static let formatVersion = 1

    /// nil: no folder (tests and previews); nothing is read or written.
    let folder: URL?

    struct Scan {
        var entries: [EG13PresetEntry] = []
        /// "name.eg13patch: why", one per file left out.
        var skipped: [String] = []
    }

    static func fileExtension(for kind: EG13PresetEntry.Kind) -> String {
        kind == .performance ? performanceExtension : patchExtension
    }

    /// A safe file name for a preset name: the characters that separate paths or break names on
    /// some file systems (/ \ : * ? " < > | and control characters) become "-", surrounding
    /// spaces and leading dots go, and it is capped at 80 characters. Emoji and other text are
    /// kept. The preset's own name stays in the file's `name` field.
    static func fileName(for name: String) -> String {
        let bad: Set<Character> = ["/", "\\", ":", "*", "?", "\"", "<", ">", "|"]
        var text = String(name.map { character -> Character in
            let control = character.unicodeScalars.contains { $0.properties.generalCategory == .control }
            return bad.contains(character) || control ? "-" : character
        })
        text = String(text.prefix(80)).trimmingCharacters(in: .whitespaces)
        while text.hasPrefix(".") { text.removeFirst() }
        text = text.trimmingCharacters(in: .whitespaces)
        return text.isEmpty ? "Untitled" : text
    }

    func url(name: String, kind: EG13PresetEntry.Kind) -> URL? {
        folder?.appendingPathComponent(Self.fileName(for: name))
            .appendingPathExtension(Self.fileExtension(for: kind))
    }

    /// The preset files of this plugin kind ("instrument" or "effect"), sorted by name without
    /// regard to case. A file that is not a preset, is from a newer format, or is for the other
    /// plugin is skipped and named in `skipped`; the rest still list.
    func scan(plugin: String) -> Scan {
        var scan = Scan()
        guard let folder,
              let urls = try? FileManager.default.contentsOfDirectory(
                at: folder, includingPropertiesForKeys: nil, options: [.skipsHiddenFiles]) else { return scan }
        for url in urls.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
            let ext = url.pathExtension.lowercased()
            guard ext == Self.performanceExtension || ext == Self.patchExtension else { continue }
            switch Self.read(url, plugin: plugin) {
            case .success(let entry): scan.entries.append(entry)
            case .failure(let reason): scan.skipped.append("\(url.lastPathComponent): \(reason.text)")
            }
        }
        scan.entries.sort {
            let order = $0.name.caseInsensitiveCompare($1.name)
            return order == .orderedSame ? $0.id < $1.id : order == .orderedAscending
        }
        return scan
    }

    private struct Unreadable: Error { var text: String }

    private static func read(_ url: URL, plugin: String) -> Result<EG13PresetEntry, Unreadable> {
        guard let data = try? Data(contentsOf: url) else { return .failure(Unreadable(text: "could not be read")) }
        guard let root = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let format = root["eg13Preset"] as? Int, format >= 1 else {
            return .failure(Unreadable(text: "not an EG13 preset"))
        }
        guard format <= formatVersion else { return .failure(Unreadable(text: "from a newer version of EG13")) }
        guard let kindText = root["kind"] as? String, let kind = EG13PresetEntry.Kind(rawValue: kindText),
              let filePlugin = root["plugin"] as? String, filePlugin == "instrument" || filePlugin == "effect",
              let state = root["state"] as? [String: Any], state["params"] is [String: Any] else {
            return .failure(Unreadable(text: "not an EG13 preset"))
        }
        guard filePlugin == plugin else {
            return .failure(Unreadable(text: filePlugin == "effect" ? "is for EG13 FX" : "is for the EG13 instrument"))
        }
        let name = (root["name"] as? String).flatMap { $0.isEmpty ? nil : $0 }
            ?? url.deletingPathExtension().lastPathComponent
        return .success(EG13PresetEntry(bank: "User", name: name, kind: kind, note: root["note"] as? String ?? "",
                                        credit: root["credit"] as? String ?? "", licence: root["licence"] as? String ?? "",
                                        source: .user(url)))
    }

    func exists(_ url: URL) -> Bool { FileManager.default.fileExists(atPath: url.path) }

    /// Writes the file whole or not at all (a temp file renamed over the target).
    func write(_ text: String, to url: URL) throws {
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try Data(text.utf8).write(to: url, options: .atomic)
    }

    func delete(_ url: URL) throws { try FileManager.default.removeItem(at: url) }

    /// Renames a file whose new name may differ only in case: through a temporary name, so it works
    /// on case-insensitive and case-sensitive file systems alike.
    func move(_ url: URL, to target: URL) throws {
        let temp = url.deletingLastPathComponent().appendingPathComponent(".\(UUID().uuidString).tmp")
        try FileManager.default.moveItem(at: url, to: temp)
        do {
            try FileManager.default.moveItem(at: temp, to: target)
        } catch {
            try? FileManager.default.moveItem(at: temp, to: url)
            throw error
        }
    }

    /// The file's text with its `name` field changed.
    func renamedText(of url: URL, to name: String) -> String? {
        guard let data = try? Data(contentsOf: url),
              var root = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else { return nil }
        root["name"] = name
        guard let out = try? JSONSerialization.data(withJSONObject: root, options: [.sortedKeys, .prettyPrinted]) else {
            return nil
        }
        return String(decoding: out, as: UTF8.self)
    }
}
