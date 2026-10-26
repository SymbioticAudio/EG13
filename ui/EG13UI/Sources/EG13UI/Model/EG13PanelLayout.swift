import EG13CBridge

/// A knob's cap colour, as on the real module panels (owner, 2026-10-25): white, pink or teal,
/// the same in Part A and Part B (a part is told apart by the A/B tabs, the bottom-bar badge and
/// the meters, not by knob colour). A pink or teal knob has a chip in its colour and an arc
/// that matches its cap; a white knob has a plain label. A black cap (master Dry and Out, the
/// matrix route amounts) is secondary. This supersedes the round 3 controls rule "emphasised =
/// accent cap in the part's accent".
public struct EG13ParamEmphasis: Sendable, Equatable {
    public enum Cap: Sendable, Equatable { case plain, pink, teal, black }
    public var cap: Cap

    public static let plain = EG13ParamEmphasis(cap: .plain)
    public static let pink = EG13ParamEmphasis(cap: .pink)
    public static let teal = EG13ParamEmphasis(cap: .teal)
    public static let black = EG13ParamEmphasis(cap: .black)
}

/// The size and the cap colour of every engine knob, taken from the real module panels
/// (owner, 2026-10-25: three knob sizes and the panel's white, pink and teal caps). Data only: the views never name an engine.
///
/// Reading the tables: one entry per P (P1 first): `L`, `M` or `S` for the knob size, then `p`
/// for a pink cap or `t` for a teal one (none: white). A P past the end of the list is a medium
/// white knob. The size comes from the panel's knob component: a large knob (Rogan 3 and 6) is
/// large, a Rogan 1 or 2 is medium, a trimmer (or a button) is small. The colour comes from the
/// component too: the Red variant is pink, the Green variant teal, White and Gray white. The
/// Blue variant (Plaits' LPG Colour and Decay, Clouds' Reverb) has no EG13 colour: it is teal.
/// Where the owner pointed at a panel (Liquid Filter, Random Sampler) the panel as drawn decides.
///
/// Modules (VCV Rack Audible Instruments replicas of the real panels):
///   Macro Oscillator = Plaits, Digital Oscillator = Braids, Modal Synth = Elements,
///   Resonator (Source and Part FX) = Rings, Analog Filter = Ripples (Liquid Filter),
///   Ring Mod / Vocoder = Warps, EQ = Shelves, LFO = Tides 2, Random / S&H = Marbles
///   (Random Sampler), Granular = Clouds.
/// Fallbacks (no panel to read): white caps with the main one or two controls large. They are
/// Audio Input, Classic Filters, State Variable Filter, Envelope (Stages has sliders), Function
/// Generator (Peaks: four equal pots), Granular Delay (Beads is not among the replicas) and
/// Reverb. A pink or teal pair is added only where it is obviously analogous to a real panel:
/// Classic Filters and State Variable Filter take Liquid Filter's pink Resonance, and Granular
/// Delay takes Clouds' pink Position (Time) and teal Size.
/// A mod slot has no large knob: its sizes drop one step (large is medium, medium is small).
public enum EG13PanelLayout {
    public struct Knob: Equatable, Sendable {
        public var size: EG13KnobSize
        public var emphasis: EG13ParamEmphasis
    }

    private static func parse(_ text: String) -> [Knob] {
        var knobs: [Knob] = []
        for ch in text where ch != " " {
            switch ch {
            case "L": knobs.append(Knob(size: .large, emphasis: .plain))
            case "M": knobs.append(Knob(size: .medium, emphasis: .plain))
            case "S": knobs.append(Knob(size: .small, emphasis: .plain))
            case "p": if !knobs.isEmpty { knobs[knobs.count - 1].emphasis = .pink }
            case "t": if !knobs.isEmpty { knobs[knobs.count - 1].emphasis = .teal }
            default: break
            }
        }
        return knobs
    }

    /// Slot kind (0 Source, 1 Filter, 2 Shaper, 3 Mod, 7 Part FX) and engine display name.
    /// Each line: the P's in order.
    private static let table: [String: String] = [
        // Source
        "0/Macro Oscillator": "S L M M L Mt Mt M",             // Plaits; the blue LPG knobs are teal
        "0/Digital Oscillator": "M Mt Mp M M M M",             // Braids: all one size; green Timbre, red Color
        "0/Modal Synth": "S M M Mp Mt Lp Lt M Mp Mt L L M M M M", // Elements: Blow, Flow, Blow Timbre pink; Strike, Mallet, Strike Timbre teal
        "0/Resonator": "S L M M M M L M S",                    // Rings: all white
        "0/Audio Input": "L M M",                              // fallback
        // Filter
        "1/Analog Filter": "L Sp S",                           // Ripples: Freq large; Res small, pink
        "1/Classic Filters": "L Sp M M",                       // fallback, Liquid Filter's pink Res
        "1/State Variable Filter": "L Sp S",                   // fallback, Liquid Filter's pink Res
        // Shaper
        "2/Ring Mod / Vocoder": "L M S S M M M M S",           // Warps: all white
        "2/EQ": "M M Mp Mp S Mt Mt S M M M M",                 // Shelves: Mid 1 pink, Mid 2 teal
        // Mod (the sizes drop one step when read); no mapping: white
        "3/Envelope": "S L S L S L S L S M M M M",             // fallback: Stages' sliders A D S R
        "3/LFO": "S S L L M M M S S S S",                      // Tides 2: Frequency, Shape large
        "3/Random / S&H": "S S L M M S L M M S M M S M S S",   // Marbles: T Rate, X Spread large
        "3/Function Generator": "S L L M M M M M M S S",       // fallback: Attack and Rate
        // Part FX
        "7/Granular": "M Lp Lt L Mp Mt Mt Mt S S S Mp Mp",     // Clouds
        "7/Granular Delay": "M Lp Lt M M M M M S S S M M M M M", // fallback: Time, Size (Clouds' Position, Size)
        "7/Resonator": "M L M M M M M M M S S M L",            // Rings: all white
        "7/Reverb": "L L M M M M M M S",                       // fallback
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

    /// The knob of P `p` (1 for P1). An engine with no entry gets white medium knobs with P1
    /// and P2 large.
    public static func knob(kind: EG13SlotKind, engine: String, p: Int) -> Knob {
        var knob: Knob
        if let list = parsed[key(kind, engine)] {
            knob = p >= 1 && p <= list.count ? list[p - 1] : Knob(size: .medium, emphasis: .plain)
        } else {
            knob = Knob(size: p <= 2 ? .large : .medium, emphasis: .plain)
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
