import EG13CBridge

/// Part settings by parameter index within the Part slot. Mirrors core/params/param_ids.h.
public enum EG13PartParam: Int, CaseIterable, Sendable {
    case level = 1, pan, glide, transpose, baseNote, midiChannel, keyLow, keyHigh
    case voiceMode, voiceCount, trigger, bendRange, mono, output
    case inputSource, mpeMode, mpeBendRange, ccA, ccB, ccC, ccD, partFxInput
}

/// Master parameters (EG13_MASTER_*).
public enum EG13MasterParam: Int, CaseIterable, Sendable {
    case level = 0, dry, mode, splitNote, voiceGain, softClip, latencyMode
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

    /// The parameter-ID slot (EG13Slot) of an engine slot (EG13SlotKind): table order differs.
    public static func paramSlot(of kind: EG13SlotKind) -> UInt32 {
        switch kind {
        case EG13_SLOT_KIND_SOURCE: return UInt32(EG13_SLOT_SOURCE.rawValue)
        case EG13_SLOT_KIND_FILTER: return UInt32(EG13_SLOT_FILTER.rawValue)
        case EG13_SLOT_KIND_SHAPER: return UInt32(EG13_SLOT_SHAPER.rawValue)
        case EG13_SLOT_KIND_PART_FX: return UInt32(EG13_SLOT_PART_FX.rawValue)
        case EG13_SLOT_KIND_MOD1: return UInt32(EG13_SLOT_MOD1.rawValue)
        case EG13_SLOT_KIND_MOD2: return UInt32(EG13_SLOT_MOD2.rawValue)
        case EG13_SLOT_KIND_MOD3: return UInt32(EG13_SLOT_MOD3.rawValue)
        default: return UInt32(EG13_SLOT_MOD4.rawValue)
        }
    }

    /// Parameters per engine slot (P1...): Source 16, Filter 4, Shaper 12, Part FX 16, mod 16.
    public static func slotParamCount(_ kind: EG13SlotKind) -> Int {
        switch kind {
        case EG13_SLOT_KIND_SOURCE: return 16
        case EG13_SLOT_KIND_FILTER: return 4
        case EG13_SLOT_KIND_SHAPER: return 12
        default: return 16
        }
    }

    public static let routeCount = 12

    /// Whether the slot is one of the four mod slots.
    public static func isModSlot(_ kind: EG13SlotKind) -> Bool {
        kind == EG13_SLOT_KIND_MOD1 || kind == EG13_SLOT_KIND_MOD2
            || kind == EG13_SLOT_KIND_MOD3 || kind == EG13_SLOT_KIND_MOD4
    }

    /// A mod slot's Scope, the entry after its P's.
    public static func modScope(_ part: Int, _ kind: EG13SlotKind) -> UInt32 {
        slot(part, kind, slotParamCount(kind) + 1)
    }

    /// Route amount `route` (0...11) of a part's matrix.
    public static func routeAmount(_ part: Int, _ route: Int) -> UInt32 {
        eg13_param_id(UInt32(part), UInt32(EG13_SLOT_MATRIX.rawValue), UInt32(route + 1))
    }

    /// A slot's parameter: p 0 is the engine selector, 1... are P1...
    public static func slot(_ part: Int, _ kind: EG13SlotKind, _ p: Int) -> UInt32 {
        eg13_param_id(UInt32(part), paramSlot(of: kind), UInt32(p))
    }
}
