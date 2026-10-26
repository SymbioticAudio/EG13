import SwiftUI
import SymbioticTokens

/// A knob for one parameter: EG13KnobShape for drawing, EG13ControlInteraction for
/// behaviour, VoiceOver and keyboard through real SwiftUI accessibility modifiers.
public struct EG13Knob: View {
    let param: EG13Param
    let style: EG13KnobStyle
    let size: EG13KnobSize
    let layout: EG13KnobLayout

    /// - Parameter layout: the label other than the parameter's name, the width the label and the
    ///   value text may take (longer text wraps), and the box the knob is centred in. The knob's
    ///   drawing is the approved design in every case.
    public init(param: EG13Param, style: EG13KnobStyle, size: EG13KnobSize,
                layout: EG13KnobLayout = EG13KnobLayout()) {
        self.param = param
        self.style = style
        self.size = size
        self.layout = layout
    }

    public var body: some View {
        // A new param is a new control: its interaction state must not carry over.
        EG13KnobBody(param: param, style: style, size: size, layout: layout)
            .id(ObjectIdentifier(param))
    }

    public static func diameter(_ size: EG13KnobSize) -> CGFloat { EG13Layout.knobDiameter(size) }
}

/// Layout-only options of a knob: a label other than the short name, the width its label may
/// take (longer labels wrap), and the height of the box the knob sits centred in, so knobs of
/// different sizes in one row share a label line. The knob's drawing is the approved design.
public struct EG13KnobLayout: Equatable, Sendable {
    public var label: String?
    public var labelWidth: CGFloat?
    public var boxHeight: CGFloat?

    public init(label: String? = nil, labelWidth: CGFloat? = nil, boxHeight: CGFloat? = nil) {
        self.label = label
        self.labelWidth = labelWidth
        self.boxHeight = boxHeight
    }
}

private struct EG13KnobBody: View {
    let param: EG13Param
    let style: EG13KnobStyle
    let size: EG13KnobSize
    let layout: EG13KnobLayout
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlColors) private var colors

    init(param: EG13Param, style: EG13KnobStyle, size: EG13KnobSize, layout: EG13KnobLayout) {
        self.param = param
        self.style = style
        self.size = size
        self.layout = layout
        _interaction = State(initialValue: param.makeInteraction())
    }

    var body: some View {
        let diameter = EG13Knob.diameter(size)
        VStack(spacing: theme.spacing.xxs) {
            VStack(spacing: theme.spacing.xxs) {
                EG13KnobShape(normalized: param.normalized, origin: param.isBipolar ? 0.5 : 0,
                              style: style, size: size, isFocused: isFocused)
                    .overlay { EG13LiveRing(param: param, style: style, size: size) }
                    .frame(width: diameter, height: diameter)
                    .eg13ControlInput($interaction)
                    .frame(height: max(diameter, layout.boxHeight ?? diameter))
                EG13ControlLabel(text: layout.label ?? EG13ParamLabels.short(param.name), style: style.label,
                                 chip: style.accentInk, font: theme.type.caption.font, width: layout.labelWidth)
                EG13ValueText(text: param.text, font: theme.type.caption.font, width: layout.labelWidth)
            }
            .modifier(EG13ControlAccess(interaction: $interaction, label: param.accessibilityLabel,
                                        valueText: param.text, focus: $isFocused))
            .help("\(param.name): \(param.text)")
        }
        .onChange(of: param.descriptor) { _, _ in interaction.cancel(); interaction = param.makeInteraction() }
    }
}

/// The modulation ring of a knob, reading the ring itself: a modulation tick rebuilds this view
/// and nothing else of the knob, and only for a modulated knob.
private struct EG13LiveRing: View {
    let param: EG13Param
    let style: EG13KnobStyle
    let size: EG13KnobSize

    var body: some View {
        if style.arc, let ring = param.modRing {
            EG13ModRingLayer(ring: ring, style: style, size: size)
        }
    }
}

/// The value text under a knob: the short form of the core's text (EG13ValueLabels), on one line
/// or wrapped onto two in `width`. Never cut with "…" and never shrunk; the full text is in the
/// tooltip and for VoiceOver.
struct EG13ValueText: View {
    let text: String
    let font: Font
    /// The most width the text may take; nil for one natural line.
    var width: CGFloat?
    @Environment(\.eg13ControlColors) private var colors

    var body: some View {
        let lines = width.map { EG13ValueFit.lines(text, width: $0).lines } ?? [EG13ValueLabels.short(text)]
        VStack(spacing: 0) {
            ForEach(lines.indices, id: \.self) { index in
                Text(lines[index])
                    .font(font)
                    .foregroundStyle(colors.label)
                    .lineLimit(1)
                    .fixedSize()
                    .monospacedDigit()
            }
        }
    }
}
