/// What a modulated knob's ring shows, as positions across the knob's sweep (0...1).
///
/// With a live modulated value the ring runs from the set value to it and a dot marks the live
/// position. With none (a knob with routes but no movement now) the ring is the range the routes
/// can sweep, with no dot (spec §8, M6b decision 2).
public struct EG13ModRing: Equatable, Sendable {
    public var from: Double
    public var to: Double
    public var dot: Double?

    public init(from: Double, to: Double, dot: Double?) {
        self.from = from
        self.to = to
        self.dot = dot
    }

    public static func make(base: Double, live: Double?, range: ClosedRange<Double>?) -> EG13ModRing? {
        func clamp(_ x: Double) -> Double { min(max(x, 0), 1) }
        if let live {
            let at = clamp(live), start = clamp(base)
            return EG13ModRing(from: min(start, at), to: max(start, at), dot: at)
        }
        if let range {
            return EG13ModRing(from: clamp(range.lowerBound), to: clamp(range.upperBound), dot: nil)
        }
        return nil
    }
}

extension EG13Param {
    /// The ring this knob draws; nil when no route modulates it. Reads the shown value and the two
    /// modulation properties, so a knob that reads it follows exactly these.
    public var modRing: EG13ModRing? {
        EG13ModRing.make(base: normalized, live: modulatedNormalized, range: modulationRange)
    }
}
