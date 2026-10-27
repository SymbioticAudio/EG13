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
    /// The Settings and About panel, or nil when closed. It is drawn inside the editor (see EG13SettingsPanel).
    @State private var overlay = EG13SettingsOverlayState()
    /// The natural heights of the left cards and of the right pane's content (the cards, or the matrix rows);
    /// they start at the full strip and settle on the first layout pass.
    @State private var leftHeight = EG13Layout.stripHeight
    @State private var rightContentHeight = EG13Layout.stripHeight

    /// - Parameter column: the right-hand column's state; the previews start it on MATRIX.
    /// - Parameter settingsTab: the panel's tab if it starts open (the previews).
    public init(synth: EG13Synth, column: EG13RightColumnState = EG13RightColumnState(),
                settingsTab: EG13SettingsTab? = nil) {
        self.synth = synth
        _column = State(initialValue: column)
        _overlay = State(initialValue: EG13SettingsOverlayState(tab: settingsTab))
    }

    public var body: some View {
        let theme = EG13Theme.theme
        let part = synth.selectedPart
        let content = VStack(spacing: 0) {
            EG13HeaderView(synth: synth) { overlay.open() }
            VStack(spacing: EG13Layout.gap) {
                strip(part: part)
                    .frame(width: EG13Layout.stripWidth, height: stripHeight, alignment: .topLeading)
                    .clipped()
            }
            .padding(.horizontal, EG13Layout.outerPadding)
            .padding(.vertical, EG13Layout.gap)
            EG13BottomBar(synth: synth)
            Spacer(minLength: 0)
        }
        .frame(width: EG13Layout.baseSize.width, height: EG13Layout.baseSize.height, alignment: .top)
        .background(theme.surface.window.color)
        // While the overlay is open the editor behind it takes no input and is hidden from VoiceOver.
        // The knob surfaces are AppKit views, so they are also switched off through the environment.
        return content
            .transformEnvironment(\.eg13InteractiveControls) { $0 = $0 && overlay.contentIsInteractive }
            .allowsHitTesting(overlay.contentIsInteractive)
            .accessibilityHidden(overlay.contentIsHiddenFromAccessibility)
            .overlay {
                if let tab = overlay.tab {
                    EG13SettingsPanel(synth: synth, tab: Binding(get: { tab }, set: { overlay.tab = $0 })) {
                        overlay.close()
                    }
                }
            }
        .symbioticTheme(theme)
        // The shown part's cards are rebuilt from the model; a part switch replaces every control.
        .id(part)
    }

    /// The strip is as tall as the taller column, up to the full strip height: the bottom bar sits one gap
    /// under the content, and the blank space collects below it. A busy patch fills the strip (the columns
    /// scroll) and the bar is at the bottom.
    private var stripHeight: CGFloat {
        let right: CGFloat = EG13Layout.paneSwitchHeight + (EG13Layout.gap - 2) + rightContentHeight
        return min(EG13Layout.stripHeight, max(leftHeight, right))
    }

    private func strip(part: Int) -> some View {
        HStack(alignment: .top, spacing: EG13Layout.gap) {
            EG13Scrolling(onHeight: { leftHeight = $0 }) {
                EG13SlotCards(synth: synth, part: part, width: EG13Layout.slotCardWidth, columnHeight: stripHeight,
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
                EG13Scrolling(onHeight: { rightContentHeight = $0 }) {
                    EG13ModCards(synth: synth, part: part, width: EG13Layout.modCardWidth,
                                 columnHeight: stripHeight - EG13Layout.paneSwitchHeight - (EG13Layout.gap - 2),
                                 onMotionSelect: column.open(route:))
                }
            case .matrix:
                EG13MatrixEditor(synth: synth, part: part, state: column, width: EG13Layout.modCardWidth,
                                 onHeight: { rightContentHeight = $0 })
                    .frame(height: min(rightContentHeight, stripHeight - EG13Layout.paneSwitchHeight - (EG13Layout.gap - 2)),
                           alignment: .top)
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
    /// Called with the content's natural height whenever it changes.
    var onHeight: ((CGFloat) -> Void)?
    @ViewBuilder let content: Content

    var body: some View {
        let measured = content.background(
            GeometryReader { Color.clear.preference(key: EG13ContentHeightKey.self, value: $0.size.height) })
            .onPreferenceChange(EG13ContentHeightKey.self) { height in onHeight?(height) }
        if interactive {
            ScrollView(.vertical) { measured }
                .scrollBounceBehavior(.basedOnSize)
                .scrollIndicators(.automatic)
        } else {
            measured
        }
    }
}

private struct EG13ContentHeightKey: PreferenceKey {
    static let defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
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
