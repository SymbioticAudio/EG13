import CoreGraphics
import EG13CBridge

/// One motion chip: a route into a card, written source → parameter ("M1 → CUTOFF").
public struct EG13MotionChipInfo: Equatable, Identifiable, Sendable {
    /// The route's index in the part's matrix (0...11).
    public var route: Int
    /// The route's source code, for the chip's live level.
    public var source: UInt16
    public var text: String
    public var id: Int { route }
}

/// How a chip is written, and which card shows it (spec §8, M6b decision 3).
public enum EG13MotionChips {
    private static let fixedSources: [UInt16: String] = [
        1: "VEL", 2: "RVEL", 3: "NOTE", 4: "GATE", 5: "RND", 6: "PRESS", 7: "SLIDE", 8: "NBEND",
        9: "MW", 10: "PB", 11: "CP", 12: "CC A", 13: "CC B", 14: "CC C", 15: "CC D", 16: "PHASE",
    ]

    /// The card that shows a route into `destination`: the slot whose parameter it is. Pitch and Amp
    /// have no card of their own and show on Source (the voice's sound); Pan and Part Level are
    /// bottom-bar controls and show no chip. Nil for a code no card shows.
    public static func card(forDestination destination: UInt16) -> EG13SlotKind? {
        switch destination {
        case 1, 2: return EG13_SLOT_KIND_SOURCE
        case 16..<32: return EG13_SLOT_KIND_SOURCE
        case 32..<48: return EG13_SLOT_KIND_FILTER
        case 48..<64: return EG13_SLOT_KIND_SHAPER
        case 64..<80: return EG13_SLOT_KIND_PART_FX
        case 80..<96: return EG13_SLOT_KIND_MOD1
        case 96..<112: return EG13_SLOT_KIND_MOD2
        case 112..<128: return EG13_SLOT_KIND_MOD3
        case 128..<144: return EG13_SLOT_KIND_MOD4
        default: return nil
        }
    }

    /// "M1" for a mod slot's only output, "M2.1" for one of several (the core names those "... Out 1"),
    /// a short name for a fixed source, else the full name in capitals.
    public static func sourceText(code: UInt16, name: String) -> String {
        if code >= 32 && code < 64 {
            let slot = Int(code - 32) / 8 + 1, output = Int(code - 32) % 8 + 1
            return name.contains(" Out ") ? "M\(slot).\(output)" : "M\(slot)"
        }
        return fixedSources[code] ?? name.uppercased()
    }

    /// The parameter's short label from the destination's name ("Filter: Cutoff" is "CUTOFF").
    public static func destinationText(name: String) -> String {
        let parameter = name.range(of: ": ").map { String(name[$0.upperBound...]) } ?? name
        return EG13ParamLabels.short(parameter)
    }
}

/// How many chips fit a header strip. A chip is never cut: when they do not all fit, the first N
/// show and a "+k" chip stands for the rest.
public enum EG13ChipFit {
    public static let padding: CGFloat = 6
    public static let spacing: CGFloat = 4

    public static func width(_ text: String) -> CGFloat { EG13LabelFit.width(text) + 2 * padding }

    /// The width the chips take, each followed by the gap before the next chip.
    public static func usedWidth(_ texts: [String]) -> CGFloat {
        texts.map(width).reduce(0, +) + CGFloat(texts.count) * spacing
    }

    /// The most chips, from the first, that fit `available` with the "+k" chip after them.
    public static func shown(texts: [String], available: CGFloat) -> Int {
        let widths = texts.map(width)
        let all = widths.reduce(0, +) + CGFloat(max(0, texts.count - 1)) * spacing
        if all <= available { return texts.count }
        var count = texts.count - 1
        while count > 0 {
            let used = widths.prefix(count).reduce(0, +) + CGFloat(count) * spacing
                + width("+\(texts.count - count)")
            if used <= available { return count }
            count -= 1
        }
        return 0
    }
}

extension EG13Synth {
    /// The chips of a card: every route into one of its parameters that the part's engines show, in
    /// route order. A route into a hidden parameter, or from a hidden source, has none.
    public func chips(kind: EG13SlotKind, part: Int) -> [EG13MotionChipInfo] {
        routes(part: part).enumerated().compactMap { index, route in
            guard route.isActive, !route.destinationHidden, !route.sourceHidden,
                  EG13MotionChips.card(forDestination: route.destination) == kind,
                  let source = route.sourceName, let destination = route.destinationName else { return nil }
            return EG13MotionChipInfo(
                route: index, source: route.source,
                text: EG13MotionChips.sourceText(code: route.source, name: source) + " → "
                    + EG13MotionChips.destinationText(name: destination))
        }
    }

    /// A source's live level, 0...1: the latest max of its mod output's trace (0 in a gap). Nil for a
    /// fixed source, which has no trace and shows steady.
    public func sourceLevel(part: Int, code: UInt16) -> Double? {
        guard code >= 32 && code < 64 else { return nil }
        let slot = Int(code - 32) / 8, output = Int(code - 32) % 8
        guard let frame = modTrace(part: part, slot: slot).latest(output: output) else { return 0 }
        return min(max(Double(frame.max), 0), 1)
    }
}
