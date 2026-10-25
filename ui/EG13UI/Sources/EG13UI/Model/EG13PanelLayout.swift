import EG13CBridge

/// Which knobs stand out: a white cap by default, an accent cap with an accent chip for
/// emphasised knobs, a black cap with the part-colour arc for secondary ones. The accent is
/// always the shown part's accent (teal for Part A, pink for Part B); emphasis never picks
/// a colour of its own.
public struct EG13ParamEmphasis: Sendable, Equatable {
    public enum Cap: Sendable, Equatable { case plain, accent, black }
    public var cap: Cap

    public static let plain = EG13ParamEmphasis(cap: .plain)
    public static let accent = EG13ParamEmphasis(cap: .accent)
    public static let black = EG13ParamEmphasis(cap: .black)
}

/// The size and the emphasis of every engine knob, taken from the real module panels
/// (owner, 2026-10-25: three knob sizes following the panels; emphasis where the panel has
/// a large knob). Data only: the views never name an engine.
///
/// Reading the tables: one entry per P (P1 first), `L`, `M` or `S` for the knob size and a
/// `*` for emphasised. A P past the end of the list is a medium plain knob. The size comes
/// from the panel's knob component: a large knob (Rogan 3 and 6) is large, a Rogan 1 or 2 is
/// medium, a trimmer (or a button) is small. Where the owner pointed at a panel (Liquid
/// Filter, Random Sampler) the panel as drawn decides.
///
/// Modules (VCV Rack Audible Instruments replicas of the real panels):
///   Macro Oscillator = Plaits, Digital Oscillator = Braids, Modal Synth = Elements,
///   Resonator (Source and Part FX) = Rings, Analog Filter = Ripples (Liquid Filter),
///   Ring Mod / Vocoder = Warps, EQ = Shelves, LFO = Tides 2, Random / S&H = Marbles
///   (Random Sampler), Granular = Clouds, Reverb = Elements' reverb.
/// Fallbacks (no panel to read, or a panel with no size differences), see the design log:
///   Audio Input, Classic Filters, State Variable Filter, Envelope (Stages: sliders), Function
///   Generator (Peaks: four equal pots), Granular Delay (Beads is not among the replicas) and
///   Reverb (the knob on Elements is one trimmer-sized Space knob).
/// A mod slot has no large knob: its sizes drop one step (large is medium, medium is small).
public enum EG13PanelLayout {
    public struct Knob: Equatable, Sendable {
        public var size: EG13KnobSize
        public var emphasised: Bool
    }

    private static func parse(_ text: String) -> [Knob] {
        var knobs: [Knob] = []
        for ch in text where ch != " " {
            switch ch {
            case "L": knobs.append(Knob(size: .large, emphasised: false))
            case "M": knobs.append(Knob(size: .medium, emphasised: false))
            case "S": knobs.append(Knob(size: .small, emphasised: false))
            case "*": if !knobs.isEmpty { knobs[knobs.count - 1].emphasised = true }
            default: break
            }
        }
        return knobs
    }

    /// Slot kind (0 Source, 1 Filter, 2 Shaper, 3 Mod, 7 Part FX) and engine display name.
    /// Each line: the P's in order.
    private static let table: [String: String] = [
        // Source
        "0/Macro Oscillator": "S L* M M L* M M M",             // Plaits: Harmonics, Frequency large
        "0/Digital Oscillator": "M M* M* M M M M",             // Braids: all one size; the coloured Timbre, Color
        "0/Modal Synth": "S M M M M L* L* M M M L* L* M M M M", // Elements: Flow, Mallet, Geometry, Brightness
        "0/Resonator": "S L* M M M M L* M S",                  // Rings: Structure, Frequency
        "0/Audio Input": "L* M M",                             // fallback: Gain
        // Filter
        "1/Analog Filter": "L* S* S",                          // Ripples: Freq large, Res small and coloured
        "1/Classic Filters": "L* S* M M",                      // fallback, Ripples' pattern
        "1/State Variable Filter": "L* S* S",                  // fallback, Ripples' pattern
        // Shaper
        "2/Ring Mod / Vocoder": "L* M S S M M M M S",          // Warps: Algorithm large, Timbre medium
        "2/EQ": "M M M* M* S M* M* S M M M M",                 // Shelves: no large knob; the coloured pairs
        // Mod (the sizes drop one step when read)
        "3/Envelope": "S L* S L* S L* S L* S M M M M",         // fallback: Stages' sliders A D S R
        "3/LFO": "S S L* L* M M M S S S S",                    // Tides 2: Frequency, Shape large
        "3/Random / S&H": "S S L* M M S L* M M S M M S M S S", // Marbles: T Rate, X Spread large
        "3/Function Generator": "S L* L* M M M M M M S S",     // fallback: Peaks, Attack and Rate
        // Part FX
        "7/Granular": "M L* L* L* M M M M S S S M M",          // Clouds: Position, Size, Pitch large
        "7/Granular Delay": "M L* L* M M M M M S S S M M M M M", // fallback: Time, Size
        "7/Resonator": "M L* M M M M M M M S S M L*",          // Rings: Structure, Tune (Frequency)
        "7/Reverb": "L* L* M M M M M M S",                     // fallback: Amount, Time
    ]

    private static let parsed: [String: [Knob]] = table.mapValues(parse)

    /// True when the engine has an entry (tests check every engine has one).
    public static func hasEntry(kind: EG13SlotKind, engine: String) -> Bool {
        parsed[key(kind, engine)] != nil
    }

    /// How many P's the engine's entry covers.
    public static func entryCount(kind: EG13SlotKind, engine: String) -> Int {
        parsed[key(kind, engine)]?.count ?? 0
    }

    private static func key(_ kind: EG13SlotKind, _ engine: String) -> String {
        let group = EG13ParamID.isModSlot(kind) ? 3 : Int(kind.rawValue)
        return "\(group)/\(engine)"
    }

    /// The knob of P `p` (1 for P1). An engine with no entry gets medium knobs with P1 and
    /// P2 large and emphasised.
    public static func knob(kind: EG13SlotKind, engine: String, p: Int) -> Knob {
        var knob: Knob
        if let list = parsed[key(kind, engine)] {
            knob = p >= 1 && p <= list.count ? list[p - 1] : Knob(size: .medium, emphasised: false)
        } else {
            knob = Knob(size: p <= 2 ? .large : .medium, emphasised: p <= 2)
        }
        if EG13ParamID.isModSlot(kind) {
            switch knob.size {
            case .large: knob.size = .medium
            case .medium, .small: knob.size = .small
            }
        }
        return knob
    }
}
