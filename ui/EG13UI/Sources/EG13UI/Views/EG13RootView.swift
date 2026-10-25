import EG13CBridge
import SwiftUI
import SymbioticTokens

/// The whole editor at its base size (spec §8, owner 2026-10-20: one part at a time, module
/// columns, a bottom bar). Top to bottom: header; Source, Filter, Shaper and Part FX cards with
/// the MODS column; the matrix route amounts; the bottom bar. The keyboard is not in this
/// version (it needs note input from the editor: M6b).
public struct EG13RootView: View {
    let synth: EG13Synth

    public init(synth: EG13Synth) {
        self.synth = synth
    }

    public var body: some View {
        let theme = EG13Theme.theme
        let part = synth.selectedPart
        VStack(spacing: 0) {
            EG13HeaderView(synth: synth)
            VStack(spacing: EG13Layout.gap) {
                HStack(spacing: EG13Layout.gap) {
                    EG13SlotCard(synth: synth, kind: EG13_SLOT_KIND_SOURCE, part: part)
                    EG13SlotCard(synth: synth, kind: EG13_SLOT_KIND_FILTER, part: part)
                    EG13SlotCard(synth: synth, kind: EG13_SLOT_KIND_SHAPER, part: part)
                    EG13SlotCard(synth: synth, kind: EG13_SLOT_KIND_PART_FX, part: part)
                    EG13ModsColumn(synth: synth, part: part)
                }
                .frame(height: EG13Layout.columnStripHeight)
                EG13MatrixAmounts(synth: synth, part: part)
                    .padding(.horizontal, 0)
            }
            .padding(.horizontal, EG13Layout.outerPadding)
            .padding(.vertical, EG13Layout.gap)
            EG13BottomBar(synth: synth)
            Spacer(minLength: 0)
        }
        .frame(width: EG13Layout.baseSize.width, height: EG13Layout.baseSize.height, alignment: .top)
        .background(theme.surface.window.color)
        .symbioticTheme(theme)
        // The shown part's cards are rebuilt from the model; a part switch replaces every control.
        .id(part)
    }
}

/// The root view scaled to the editor's zoom (75-200%). scaleEffect keeps hit testing correct.
struct EG13ScaledRootView: View {
    let synth: EG13Synth
    let zoom: CGFloat

    var body: some View {
        EG13RootView(synth: synth)
            .scaleEffect(zoom, anchor: .topLeading)
            .frame(width: EG13Layout.baseSize.width * zoom, height: EG13Layout.baseSize.height * zoom,
                   alignment: .topLeading)
    }
}
