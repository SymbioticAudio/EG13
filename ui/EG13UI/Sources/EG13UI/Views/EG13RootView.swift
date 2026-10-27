import EG13CBridge
import SwiftUI
import SymbioticTokens

/// The whole editor at its base size (spec §8, owner 2026-10-20 and 2026-10-25: one part at a
/// time, horizontal module cards, a bottom bar). Top to bottom: header; the card strip (Source,
/// Filter, Shaper and Part FX cards, one under another, and a column beside them that shows the four
/// Mods or the 12-route matrix, by a MODS / MATRIX switch); the bottom bar.
public struct EG13RootView: View {
    let synth: EG13Synth
    /// The right-hand column's pane and the matrix row a Motion row asked for; kept across part switches.
    @State private var column = EG13RightColumnState()

    /// - Parameter column: the right-hand column's state; the previews start it on MATRIX.
    public init(synth: EG13Synth, column: EG13RightColumnState = EG13RightColumnState()) {
        self.synth = synth
        _column = State(initialValue: column)
    }

    public var body: some View {
        let theme = EG13Theme.theme
        let part = synth.selectedPart
        VStack(spacing: 0) {
            EG13HeaderView(synth: synth)
            VStack(spacing: EG13Layout.gap) {
                strip(part: part)
                    .frame(width: EG13Layout.stripWidth, height: EG13Layout.stripHeight, alignment: .topLeading)
                    .clipped()
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

    private func strip(part: Int) -> some View {
        HStack(alignment: .top, spacing: EG13Layout.gap) {
            EG13Scrolling {
                EG13SlotCards(synth: synth, part: part, width: EG13Layout.slotCardWidth,
                              onMotionSelect: column.open(route:))
            }
            rightColumn(part: part)
        }
        .environment(\.eg13OpenRoute, column.open(route:))
    }

    /// MODS (the four mod cards) or MATRIX (the 12 routes), switched at the top.
    private func rightColumn(part: Int) -> some View {
        VStack(alignment: .leading, spacing: EG13Layout.gap - 2) {
            EG13LCDSegments(labels: EG13RightColumnState.Pane.allCases.map(\.title), selected: column.pane.rawValue,
                            accessibilityPrefix: "Show") { column.pane = EG13RightColumnState.Pane(rawValue: $0) ?? .mods }
                .frame(height: EG13Layout.paneSwitchHeight)
            switch column.pane {
            case .mods:
                EG13Scrolling {
                    EG13ModCards(synth: synth, part: part, width: EG13Layout.modCardWidth,
                                 onMotionSelect: column.open(route:))
                }
            case .matrix:
                EG13MatrixEditor(synth: synth, part: part, state: column, width: EG13Layout.modCardWidth)
            }
        }
        .frame(width: EG13Layout.modCardWidth, alignment: .topLeading)
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

    var body: some View {
        EG13RootView(synth: synth)
            .scaleEffect(zoom, anchor: .topLeading)
            .frame(width: EG13Layout.baseSize.width * zoom, height: EG13Layout.baseSize.height * zoom,
                   alignment: .topLeading)
    }
}
