import EG13CBridge

/// Part settings by parameter index within the Part slot. Mirrors core/params/param_ids.h.
public enum EG13PartParam: Int, CaseIterable, Sendable {
    case level = 1, pan, glide, transpose, baseNote, midiChannel, keyLow, keyHigh
    case voiceMode, voiceCount, trigger, bendRange, mono, output
}

/// Master parameters (EG13_MASTER_*).
public enum EG13MasterParam: Int, CaseIterable, Sendable {
    case level = 0, dry, mode, splitNote
}

/// Global mode switch values (spec §5.3).
public enum EG13Mode: Int, CaseIterable, Sendable {
    case single = 0, layer, split, multi

    public var label: String {
        switch self {
        case .single: return "Single"
        case .layer: return "Layer"
        case .split: return "Split"
        case .multi: return "Multi"
        }
    }
}

/// Stable parameter IDs: (part << 16) | (slot << 8) | index (spec §7.1).
public enum EG13ParamID {
    public static let parts = Int(EG13_NUM_PARTS)

    public static func part(_ part: Int, _ param: EG13PartParam) -> UInt32 {
        eg13_param_id(UInt32(part), UInt32(EG13_SLOT_PART.rawValue), UInt32(param.rawValue))
    }

    public static func master(_ param: EG13MasterParam) -> UInt32 {
        eg13_param_id(UInt32(EG13_MASTER_PART), UInt32(EG13_SLOT_PART.rawValue), UInt32(param.rawValue))
    }

    /// p: 1...8 for P1–P8, 0 for the engine selector.
    public static func source(_ part: Int, _ p: Int) -> UInt32 {
        eg13_param_id(UInt32(part), UInt32(EG13_SLOT_SOURCE.rawValue), UInt32(p))
    }

    /// 0 or 1 for a part's parameters, nil for master parameters.
    public static func partIndex(of id: UInt32) -> Int? {
        let part = Int(id >> 16)
        return part < parts ? part : nil
    }

    /// The Source engine slot, for the engine functions of the C API.
    public static var sourceKind: UInt32 { UInt32(EG13_SLOT_KIND_SOURCE.rawValue) }
}
