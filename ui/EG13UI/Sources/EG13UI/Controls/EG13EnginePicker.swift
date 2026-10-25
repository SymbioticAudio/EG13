import SwiftUI
import SymbioticTokens

/// A menu of engine names that edits the slot's selector parameter. Choosing an engine is
/// one gesture on the selector (begin, set, end).
public struct EG13EnginePicker: View {
    let names: [String]
    let selector: EG13Param
    let selected: Int
    let accessibilityName: String
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only

    /// - Parameters:
    ///   - names: engine names, in selector order.
    ///   - selector: the slot's engine selector parameter.
    ///   - selected: the engine the plugin reports as selected.
    ///   - accessibilityName: e.g. "Part A Source engine".
    public init(names: [String], selector: EG13Param, selected: Int, accessibilityName: String) {
        self.names = names
        self.selector = selector
        self.selected = selected
        self.accessibilityName = accessibilityName
    }

    public var body: some View {
        Menu {
            ForEach(names.indices, id: \.self) { index in
                Button {
                    selector.setValue(Double(index))
                } label: {
                    if index == selected { Label(names[index], systemImage: "checkmark") } else { Text(names[index]) }
                }
            }
        } label: {
            Text(names.indices.contains(selected) ? names[selected] : "")
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(colors.chipText)
                .lineLimit(1)
                .padding(.horizontal, theme.spacing.xs)
                .frame(minWidth: 96, minHeight: 20)
                .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.skirt))
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.visible)
        .fixedSize()
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityName)
        .accessibilityValue(names.indices.contains(selected) ? names[selected] : "")
    }
}
