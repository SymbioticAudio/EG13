import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// Placeholder for the on-screen keyboard: the strip with its toggle, no keys. The plugin
/// takes notes from the host only, so the keyboard is redesigned in Tasks 7-9.
struct EG13KeyboardPanel: View {
    @Bindable var synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(spacing: theme.spacing.s) {
            Button { synth.keyboardVisible.toggle() } label: {
                HStack(spacing: theme.spacing.xs) {
                    Icon(synth.keyboardVisible ? .navDown : .navUp, size: 12)
                    Text("KEYBOARD").font(theme.type.caption.font)
                }
            }
            .buttonStyle(.plain)
            .accessibilityLabel(synth.keyboardVisible ? "Hide keyboard" : "Show keyboard")
            Spacer()
        }
        .foregroundStyle(theme.text.inverse.color)
        .frame(height: EG13Layout.keyboardToggleHeight)
        .padding(.horizontal, EG13Layout.outerPadding)
        .background(theme.surface.inset.color)
    }
}
