import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// A stepped setting: an LCD well (drag, scroll, double-click, keys) between minus and
/// plus buttons, drawn by the environment's EG13ControlStyle.
public struct EG13Stepper: View {
    let param: EG13Param
    let label: String?

    public init(param: EG13Param, label: String? = nil) {
        self.param = param
        self.label = label
    }

    public var body: some View {
        EG13StepperBody(param: param, label: label ?? param.name)
            .id(ObjectIdentifier(param))
    }
}

private struct EG13StepperBody: View {
    let param: EG13Param
    let label: String
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlStyle) private var style

    init(param: EG13Param, label: String) {
        self.param = param
        self.label = label
        _interaction = State(initialValue: EG13ControlInteraction(param: param))
    }

    var body: some View {
        VStack(spacing: theme.spacing.xxs) {
            HStack(spacing: theme.spacing.xxs) {
                stepButton(-1, icon: .editRemove, name: "Decrease")
                style.stepper(EG13StepperConfiguration(
                    text: param.text, isFocused: isFocused, isEnabled: true))
                    .frame(width: EG13Layout.stepperWidth, height: 26)
                    .overlay(EG13ControlSurface(interaction: interaction))
                    .modifier(EG13ControlAccess(interaction: interaction, focus: $isFocused))
                stepButton(1, icon: .editAdd, name: "Increase")
            }
            EG13ControlLabel(text: label, chip: nil, colors: style.colors, font: theme.type.caption.font)
        }
    }

    private func stepButton(_ steps: Int, icon: IconRole, name: String) -> some View {
        Button { interaction.increment(by: steps) } label: { Icon(icon, size: 10) }
            .buttonStyle(.plain)
            .foregroundStyle(style.colors.label)
            .accessibilityLabel("\(name) \(param.accessibilityLabel)")
    }
}
