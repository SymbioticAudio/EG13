import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// A stepped setting: an LCD well (drag, scroll, double-click, keys) between minus and
/// plus buttons. Behaviour is EG13ControlInteraction; stepped values snap to integers.
public struct EG13Stepped: View {
    let param: EG13Param
    let label: String?

    public init(param: EG13Param, label: String? = nil) {
        self.param = param
        self.label = label
    }

    public var body: some View {
        EG13SteppedBody(param: param, label: label ?? param.name)
            .id(ObjectIdentifier(param))
    }
}

private struct EG13SteppedBody: View {
    let param: EG13Param
    let label: String
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlColors) private var colors

    init(param: EG13Param, label: String) {
        self.param = param
        self.label = label
        _interaction = State(initialValue: param.makeInteraction())
    }

    var body: some View {
        VStack(spacing: theme.spacing.xxs) {
            HStack(spacing: theme.spacing.xxs) {
                stepButton(-1, icon: .editRemove, name: "Decrease")
                EG13LCDText(text: param.text, isFocused: isFocused)
                    .frame(width: EG13Layout.stepperWidth, height: 26)
                    .eg13ControlInput($interaction)
                    .modifier(EG13ControlAccess(interaction: $interaction, label: param.accessibilityLabel,
                                                valueText: param.text, focus: $isFocused))
                stepButton(1, icon: .editAdd, name: "Increase")
            }
            EG13ControlLabel(text: label, style: .plain, font: theme.type.caption.font)
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
