/// Matrix names like the core's (plugin/src/core_api.cpp), for the preview's and the tests' fake
/// cores: a code is named only when the part's current engines have it. Not used by the plugin.
public enum EG13MatrixNames {
    public static let fixedSources: [UInt16: String] = [
        1: "Velocity", 2: "Release Velocity", 3: "Note", 4: "Gate", 5: "Random", 6: "Pressure",
        7: "Slide", 8: "Note Bend", 9: "Mod Wheel", 10: "Pitch Bend", 11: "Channel Pressure",
        12: "CC A", 13: "CC B", 14: "CC C", 15: "CC D", 16: "Transport Phase",
    ]
    public static let fixedDestinations: [UInt16: String] = [1: "Pitch", 2: "Amp", 3: "Pan", 4: "Part Level"]

    /// How many outputs a mod engine has (the preview's guess; the core knows).
    public static func outputCount(engine: String) -> Int {
        switch engine {
        case "Off": return 0
        case "LFO", "Function Generator": return 2
        case "Random / S&H": return 4
        default: return 1
        }
    }

    /// modEngines: the engine name selected in each of the four mod slots.
    public static func sourceName(code: UInt16, modEngines: [String]) -> String? {
        if let name = fixedSources[code] { return name }
        guard code >= 32, code < 64 else { return nil }
        let slot = Int(code - 32) / 8, output = Int(code - 32) % 8
        guard slot < modEngines.count, output < outputCount(engine: modEngines[slot]) else { return nil }
        let engine = modEngines[slot]
        return outputCount(engine: engine) == 1 ? "Mod \(slot + 1) \(engine)" : "Mod \(slot + 1) \(engine) Out \(output + 1)"
    }

    /// paramNames(kind): the P names of the engine selected in slot kind `kind` (EG13SlotKind
    /// order: 0 Source, 1 Filter, 2 Shaper, 3...6 Mod 1-4, 7 Part FX); only the P's it uses.
    public static func destinationName(code: UInt16, paramNames: (Int) -> [String]) -> String? {
        if let name = fixedDestinations[code] { return name }
        let groups: [(base: UInt16, kind: Int, prefix: String)] = [
            (16, 0, "Source"), (32, 1, "Filter"), (48, 2, "Shaper"), (64, 7, "Part FX"),
            (80, 3, "Mod 1"), (96, 4, "Mod 2"), (112, 5, "Mod 3"), (128, 6, "Mod 4"),
        ]
        for group in groups where code >= group.base && code < group.base + 16 {
            let names = paramNames(group.kind)
            let p = Int(code - group.base)
            return p < names.count ? "\(group.prefix): \(names[p])" : nil
        }
        return nil
    }
}
