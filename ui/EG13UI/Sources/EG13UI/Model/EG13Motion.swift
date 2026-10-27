import CoreGraphics
import EG13CBridge

/// One row of a card's Motion list: a route into the card, written in full
/// ("Mod 2 Envelope → Cutoff"), with its amount ("+30 %").
public struct EG13MotionItem: Equatable, Identifiable, Sendable {
    /// The route's index in the part's matrix (0...11).
    public var route: Int
    /// The route's source code, for the row's live level bar.
    public var source: UInt16
    public var text: String
    public var amountText: String
    public var id: Int { route }
}

/// Which card shows a route, and how a Motion row is written (spec §8, M6b decision 3, round 2).
public enum EG13Motion {
    /// The card that lists a route into `destination`: the slot whose parameter it is. Pitch and Amp
    /// have no card of their own and show on Source (the voice's sound); Pan and Part Level are
    /// bottom-bar controls and show no list. Nil for a code no card shows.
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

    /// The parameter's full name from the destination's name ("Filter: Cutoff" is "Cutoff").
    public static func destinationText(name: String) -> String {
        name.range(of: ": ").map { String(name[$0.upperBound...]) } ?? name
    }

    /// A route amount as a signed percentage: "+30 %", "-5 %", "0 %".
    public static func amountText(_ amount: Double) -> String {
        let percent = Int((amount * 100).rounded())
        return percent > 0 ? "+\(percent) %" : "\(percent) %"
    }
}

extension EG13Synth {
    /// The rows of a card's Motion list: every visible route into one of its parameters, in route
    /// order. A route into a hidden parameter, or from a hidden source, has none.
    public func motionItems(kind: EG13SlotKind, part: Int) -> [EG13MotionItem] {
        routes(part: part).enumerated().compactMap { index, route in
            guard route.isActive, !route.destinationHidden, !route.sourceHidden,
                  EG13Motion.card(forDestination: route.destination) == kind,
                  let source = route.sourceName, let destination = route.destinationName else { return nil }
            return EG13MotionItem(route: index, source: route.source,
                                  text: source + " → " + EG13Motion.destinationText(name: destination),
                                  amountText: EG13Motion.amountText(route.amount))
        }
    }

    /// Whether a card shows its MOTION button: the display is on and a route reaches the card.
    public func showsMotionButton(kind: EG13SlotKind, part: Int) -> Bool {
        showsModulation && !motionItems(kind: kind, part: part).isEmpty
    }

    /// The rows for one knob: every visible route into it, in route order (the knob's popover).
    public func motionItems(into id: UInt32, part: Int) -> [EG13MotionItem] {
        guard let destination = destinationByID[id] else { return [] }
        return routes(part: part).enumerated().compactMap { index, route in
            guard route.isActive, route.destination == destination, !route.destinationHidden, !route.sourceHidden,
                  let source = route.sourceName, let name = route.destinationName else { return nil }
            return EG13MotionItem(route: index, source: route.source,
                                  text: source + " → " + EG13Motion.destinationText(name: name),
                                  amountText: EG13Motion.amountText(route.amount))
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
