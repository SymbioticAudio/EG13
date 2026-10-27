import Foundation
import os
import Observation

/// The app-wide settings (spec §8, M6b decision 9). They belong to the user's machine, not to a
/// project, so they never go into plugin state.
public struct EG13Settings: Equatable, Sendable {
    /// The window sizes a new editor can open at, in percent.
    public static let windowPercents = [75, 100, 125, 150, 200]
    public static let schemaVersion = 1

    /// The size new editors open at. Open editors keep theirs.
    public var defaultWindowPercent = 100
    /// Off hides the wave marks, the MOTION buttons and the traces, and stops the 15 Hz refresh.
    public var modulationDisplay = true

    public init() {}

    public var defaultZoom: Double { Double(defaultWindowPercent) / 100 }
}

extension EG13Settings: Codable {
    private enum Key: String, CodingKey { case schemaVersion, defaultWindowPercent, modulationDisplay }

    /// Throws for a wrong type, an unknown size or a file from a newer version; unknown keys are
    /// skipped and a missing key keeps its default.
    public init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: Key.self)
        let version = try container.decodeIfPresent(Int.self, forKey: .schemaVersion) ?? Self.schemaVersion
        guard version <= Self.schemaVersion else {
            throw DecodingError.dataCorruptedError(forKey: .schemaVersion, in: container,
                                                   debugDescription: "newer settings file")
        }
        self.init()
        if let percent = try container.decodeIfPresent(Int.self, forKey: .defaultWindowPercent) {
            guard Self.windowPercents.contains(percent) else {
                throw DecodingError.dataCorruptedError(forKey: .defaultWindowPercent, in: container,
                                                       debugDescription: "unknown window size")
            }
            defaultWindowPercent = percent
        }
        modulationDisplay = try container.decodeIfPresent(Bool.self, forKey: .modulationDisplay) ?? true
    }

    public func encode(to encoder: Encoder) throws {
        var container = encoder.container(keyedBy: Key.self)
        try container.encode(Self.schemaVersion, forKey: .schemaVersion)
        try container.encode(defaultWindowPercent, forKey: .defaultWindowPercent)
        try container.encode(modulationDisplay, forKey: .modulationDisplay)
    }
}

/// Path overrides from the environment for tests and the preview app. They exist only in debug
/// builds: a release plugin never reads the environment of the host it runs in, and the
/// variable names are not in the binary (scripts/build-ui.sh checks that).
enum EG13DebugOverride {
    static func path(_ name: String) -> String? {
        #if DEBUG
        if let path = ProcessInfo.processInfo.environment[name], !path.isEmpty { return path }
        #endif
        return nil
    }
}

/// A settings file set from code, not from the environment, so a release plugin can be tested
/// without reading the developer's real file. Set only through `eg13_ui_set_settings_url_for_testing`.
enum EG13SettingsLocation {
    nonisolated(unsafe) static var testingURL: URL?
}

/// Reads and writes the settings file. A missing, corrupt or newer file loads as defaults. The
/// file is written only when the user changes a setting, by writing a temp file and renaming it
/// over the old one, so a crash never leaves half a file.
@MainActor
@Observable
public final class EG13SettingsStore {
    public private(set) var settings = EG13Settings()
    /// Set when the last change could not be written (the Settings overlay shows it); cleared by
    /// the next write that works. The change itself stays for this session.
    public private(set) var saveError: String?
    @ObservationIgnored private let fixedURL: URL?
    /// The shared store: its file is `defaultURL` as of each read or write, so a test that points
    /// `EG13_SETTINGS_PATH` elsewhere mid-process is followed.
    @ObservationIgnored private let followsEnvironment: Bool
    private var url: URL? { followsEnvironment ? Self.defaultURL : fixedURL }

    /// The file the plugin uses: `$EG13_SETTINGS_PATH` if set (debug builds only: tests), else
    /// ~/Library/Application Support/SymbioticAudio/EG13/settings.json.
    public static var defaultURL: URL {
        if let url = EG13SettingsLocation.testingURL { return url }
        if let path = EG13DebugOverride.path("EG13_SETTINGS_PATH") { return URL(fileURLWithPath: path) }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Application Support")
        return support.appendingPathComponent("SymbioticAudio/EG13/settings.json")
    }

    /// Shared by every editor in the process, so a change in one shows in the next one opened.
    public static let shared = EG13SettingsStore(followingEnvironment: ())
    private static let log = Logger(subsystem: "com.symbioticaudio.eg13", category: "settings")

    /// url nil: nothing is read or written (tests and previews).
    public init(url: URL?) {
        fixedURL = url
        followsEnvironment = false
        reload()
    }

    private init(followingEnvironment: Void) {
        fixedURL = nil
        followsEnvironment = true
        reload()
    }

    /// Re-reads the file (another instance or process may have changed it).
    public func reload() {
        var loaded = EG13Settings()
        if let url, let data = try? Data(contentsOf: url),
           let decoded = try? JSONDecoder().decode(EG13Settings.self, from: data) {
            loaded = decoded
        }
        if loaded != settings { settings = loaded }
    }

    /// Applies a change. Nothing happens, and nothing is written, if it changes no value. A failed
    /// write keeps the new value for this session and leaves the old file as it was.
    public func update(_ change: (inout EG13Settings) -> Void) {
        var next = settings
        change(&next)
        guard next != settings else { return }
        settings = next
        save()
    }

    /// The zoom a new editor opens at: the plugin's request, or the default when it passes 0.
    public func initialZoom(requested: Double) -> Double {
        requested > 0 ? requested : settings.defaultZoom
    }

    private func save() {
        guard let url else { return }
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        guard let data = try? encoder.encode(settings) else { return }
        let directory = url.deletingLastPathComponent()
        let temp = directory.appendingPathComponent(".\(url.lastPathComponent).\(UUID().uuidString).tmp")
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try data.write(to: temp)
            // On disk before the rename, so a crash or power loss cannot leave an empty file.
            let handle = try FileHandle(forWritingTo: temp)
            defer { try? handle.close() }
            if fsync(handle.fileDescriptor) != 0 { throw CocoaError(.fileWriteUnknown) }
            if rename(temp.path, url.path) != 0 { throw CocoaError(.fileWriteUnknown) }
            saveError = nil
        } catch {
            try? FileManager.default.removeItem(at: temp)
            saveError = "Could not save settings. They apply to this session only."
            Self.log.error("could not write the settings file \(url.path, privacy: .public): \(String(describing: error), privacy: .public)")
        }
    }
}
