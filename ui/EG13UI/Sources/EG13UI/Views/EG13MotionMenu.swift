import EG13CBridge
import SwiftUI
import SymbioticTokens

/// The Motion button in a card's header: an LCD well reading "MOTION n", n the number of routes
/// into the card. Only a card with at least one route has it. Clicking opens the Motion list in a
/// popover (a menu cannot show live levels); choosing a row calls `onSelect` with the route's
/// index, which opens that route in the matrix editor.
struct EG13MotionButton: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let part: Int
    let onSelect: (Int) -> Void
    @State private var isOpen = false
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography and radii only

    var body: some View {
        let items = synth.motionItems(kind: kind, part: part)
        if !items.isEmpty {
            Button { isOpen.toggle() } label: {
                Text("MOTION \(items.count)")
                    .font(.system(size: max(theme.type.caption.size, 11), weight: .bold, design: .monospaced))
                    .foregroundStyle(colors.lcdText)
                    .lineLimit(1)
                    .fixedSize()
                    .padding(.horizontal, theme.spacing.s)
                    .frame(height: EG13Layout.motionButtonHeight)
                    .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Motion, \(items.count) \(items.count == 1 ? "route" : "routes")")
            .accessibilityHint("Lists the routes into this card")
            .popover(isPresented: $isOpen, arrowEdge: .bottom) {
                EG13MotionList(synth: synth, kind: kind, part: part) { route in
                    isOpen = false
                    onSelect(route)
                }
            }
        }
    }
}

/// The Motion list of one card: a row per route, "Mod 2 Envelope → Cutoff", its amount, and a level
/// bar that follows the source live (a fixed source, such as velocity, shows no bar).
public struct EG13MotionList: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let part: Int
    let onSelect: (Int) -> Void

    public init(synth: EG13Synth, kind: EG13SlotKind, part: Int, onSelect: @escaping (Int) -> Void = { _ in }) {
        self.synth = synth
        self.kind = kind
        self.part = part
        self.onSelect = onSelect
    }
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme

    static let rowHeight: CGFloat = 26
    static let barSize = CGSize(width: 44, height: 6)

    public var body: some View {
        let items = synth.motionItems(kind: kind, part: part)
        VStack(alignment: .leading, spacing: 2) {
            ForEach(items) { item in
                Button { onSelect(item.route) } label: {
                    HStack(spacing: theme.spacing.s) {
                        Text(item.text)
                            .frame(maxWidth: .infinity, alignment: .leading)
                        Text(item.amountText)
                            .monospacedDigit()
                        EG13LevelBar(synth: synth, part: part, source: item.source)
                            .frame(width: Self.barSize.width, height: Self.barSize.height)
                    }
                    .font(.system(size: max(theme.type.caption.size, 12), weight: .bold, design: .monospaced))
                    .foregroundStyle(colors.lcdText)
                    .lineLimit(1)
                    .padding(.horizontal, theme.spacing.s)
                    .frame(height: Self.rowHeight)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("\(item.text), \(item.amountText)")
                .accessibilityHint("Opens the route in the matrix")
            }
        }
        .padding(theme.spacing.xs)
        .frame(width: 340)
        .background(colors.lcdBackground)
    }
}

/// A source's live level, a small bar; it reads the level itself, so only the bar is rebuilt when
/// the level moves. A fixed source (no level) shows an empty well.
private struct EG13LevelBar: View {
    let synth: EG13Synth
    let part: Int
    let source: UInt16
    @Environment(\.eg13ControlColors) private var colors

    var body: some View {
        let level = synth.sourceLevel(part: part, code: source)
        Canvas { context, size in
            let well = Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: 1)
            context.fill(well, with: .color(colors.lcdText.opacity(0.2)))
            guard let level else { return }
            let fill = Path(CGRect(x: 0, y: 0, width: size.width * CGFloat(level), height: size.height))
            context.fill(fill, with: .color(colors.lcdText))
        }
        .accessibilityHidden(true)
    }
}
