import EG13CBridge

/// Which knobs stand out (owner, 2026-10-20; round 3 control choices): a white cap by
/// default, an accent cap with an accent chip for emphasised knobs, a black cap with the
/// part-colour arc for secondary ones.
public struct EG13ParamEmphasis: Sendable, Equatable {
    public enum Cap: Sendable, Equatable { case plain, accent(EG13Palette.AccentRole), black }
    public var cap: Cap

    public static let plain = EG13ParamEmphasis(cap: .plain)
    public static let teal = EG13ParamEmphasis(cap: .accent(.teal))
    public static let pink = EG13ParamEmphasis(cap: .accent(.pink))
    public static let black = EG13ParamEmphasis(cap: .black)
}

/// Layout round 1 proposal (an open question for the owner, design log): an engine's main
/// controls are its first two P's, teal then pink, in every Source, Filter, Shaper and Part FX
/// engine. Mod knobs stay plain (their cards are compact). Keyed on the slot and the engine
/// index, so an engine can later get its own choice without touching the views.
public enum EG13Emphasis {
    /// `p` is 1 for P1.
    public static func slotParam(kind: EG13SlotKind, engine: Int, p: Int) -> EG13ParamEmphasis {
        guard !EG13ParamID.isModSlot(kind) else { return .plain }
        switch p {
        case 1: return .teal
        case 2: return .pink
        default: return .plain
        }
    }
}
