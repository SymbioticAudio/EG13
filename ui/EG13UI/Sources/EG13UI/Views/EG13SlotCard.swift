import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// A slot card (Source, Filter, Shaper or Part FX): a light header strip with the small
/// category label and the lowercase engine title, the engine menu, and a grid of the knobs
/// the engine uses, in four columns. Draws from `synth.editorControls`, so what shows is
/// what the reachability test checks.
public struct EG13SlotCard: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    public init(synth: EG13Synth, kind: EG13SlotKind, part: Int) {
        self.synth = synth
        self.kind = kind
        self.part = part
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
        let category = Self.category(kind)
        VStack(spacing: 0) {
            HStack(spacing: theme.spacing.s) {
                Icon(Self.icon(kind), size: 18).foregroundStyle(theme.text.secondary.color)
                VStack(alignment: .leading, spacing: 0) {
                    Text(category.title)
                        .font(theme.type.caption.font.weight(.bold))
                        .foregroundStyle(theme.text.secondary.color)
                        .accessibilityAddTraits(.isHeader)
                    Text(slot.engineName.lowercased())
                        .font(.system(size: theme.type.title.size, weight: .light))
                        .foregroundStyle(theme.text.primary.color)
                        .lineLimit(1)
                        .minimumScaleFactor(0.6)
                }
                Spacer(minLength: 0)
            }
            .padding(.horizontal, theme.spacing.s)
            .frame(height: EG13Layout.cardHeaderHeight)
            .frame(maxWidth: .infinity)
            .background(theme.surface.raised.color)
            VStack(spacing: theme.spacing.s) {
                EG13SlotPicker(synth: synth, slot: slot,
                               accessibilityName: "\(EG13PartName.name(part)) \(category.title.capitalized) engine")
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, theme.spacing.xs)
                EG13ControlGrid(columns: EG13Layout.slotColumns, specs: controls.params) { spec in
                    EG13ControlView(synth: synth, spec: spec, part: part)
                }
            }
            .padding(.horizontal, theme.spacing.xs)  // knob names need every point of the column
            .padding(.vertical, theme.spacing.s)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
        }
        .frame(width: EG13Layout.cardWidth)
        .frame(maxHeight: .infinity)
        .background(theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(category.title.capitalized): \(slot.engineName)")
    }
}
