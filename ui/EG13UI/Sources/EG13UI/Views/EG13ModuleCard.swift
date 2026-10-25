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
    @Environment(\.symbioticTheme) private var theme

    public init(synth: EG13Synth, kind: EG13SlotKind, part: Int, number: Int? = nil, width: CGFloat) {
        self.synth = synth
        self.kind = kind
        self.part = part
        self.number = number
        self.width = width
    }

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

    public var body: some View {
        let slot = synth.slot(kind, part: part)
        let controls = synth.slotControls(kind, part: part)
        let isMod = number != nil
        VStack(alignment: .leading, spacing: 0) {
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
                Spacer(minLength: 0)
            }
            .padding(.horizontal, EG13Layout.cardPadding)
            .frame(height: EG13Layout.cardHeaderHeight)
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

    public init(synth: EG13Synth, part: Int, width: CGFloat) {
        self.synth = synth
        self.part = part
        self.width = width
    }

    static let kinds: [EG13SlotKind] = [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3,
                                        EG13_SLOT_KIND_MOD4]

    public var body: some View {
        VStack(spacing: EG13Layout.gap) {
            ForEach(Self.kinds.indices, id: \.self) { index in
                EG13ModuleCard(synth: synth, kind: Self.kinds[index], part: part, number: index + 1, width: width)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Modulation")
    }
}

/// The Source, Filter, Shaper and Part FX cards, one under another.
public struct EG13SlotCards: View {
    let synth: EG13Synth
    let part: Int
    let width: CGFloat

    public init(synth: EG13Synth, part: Int, width: CGFloat) {
        self.synth = synth
        self.part = part
        self.width = width
    }

    static let kinds: [EG13SlotKind] = [EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER,
                                        EG13_SLOT_KIND_PART_FX]

    public var body: some View {
        VStack(spacing: EG13Layout.gap) {
            ForEach(Self.kinds.indices, id: \.self) { index in
                EG13ModuleCard(synth: synth, kind: Self.kinds[index], part: part, width: width)
            }
        }
    }
}
