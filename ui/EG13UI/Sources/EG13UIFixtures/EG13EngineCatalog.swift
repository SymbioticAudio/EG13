import Foundation

/// One engine of a slot, as the core reports it: display name, the names of the P's it uses and
/// the value strings each P shows.
public struct EG13CatalogEngine: Sendable {
    public let name: String
    public let params: [String]
    /// The value strings of each P (EngineParamInfo::format swept over 0...1): all of them for an
    /// enumeration or a step count, one of each of the longest shapes for a number.
    public let values: [[String]]
    public init(_ name: String, _ params: [String], values: [[String]] = []) {
        self.name = name
        self.params = params
        self.values = values
    }
}

/// One P's value strings, for the tests that measure them.
public struct EG13ValueRow: Sendable {
    public let slot: String
    public let engine: String
    public let param: String
    public let texts: [String]
}

/// The plugin's engine tables for the preview and the tests: loaded from core-tables.json, which
/// the core generates (tools/ui_tables, ctest ui_tables_up_to_date keeps it current). Names
/// the core marks "(unused)" stay in the middle of an engine's range, as in the plugin. The plugin
/// reads the live names from the core; this target is never linked into it.
public enum EG13EngineCatalog {
    private struct Table: Decodable {
        struct Param: Decodable { let name: String; let values: [String] }
        struct Engine: Decodable { let name: String; let params: [Param] }
        let slots: [String: [Engine]]
    }

    private static let tables: [String: [EG13CatalogEngine]] = {
        guard let url = Bundle.module.url(forResource: "core-tables", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let table = try? JSONDecoder().decode(Table.self, from: data) else {
            preconditionFailure("EG13UIFixtures: core-tables.json is missing or malformed")
        }
        return table.slots.mapValues { engines in
            engines.map { EG13CatalogEngine($0.name, $0.params.map(\.name), values: $0.params.map(\.values)) }
        }
    }()

    public static let source = tables["source"] ?? []
    public static let filter = tables["filter"] ?? []
    public static let shaper = tables["shaper"] ?? []
    /// The four mod slots share one engine table.
    public static let mod = tables["mod"] ?? []
    public static let partFx = tables["partFx"] ?? []

    /// The engines of each slot kind (EG13SlotKind raw order: Source, Filter, Shaper, Mod 1-4, Part FX).
    public static func engines(kind: Int) -> [EG13CatalogEngine] {
        switch kind {
        case 0: return source
        case 1: return filter
        case 2: return shaper
        case 7: return partFx
        default: return mod
        }
    }

    /// Every P that shows values, by slot group ("source", "filter", "shaper", "mod", "partFx").
    public static let valueRows: [EG13ValueRow] = {
        var rows: [EG13ValueRow] = []
        for group in ["source", "filter", "shaper", "mod", "partFx"] {
            for engine in tables[group] ?? [] {
                for (index, name) in engine.params.enumerated() where name != "(unused)" {
                    rows.append(EG13ValueRow(slot: group, engine: engine.name, param: name,
                                             texts: engine.values[index]))
                }
            }
        }
        return rows
    }()
}
