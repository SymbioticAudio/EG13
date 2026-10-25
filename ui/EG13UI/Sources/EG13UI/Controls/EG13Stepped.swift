import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// A stepped setting: an LCD well (drag, scroll, double-click, keys) between minus and
/// plus buttons. Behaviour is EG13ControlInteraction; stepped values snap to integers.
public struct EG13Stepped: View {
    let param: EG13Param
    let label: String?
    let wellWidth: CGFloat
    let showsLabel: Bool

    /// - Parameters:
    ///   - wellWidth: the LCD well's width; 64 pt, or `EG13Layout.wideStepperWidth` for long values.
    ///   - showsLabel: false where the value says what it is (a mod card's Scope): the label stays
    ///     for VoiceOver.
    public init(param: EG13Param, label: String? = nil, wellWidth: CGFloat = EG13Layout.stepperWidth,
                showsLabel: Bool = true) {
        self.param = param
        self.label = label
        self.wellWidth = wellWidth
        self.showsLabel = showsLabel
    }

    public var body: some View {
        EG13SteppedBody(param: param, label: label ?? param.name, wellWidth: wellWidth, showsLabel: showsLabel)
            .id(ObjectIdentifier(param))
    }
}

private struct EG13SteppedBody: View {
    let param: EG13Param
    let label: String
    let wellWidth: CGFloat
    let showsLabel: Bool
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlColors) private var colors

    init(param: EG13Param, label: String, wellWidth: CGFloat, showsLabel: Bool) {
        self.param = param
        self.label = label
        self.wellWidth = wellWidth
        self.showsLabel = showsLabel
        _interaction = State(initialValue: param.makeInteraction())
    }

    var body: some View {
        VStack(spacing: theme.spacing.xxs) {
            HStack(spacing: theme.spacing.xxs) {
                stepButton(-1, icon: .editRemove, name: "Decrease")
                EG13LCDText(text: param.text, isFocused: isFocused)
                    .frame(width: wellWidth, height: 26)
                    .eg13ControlInput($interaction)
                    .modifier(EG13ControlAccess(interaction: $interaction, label: param.accessibilityLabel,
                                                valueText: param.text, focus: $isFocused))
                stepButton(1, icon: .editAdd, name: "Increase")
            }
            if showsLabel { EG13ControlLabel(text: label, style: .plain, font: theme.type.caption.font) }
        }
        .onChange(of: param.descriptor) { _, _ in interaction.cancel(); interaction = param.makeInteraction() }
    }

    private func stepButton(_ steps: Int, icon: IconRole, name: String) -> some View {
        Button { interaction.increment(by: steps) } label: { Icon(icon, size: 10) }
            .buttonStyle(.plain)
            .foregroundStyle(colors.label)
            .accessibilityLabel("\(name) \(param.accessibilityLabel)")
    }
}
