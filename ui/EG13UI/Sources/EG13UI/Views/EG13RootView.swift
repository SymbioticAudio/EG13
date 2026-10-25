import SwiftUI
import SymbioticTokens

/// The whole editor at its base size (spec §8, owner 2026-10-20: one part at a time,
/// module columns, bottom bar, collapsible keyboard).
public struct EG13RootView: View {
    let synth: EG13Synth

    public init(synth: EG13Synth) {
        self.synth = synth
    }

    public var body: some View {
        let theme = EG13Theme.theme
        VStack(spacing: 0) {
            EG13HeaderView(synth: synth)
            HStack(spacing: EG13Layout.gap) {
                EG13SourceCard(synth: synth, part: synth.selectedPart)
                EG13PlaceholderCard(category: .filter, icon: .slotFilter, note: "Filter engines arrive in M3")
                EG13PlaceholderCard(category: .shaper, icon: .slotShaper, note: "Shaper engines arrive in M3")
                EG13PlaceholderCard(category: .partFx, icon: .slotFx, note: "Part FX arrive in M5")
                EG13ModsColumn()
            }
            .padding(.horizontal, EG13Layout.outerPadding)
            .padding(.vertical, EG13Layout.gap)
            .frame(height: EG13Layout.columnStripHeight(keyboardVisible: synth.keyboardVisible))
            EG13BottomBar(synth: synth)
            Spacer(minLength: 0)
            EG13KeyboardPanel(synth: synth)
        }
        .frame(width: EG13Layout.baseSize.width, height: EG13Layout.baseSize.height)
        .background(theme.surface.window.color)
        .symbioticTheme(theme)
    }
}

/// The root view scaled to the editor's zoom (75–200%). scaleEffect keeps hit testing
/// and the keyboard's key mapping correct (verified in the M6a build spike).
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
