import EG13CBridge

/// One engine slot of one part, as the cards show it. Rebuilt when the plugin's engine
/// generation changes; never on a plain value update.
public struct EG13Slot: Equatable {
    public var kind: EG13SlotKind
    public var part: Int
    /// The engine selected in the slot. Views key engine emphasis on it.
    public var engine: Int
    /// Every engine the slot can host, in selector order. Empty when the slot has none yet.
    public var engineNames: [String]
    /// The slot's visible P's: only those the selected engine uses (P1 first).
    public var descriptors: [EG13ParamDescriptor]

    public var engineName: String { engineNames.indices.contains(engine) ? engineNames[engine] : "" }
    /// The selector's parameter ID.
    public var selectorID: UInt32 { EG13ParamID.slot(part, kind, 0) }
    public var visibleParamCount: Int { descriptors.count }

    public static func empty(_ kind: EG13SlotKind, part: Int) -> EG13Slot {
        EG13Slot(kind: kind, part: part, engine: 0, engineNames: [], descriptors: [])
    }
}
