/// Which knobs stand out on an engine's card (owner, 2026-10-20): a plain white cap (the
/// default), an accent-filled cap with a teal or pink label chip, or a small black cap.
/// UI metadata keyed by engine id (EngineInfo.id) and P index (0 = P1); not part of the core.
public struct EG13ParamEmphasis: Sendable, Equatable {
    public enum Cap: Sendable, Equatable { case plain, accent(EG13Palette.AccentRole), black }
    public var cap: Cap

    public static let plain = EG13ParamEmphasis(cap: .plain)
    public static let teal = EG13ParamEmphasis(cap: .accent(.teal))
    public static let pink = EG13ParamEmphasis(cap: .accent(.pink))
    public static let black = EG13ParamEmphasis(cap: .black)
}

public enum EG13Emphasis {
    static let table: [String: [Int: EG13ParamEmphasis]] = [
        // Macro Oscillator: Model, Harmonics, Timbre, Morph, Frequency, Decay, LPG Colour, Aux Mix
        "plaits": [2: .teal, 3: .pink, 5: .black, 6: .black, 7: .black],
        // Digital Oscillator: Shape, Timbre, Color, Frequency, Signature, Bit Depth, Sample Rate
        "braids": [1: .teal, 2: .pink, 4: .black, 5: .black, 6: .black],
        // Modal Synth: Model, Bow, Blow, Strike, Geometry, Brightness, Damping, Position
        "elements": [2: .pink, 3: .teal, 6: .black, 7: .black],
        // Resonator: Model, Structure, Brightness, Damping, Position, Exciter, Frequency, Spread
        "rings": [1: .teal, 2: .pink, 5: .black, 6: .black, 7: .black],
        // Audio Input: Gain, Attack, Release
        "input": [1: .black, 2: .black],
    ]

    public static func emphasis(engineID: String, paramIndex: Int) -> EG13ParamEmphasis {
        table[engineID]?[paramIndex] ?? .plain
    }
}
