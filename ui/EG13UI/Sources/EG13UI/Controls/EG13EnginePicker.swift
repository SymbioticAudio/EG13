import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// A menu of engine names that edits the slot's selector parameter. Choosing an engine is
/// one gesture on the selector (begin, set, end).
public struct EG13EnginePicker: View {
    let names: [String]
    let selector: EG13Param
    let selected: Int
    let accessibilityName: String
    let wellWidth: CGFloat
    @Environment(\.eg13InteractiveControls) private var interactive

    /// - Parameters:
    ///   - names: engine names, in selector order.
    ///   - selector: the slot's engine selector parameter.
    ///   - selected: the engine the plugin reports as selected.
    ///   - accessibilityName: e.g. "Part A Source engine".
    ///   - wellWidth: the LCD well's width; the editor's cards use `EG13Layout.enginePickerWidth`,
    ///     wide enough for every engine name.
    public init(names: [String], selector: EG13Param, selected: Int, accessibilityName: String,
                wellWidth: CGFloat = 118) {
        self.wellWidth = wellWidth
        self.names = names
        self.selector = selector
        self.selected = selected
        self.accessibilityName = accessibilityName
    }

    public var body: some View {
        Group {
            if interactive {
                Menu {
                    ForEach(names.indices, id: \.self) { index in
                        // A Toggle in a menu is drawn with the menu's own check mark (menu state, not an image).
                        Toggle(names[index], isOn: Binding(get: { index == selected },
                                                          set: { _ in selector.setValue(Double(index)) }))
                    }
                } label: { well }
                .menuStyle(.borderlessButton)
                .menuIndicator(.visible)
            } else {
                // An image render cannot draw the AppKit menu: show its closed state.
                HStack(spacing: 4) {
                    well
                    VStack(spacing: -3) { Icon(.navUp, size: 8); Icon(.navDown, size: 8) }
                }
            }
        }
        .fixedSize()
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(accessibilityName)
        .accessibilityValue(selectedName)
    }

    private var selectedName: String { names.indices.contains(selected) ? names[selected] : "" }

    /// The same LCD look as the other wells: green on grey.
    private var well: some View {
        EG13LCDText(text: selectedName)
            .frame(width: wellWidth, height: 24)
    }
}
