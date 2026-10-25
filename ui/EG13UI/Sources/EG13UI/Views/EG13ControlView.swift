import EG13CBridge
import SwiftUI
import SymbioticTokens

/// Draws one control spec with the matching approved control: a knob in its emphasis style,
/// a stepper, the engine menu or the Mode switch. A spec whose parameter the plugin does not
/// have draws nothing.
struct EG13ControlView: View {
    let synth: EG13Synth
    let spec: EG13ControlSpec
    let part: Int
    /// For a knob in a grid: the cell's width (the label wraps within it) and the row's knob box.
    var cell: CGFloat?
    var boxHeight: CGFloat?

    var body: some View {
        if let param = synth.param(spec.id) {
            switch spec.kind {
            case .knob(let size):
                EG13Knob(param: param, style: style, size: size,
                         layout: EG13KnobLayout(label: spec.label, labelWidth: cell.map(EG13Layout.labelWidth(cell:)),
                                                boxHeight: boxHeight))
            case .stepped:
                EG13Stepped(param: param, label: spec.label)
            case .wideStepped:
                EG13Stepped(param: param, label: spec.label, wellWidth: EG13Layout.wideStepperWidth)
            case .scope:
                EG13Stepped(param: param, label: spec.label, wellWidth: EG13Layout.wideStepperWidth, showsLabel: false)
            case .segmented:
                EG13ModeSwitch(param: param)
            case .enginePicker(let kind):
                enginePicker(kind)
            }
        }
    }

    private var style: EG13KnobStyle { EG13Palette.knobStyle(for: spec.emphasis, part: part) }

    /// The slot's engine menu, drawn from the spec, so a card that drops the spec drops the menu.
    @ViewBuilder private func enginePicker(_ kind: EG13SlotKind) -> some View {
        let slot = synth.slot(kind, part: part)
        if let selector = synth.param(slot.selectorID), slot.engineNames.count > 1 {
            EG13EnginePicker(names: slot.engineNames, selector: selector, selected: slot.engine,
                             accessibilityName: "\(EG13PartName.name(part)) \(EG13ModuleCard.title(kind)) engine",
                             wellWidth: EG13Layout.enginePickerWidth)
        }
    }
}

/// Knobs in balanced rows of equal cells (`EG13Layout.gridShape`); knobs of different sizes in a
/// row sit centred in a box as tall as the row's largest, so their labels share a line.
struct EG13KnobGrid: View {
    let synth: EG13Synth
    let specs: [EG13ControlSpec]
    let part: Int
    let width: CGFloat
    let minCell: CGFloat
    var rowSpacing: CGFloat = EG13Layout.rowGap

    static func boxHeight(_ row: [EG13ControlSpec]) -> CGFloat {
        row.map { spec -> CGFloat in
            if case .knob(let size) = spec.kind { return EG13Layout.knobDiameter(size) }
            return 0
        }.max() ?? 0
    }

    var body: some View {
        let shape = EG13Layout.gridShape(count: specs.count, width: width, minCell: minCell)
        let rows = stride(from: 0, to: specs.count, by: shape.columns).map {
            Array(specs[$0..<min($0 + shape.columns, specs.count)])
        }
        VStack(alignment: .leading, spacing: rowSpacing) {
            ForEach(rows.indices, id: \.self) { row in
                let box = Self.boxHeight(rows[row])
                HStack(alignment: .top, spacing: 0) {
                    ForEach(rows[row]) { spec in
                        EG13ControlView(synth: synth, spec: spec, part: part, cell: shape.cell, boxHeight: box)
                            .frame(width: shape.cell)
                    }
                }
            }
        }
        .frame(width: width, alignment: .leading)
    }
}

/// Cells of a row for the bar, the matrix and the popovers: equal widths, left-aligned last row.
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

/// A row of LCD segments, one lit: the master Mode, and variant B's VOICE / MODS switch.
struct EG13LCDSegments: View {
    let labels: [String]
    let selected: Int
    let accessibilityPrefix: String
    let select: (Int) -> Void
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(spacing: 2) {
            ForEach(labels.indices, id: \.self) { index in
                let isSelected = selected == index
                Button { select(index) } label: {
                    Text(labels[index].uppercased())
                        .font(.system(size: 11, weight: .bold, design: .monospaced))
                        .foregroundStyle(isSelected ? EG13Palette.lcdBackground.color : EG13Palette.lcdSegment.color)
                        .padding(.horizontal, theme.spacing.s)
                        .frame(height: 24)
                        .background(RoundedRectangle(cornerRadius: 2)
                            .fill(isSelected ? EG13Palette.lcdSegment.color : Color.clear))
                }
                .buttonStyle(.plain)
                .accessibilityLabel("\(accessibilityPrefix) \(labels[index])")
                .accessibilityAddTraits(isSelected ? [.isSelected, .isButton] : .isButton)
            }
        }
        .padding(2)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(EG13Palette.lcdBackground.color))
    }
}

/// The master Mode: Single, Layer, Split, Multi as four LCD segments.
struct EG13ModeSwitch: View {
    let param: EG13Param

    var body: some View {
        EG13LCDSegments(labels: EG13Mode.allCases.map(\.label), selected: Int(param.value),
                        accessibilityPrefix: "Mode") { param.setValue(Double($0)) }
    }
}
