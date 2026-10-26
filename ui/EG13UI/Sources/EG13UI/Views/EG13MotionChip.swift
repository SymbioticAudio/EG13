import EG13CBridge
import SwiftUI
import SymbioticTokens

/// One motion chip in a card's header strip: "M1 → CUTOFF", a small LCD well like the engine menu.
/// Its fill lights from the well's dark to the LCD green with the source's live level (a fixed
/// source such as velocity has none and shows steady); the text flips to dark once the fill is lit.
/// Never truncated: the strip decides how many fit (EG13ChipFit).
struct EG13MotionChip: View {
    let text: String
    /// 0...1, or nil for a steady source.
    let level: Double?
    let accessibilityLabel: String
    let action: () -> Void
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography only

    /// The level a steady source shows.
    static let steadyLevel = 0.7

    var body: some View {
        let shown = level ?? Self.steadyLevel
        Button(action: action) {
            Text(text)
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(shown > 0.5 ? colors.lcdBackground : colors.lcdText)
                .lineLimit(1)
                .fixedSize()
                .padding(.horizontal, EG13ChipFit.padding)
                .frame(height: EG13Layout.motionChipHeight)
                .background {
                    ZStack {
                        Capsule().fill(colors.lcdBackground)
                        Capsule().fill(colors.lcdText.opacity(min(max(shown, 0), 1)))
                    }
                }
                .overlay(Capsule().stroke(colors.lcdText, lineWidth: 0.6))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(accessibilityLabel)
        .accessibilityHint("Opens the route in the matrix")
    }
}

/// A chip that reads its source's level itself, so only the chip is rebuilt when the level moves.
private struct EG13LiveChip: View {
    let synth: EG13Synth
    let part: Int
    let chip: EG13MotionChipInfo
    let accessibilityLabel: String
    let action: () -> Void

    var body: some View {
        EG13MotionChip(text: chip.text, level: synth.sourceLevel(part: part, code: chip.source),
                       accessibilityLabel: accessibilityLabel, action: action)
    }
}

/// The chips of one card, left to right, with a "+k" chip for those that do not fit `width`.
struct EG13CardChips: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let part: Int
    let width: CGFloat
    /// Called with the route's index when a chip is clicked (the matrix editor opens it).
    let onSelect: (Int) -> Void

    var body: some View {
        let chips = synth.chips(kind: kind, part: part)
        let shown = EG13ChipFit.shown(texts: chips.map(\.text), available: width)
        HStack(spacing: EG13ChipFit.spacing) {
            ForEach(chips.prefix(shown)) { chip in
                EG13LiveChip(synth: synth, part: part, chip: chip, accessibilityLabel: label(chip)) {
                    onSelect(chip.route)
                }
            }
            // The "+k" chip is shown whole or not at all (a strip too narrow even for it shows none).
            if shown < chips.count, EG13ChipFit.width("+\(chips.count - shown)")
                <= width - EG13ChipFit.usedWidth(chips.prefix(shown).map(\.text)) {
                EG13MotionChip(text: "+\(chips.count - shown)", level: nil,
                               accessibilityLabel: "\(chips.count - shown) more routes") {
                    onSelect(chips[shown].route)
                }
            }
        }
        .frame(width: width, alignment: .leading)
    }

    private func label(_ chip: EG13MotionChipInfo) -> String {
        let route = synth.routes(part: part)[chip.route]
        let amount = Int((route.amount * 100).rounded())
        return "Modulation: \(route.sourceName ?? "") to \(route.destinationName ?? ""), \(amount > 0 ? "+" : "")\(amount) percent"
    }
}
