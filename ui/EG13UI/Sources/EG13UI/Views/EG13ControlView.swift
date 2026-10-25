import SwiftUI
import SymbioticTokens

/// Draws one control spec with the matching approved control: a knob in its emphasis style,
/// a stepper or the Mode switch. Engine menus are drawn by the card that owns the slot.
/// A spec whose parameter the plugin does not have draws nothing.
struct EG13ControlView: View {
    let synth: EG13Synth
    let spec: EG13ControlSpec
    let part: Int

    var body: some View {
        if let param = synth.param(spec.id) {
            switch spec.kind {
            case .knob(let size):
                EG13Knob(param: param, style: style, size: size, layout: EG13KnobLayout(label: spec.label))
            case .compactKnob:
                EG13Knob(param: param, style: style, size: .detail,
                         layout: EG13KnobLayout(diameter: EG13Layout.compactKnobDiameter, showsValue: false,
                                                label: spec.label))
            case .stepped:
                EG13Stepped(param: param, label: spec.label)
            case .segmented:
                EG13ModeSwitch(param: param)
            case .enginePicker:
                EmptyView()
            }
        }
    }

    private var style: EG13KnobStyle { EG13Palette.knobStyle(for: spec.emphasis, part: part) }
}

/// Knob cells in rows of `columns`, each cell the same width; the last row is left-aligned.
struct EG13ControlGrid<Cell: View>: View {
    let columns: Int
    let specs: [EG13ControlSpec]
    var rowSpacing: CGFloat = 6
    @ViewBuilder let cell: (EG13ControlSpec) -> Cell

    var body: some View {
        let rows = stride(from: 0, to: specs.count, by: columns).map { Array(specs[$0..<min($0 + columns, specs.count)]) }
        VStack(spacing: rowSpacing) {
            ForEach(rows.indices, id: \.self) { row in
                HStack(alignment: .top, spacing: 0) {
                    ForEach(0..<columns, id: \.self) { column in
                        Group {
                            if column < rows[row].count { cell(rows[row][column]) } else { Color.clear }
                        }
                        .frame(maxWidth: .infinity)
                    }
                }
            }
        }
    }
}

/// The engine menu of a slot, or nothing when the plugin has no engines for it.
struct EG13SlotPicker: View {
    let synth: EG13Synth
    let slot: EG13Slot
    let accessibilityName: String

    var body: some View {
        if let selector = synth.param(slot.selectorID), slot.engineNames.count > 1 {
            EG13EnginePicker(names: slot.engineNames, selector: selector, selected: slot.engine,
                             accessibilityName: accessibilityName)
        }
    }
}

/// The master Mode: Single, Layer, Split, Multi as four LCD segments.
struct EG13ModeSwitch: View {
    let param: EG13Param
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(spacing: 2) {
            ForEach(EG13Mode.allCases, id: \.self) { mode in
                let selected = Int(param.value) == mode.rawValue
                Button { param.setValue(Double(mode.rawValue)) } label: {
                    Text(mode.label.uppercased())
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundStyle(selected ? EG13Palette.lcdBackground.color : EG13Palette.lcdSegment.color)
                        .padding(.horizontal, theme.spacing.s)
                        .frame(height: 24)
                        .background(RoundedRectangle(cornerRadius: 2)
                            .fill(selected ? EG13Palette.lcdSegment.color : Color.clear))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Mode \(mode.label)")
                .accessibilityAddTraits(selected ? [.isSelected, .isButton] : .isButton)
            }
        }
        .padding(2)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(EG13Palette.lcdBackground.color))
    }
}
