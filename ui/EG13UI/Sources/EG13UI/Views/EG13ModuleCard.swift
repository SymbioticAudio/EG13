import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// One horizontal module card: Source, Filter, Shaper, Part FX or a Mod. A slim header strip holds
/// the slot's name and the LCD engine menu (and a mod's Scope); under it the knobs of the
/// engine's P's, in as many rows as they need. An engine with no P's (Off) is the header alone.
/// Draws from `synth.slotControls`, so what shows is what the reachability test checks.
public struct EG13ModuleCard: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let part: Int
    /// 1...4 for a mod slot, nil for the others.
    let number: Int?
    let width: CGFloat
    /// Called with a route's index (0...11) when its Motion row is chosen; the matrix editor
    /// opens that route.
    let onMotionSelect: (Int) -> Void
    @Environment(\.symbioticTheme) private var theme

    public init(synth: EG13Synth, kind: EG13SlotKind, part: Int, number: Int? = nil, width: CGFloat,
                onMotionSelect: @escaping (Int) -> Void = { _ in }) {
        self.synth = synth
        self.kind = kind
        self.part = part
        self.number = number
        self.width = width
        self.onMotionSelect = onMotionSelect
    }

    /// The trace of a mod card's first output, on the right of the header strip.
    static let traceSize = CGSize(width: 64, height: 20)
    /// The least the trace shrinks to before the Motion button moves.
    static let traceMinWidth: CGFloat = 40

    static func category(_ kind: EG13SlotKind) -> EG13Category {
        switch kind {
        case EG13_SLOT_KIND_SOURCE: return .source
        case EG13_SLOT_KIND_FILTER: return .filter
        case EG13_SLOT_KIND_SHAPER: return .shaper
        case EG13_SLOT_KIND_PART_FX: return .partFx
        default: return .mod
        }
    }

    /// "Source", "Filter", "Shaper", "Part FX", "Mod 2": the card's name for VoiceOver.
    static func title(_ kind: EG13SlotKind) -> String {
        switch kind {
        case EG13_SLOT_KIND_MOD1: return "Mod 1"
        case EG13_SLOT_KIND_MOD2: return "Mod 2"
        case EG13_SLOT_KIND_MOD3: return "Mod 3"
        case EG13_SLOT_KIND_MOD4: return "Mod 4"
        default: return category(kind).title.capitalized
        }
    }

    static func icon(_ kind: EG13SlotKind) -> IconRole {
        switch kind {
        case EG13_SLOT_KIND_SOURCE: return .slotSource
        case EG13_SLOT_KIND_FILTER: return .slotFilter
        case EG13_SLOT_KIND_SHAPER: return .slotShaper
        case EG13_SLOT_KIND_PART_FX: return .slotFx
        default: return .slotModulation
        }
    }

    /// One line of the card header: number or icon and title, the engine and scope controls, the Motion
    /// button (`motion`) and, at the end, a mod card's trace.
    private func headerRow(kind: EG13SlotKind, controls: EG13SlotControls, number: Int?, showsTrace: Bool,
                           motion: Bool) -> some View {
        HStack(spacing: theme.spacing.s) {
            if let number {
                Text("\(number)")
                    .font(theme.type.caption.font.weight(.bold))
                    .foregroundStyle(theme.text.inverse.color)
                    .frame(width: 18, height: 18)
                    .background(RoundedRectangle(cornerRadius: 2).fill(EG13Palette.groupChip.color))
                    .accessibilityHidden(true)
            } else {
                Icon(Self.icon(kind), size: 18).foregroundStyle(theme.text.secondary.color)
                Text(Self.category(kind).title)
                    .font(theme.type.caption.font.weight(.bold))
                    .foregroundStyle(theme.text.secondary.color)
                    .accessibilityAddTraits(.isHeader)
            }
            if let engine = controls.engine { EG13ControlView(synth: synth, spec: engine, part: part) }
            if let scope = controls.scope { EG13ControlView(synth: synth, spec: scope, part: part) }
            // The Motion button shows with at least one route into the card; a mod card's trace sits at the end.
            if motion { EG13MotionButton(synth: synth, kind: kind, part: part, onSelect: onMotionSelect) }
            Spacer(minLength: 0)
            if let number, showsTrace {
                EG13ModTraceView(trace: synth.modTrace(part: part, slot: number - 1))
                    .frame(minWidth: Self.traceMinWidth, maxWidth: Self.traceSize.width)
                    .frame(height: Self.traceSize.height)
            }
        }
        .padding(.horizontal, EG13Layout.cardPadding)
        .frame(height: EG13Layout.cardHeaderHeight)
    }

    public var body: some View {
        let slot = synth.slot(kind, part: part)
        let controls = synth.slotControls(kind, part: part)
        let isMod = number != nil
        VStack(alignment: .leading, spacing: 0) {
            // The header row; a mod card's trace takes the space left (40 pt at least). If even that does not
            // fit next to the Motion button, the button moves to a second line: nothing is clipped.
            let showsTrace = isMod && !controls.params.isEmpty && synth.showsModulation
            ViewThatFits(in: .horizontal) {
                headerRow(kind: kind, controls: controls, number: number, showsTrace: showsTrace, motion: true)
                VStack(alignment: .leading, spacing: 0) {
                    headerRow(kind: kind, controls: controls, number: number, showsTrace: showsTrace, motion: false)
                    HStack(spacing: 0) {
                        EG13MotionButton(synth: synth, kind: kind, part: part, onSelect: onMotionSelect)
                        Spacer(minLength: 0)
                    }
                    .padding(.horizontal, EG13Layout.cardPadding)
                    .frame(height: EG13Layout.cardHeaderHeight)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(theme.surface.raised.color)
            if !controls.params.isEmpty {
                EG13KnobGrid(synth: synth, specs: controls.params, part: part,
                             width: width - 2 * EG13Layout.cardPadding,
                             minCell: isMod ? EG13Layout.modMinCell : EG13Layout.slotMinCell)
                    .padding(.horizontal, EG13Layout.cardPadding)
                    .padding(.vertical, EG13Layout.cardPadding)
            }
        }
        .frame(width: width, alignment: .topLeading)
        .background(theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(Self.title(kind)): \(slot.engineName)")
    }
}

/// The four mod cards, one under another (the right-hand column).
public struct EG13ModCards: View {
    let synth: EG13Synth
    let part: Int
    let width: CGFloat
    /// The height the cards are justified over: the taller column's.
    let columnHeight: CGFloat
    let onMotionSelect: (Int) -> Void

    public init(synth: EG13Synth, part: Int, width: CGFloat, columnHeight: CGFloat,
                onMotionSelect: @escaping (Int) -> Void = { _ in }) {
        self.synth = synth
        self.part = part
        self.width = width
        self.columnHeight = columnHeight
        self.onMotionSelect = onMotionSelect
    }

    static let kinds: [EG13SlotKind] = [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3,
                                        EG13_SLOT_KIND_MOD4]

    public var body: some View {
        EG13SpreadStack(columnHeight: columnHeight) {
            ForEach(Self.kinds.indices, id: \.self) { index in
                EG13ModuleCard(synth: synth, kind: Self.kinds[index], part: part, number: index + 1, width: width,
                                onMotionSelect: onMotionSelect)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Modulation")
    }
}

/// Cards one under another, justified over `columnHeight` (the taller column's height): the cards keep
/// their own heights and the free space is shared equally between them (EG13Layout.spreadGap). The
/// stack reports its height at the minimum gap, so a parent can measure the column's natural height;
/// the cards are placed at the spread gap, inside the `columnHeight` the parent gives the column.
struct EG13SpreadStack: Layout {
    var columnHeight: CGFloat

    private func heights(_ subviews: Subviews, width: CGFloat?) -> [CGFloat] {
        subviews.map { $0.sizeThatFits(ProposedViewSize(width: width, height: nil)).height }
    }

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let width = proposal.width ?? subviews.map { $0.sizeThatFits(.unspecified).width }.max() ?? 0
        let h = heights(subviews, width: width)
        let total: CGFloat = h.reduce(0, +) + EG13Layout.gap * CGFloat(max(0, h.count - 1))
        return CGSize(width: width, height: total)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        let h = heights(subviews, width: bounds.width)
        let gap = EG13Layout.spreadGap(heights: h, columnHeight: columnHeight)
        var y = bounds.minY
        for (index, subview) in subviews.enumerated() {
            subview.place(at: CGPoint(x: bounds.minX, y: y), anchor: .topLeading,
                          proposal: ProposedViewSize(width: bounds.width, height: h[index]))
            y += h[index] + gap
        }
    }
}

/// The Source, Filter, Shaper and Part FX cards, one under another.
public struct EG13SlotCards: View {
    let synth: EG13Synth
    let part: Int
    let width: CGFloat
    /// The height the cards are justified over: the taller column's.
    let columnHeight: CGFloat
    let onMotionSelect: (Int) -> Void

    public init(synth: EG13Synth, part: Int, width: CGFloat, columnHeight: CGFloat,
                onMotionSelect: @escaping (Int) -> Void = { _ in }) {
        self.synth = synth
        self.part = part
        self.width = width
        self.columnHeight = columnHeight
        self.onMotionSelect = onMotionSelect
    }

    static let kinds: [EG13SlotKind] = [EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER,
                                        EG13_SLOT_KIND_PART_FX]

    public var body: some View {
        EG13SpreadStack(columnHeight: columnHeight) {
            ForEach(Self.kinds.indices, id: \.self) { index in
                EG13ModuleCard(synth: synth, kind: Self.kinds[index], part: part, width: width,
                                onMotionSelect: onMotionSelect)
            }
        }
    }
}
