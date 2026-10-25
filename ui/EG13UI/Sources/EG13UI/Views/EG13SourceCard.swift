import SwiftUI
import SymbioticTokens

/// The Source card of the shown part: engine selector, hero knob (P1) with an LCD
/// readout, and the engine's other named P knobs; knobs the engine does not use are
/// hidden. Caps, arcs and chips follow the engine's emphasis metadata.
struct EG13SourceCard: View {
    let synth: EG13Synth
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let params = synth.sourceParams(part: part)
        EG13ModuleCard(category: .source, engineTitle: synth.sourceEngineName(part: part), icon: .slotSource) {
            engineSelector
        } hero: {
            if let hero = params.first {
                EG13Knob(param: hero, style: style(hero), size: .hero, showsMotionChip: true)
            }
        } detail: {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: theme.spacing.xs),
                                     count: EG13Layout.detailColumns),
                      spacing: theme.spacing.s) {
                ForEach(params.dropFirst(), id: \.id) { param in
                    EG13Knob(param: param, style: style(param), size: .detail, showsMotionChip: true)
                }
            }
        }
    }

    private func style(_ param: EG13Param) -> EG13KnobStyle {
        EG13Palette.knobStyle(for: synth.emphasis(of: param, part: part), part: part)
    }

    @ViewBuilder
    private var engineSelector: some View {
        let names = synth.sourceEngineNames
        if let selector = synth.engineSelector(part: part), names.count > 1 {
            EG13EnginePicker(names: names, selector: selector, selected: synth.sourceEngine(part: part),
                             accessibilityName: "\(EG13PartName.name(part)) Source engine")
        }
    }
}

/// MODS column: four empty mod slots and the matrix area, reserved for M4.
struct EG13ModsColumn: View {
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text(EG13Category.mod.title)
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(theme.text.secondary.color)
                .frame(height: EG13Layout.cardHeaderHeight)
                .accessibilityAddTraits(.isHeader)
            ForEach(1...4, id: \.self) { slot in
                HStack {
                    Text("\(slot)")
                        .font(theme.type.caption.font.weight(.bold))
                        .foregroundStyle(theme.text.inverse.color)
                        .frame(width: 18, height: 18)
                        .background(RoundedRectangle(cornerRadius: 2).fill(EG13Palette.groupChip.color))
                    Text("Empty").font(theme.type.caption.font).foregroundStyle(theme.text.secondary.color)
                    Spacer()
                }
                .frame(height: 36)
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Mod slot \(slot), empty")
            }
            Text("MATRIX").font(theme.type.caption.font.weight(.bold)).accessibilityAddTraits(.isHeader)
            Text("Modulation sources and 12 routes per part arrive in M4.")
                .font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .foregroundStyle(theme.text.primary.color)
        .padding(.horizontal, theme.spacing.s)
        .padding(.bottom, theme.spacing.s)
        .frame(width: EG13Layout.modsColumnWidth)
        .frame(maxHeight: .infinity)
        .background(EG13Palette.placeholderPanel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Modulation")
    }
}
