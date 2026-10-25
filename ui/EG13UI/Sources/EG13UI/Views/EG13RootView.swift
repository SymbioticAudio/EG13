import EG13CBridge
import SwiftUI
import SymbioticTokens

/// The whole editor at its base size (spec §8, owner 2026-10-20 and 2026-10-25: one part at a
/// time, horizontal module cards, a bottom bar). Top to bottom: header; the card strip (Source,
/// Filter, Shaper and Part FX cards, one under another, and the four Mods: variant A beside them
/// in a column, variant B on a page of their own behind the header's VOICE / MODS switch); the
/// matrix route amounts; the bottom bar. The keyboard is not in this version (M6b).
public struct EG13RootView: View {
    let synth: EG13Synth
    let variant: EG13EditorVariant

    public init(synth: EG13Synth, variant: EG13EditorVariant = .modsColumn) {
        self.synth = synth
        self.variant = variant
    }

    public var body: some View {
        let theme = EG13Theme.theme
        let part = synth.selectedPart
        VStack(spacing: 0) {
            EG13HeaderView(synth: synth, variant: variant)
            VStack(spacing: EG13Layout.gap) {
                strip(part: part)
                    .frame(width: EG13Layout.stripWidth, height: EG13Layout.stripHeight, alignment: .topLeading)
                    .clipped()
                EG13MatrixAmounts(synth: synth, part: part)
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

    @ViewBuilder private func strip(part: Int) -> some View {
        switch variant {
        case .modsColumn:
            HStack(alignment: .top, spacing: EG13Layout.gap) {
                EG13Scrolling { EG13SlotCards(synth: synth, part: part, width: EG13Layout.slotCardWidth(variant)) }
                EG13Scrolling { EG13ModCards(synth: synth, part: part, width: EG13Layout.modCardWidth(variant)) }
            }
        case .modsPage:
            EG13Scrolling {
                if synth.showsMods {
                    EG13ModCards(synth: synth, part: part, width: EG13Layout.modCardWidth(variant))
                } else {
                    EG13SlotCards(synth: synth, part: part, width: EG13Layout.slotCardWidth(variant))
                }
            }
        }
    }
}

/// Cards are as tall as their knob rows need. When four large engines together are taller than the
/// strip (a rare combination), the column scrolls; every control stays reachable. Image renders
/// cannot draw a scroll view, so they show the content clipped.
struct EG13Scrolling<Content: View>: View {
    @Environment(\.eg13InteractiveControls) private var interactive
    @ViewBuilder let content: Content

    var body: some View {
        if interactive {
            ScrollView(.vertical) { content }
                .scrollBounceBehavior(.basedOnSize)
                .scrollIndicators(.automatic)
        } else {
            content
        }
    }
}

/// The root view scaled to the editor's zoom (75-200%). scaleEffect keeps hit testing correct.
struct EG13ScaledRootView: View {
    let synth: EG13Synth
    let zoom: CGFloat
    var variant: EG13EditorVariant = .modsColumn

    var body: some View {
        EG13RootView(synth: synth, variant: variant)
            .scaleEffect(zoom, anchor: .topLeading)
            .frame(width: EG13Layout.baseSize.width * zoom, height: EG13Layout.baseSize.height * zoom,
                   alignment: .topLeading)
    }
}
