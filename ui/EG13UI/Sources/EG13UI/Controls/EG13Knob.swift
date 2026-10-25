import SwiftUI
import SymbioticTokens

/// Turns modulation features on in M4 (motion chips open "Modulate from…").
public enum EG13Features {
    public static let modulation = false
}

/// A knob for one parameter: EG13KnobShape for drawing, EG13ControlInteraction for
/// behaviour, VoiceOver and keyboard through real SwiftUI accessibility modifiers.
public struct EG13Knob: View {
    let param: EG13Param
    let style: EG13KnobStyle
    let size: EG13KnobSize
    let showsMotionChip: Bool

    public init(param: EG13Param, style: EG13KnobStyle, size: EG13KnobSize, showsMotionChip: Bool = false) {
        self.param = param
        self.style = style
        self.size = size
        self.showsMotionChip = showsMotionChip
    }

    public var body: some View {
        // A new param is a new control: its interaction state must not carry over.
        EG13KnobBody(param: param, style: style, size: size, showsMotionChip: showsMotionChip)
            .id(ObjectIdentifier(param))
    }

    public static func diameter(_ size: EG13KnobSize) -> CGFloat {
        switch size {
        case .hero: return EG13Layout.heroKnobDiameter
        case .detail: return EG13Layout.detailKnobDiameter
        case .bar: return EG13Layout.barKnobDiameter
        }
    }
}

private struct EG13KnobBody: View {
    let param: EG13Param
    let style: EG13KnobStyle
    let size: EG13KnobSize
    let showsMotionChip: Bool
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlColors) private var colors

    init(param: EG13Param, style: EG13KnobStyle, size: EG13KnobSize, showsMotionChip: Bool) {
        self.param = param
        self.style = style
        self.size = size
        self.showsMotionChip = showsMotionChip
        _interaction = State(initialValue: param.makeInteraction())
    }

    var body: some View {
        let diameter = EG13Knob.diameter(size)
        VStack(spacing: theme.spacing.xxs) {
            VStack(spacing: theme.spacing.xxs) {
                EG13KnobShape(normalized: param.normalized, origin: param.isBipolar ? 0.5 : 0,
                              style: style, size: size, isFocused: isFocused)
                    .frame(width: diameter, height: diameter)
                    .eg13ControlInput($interaction)
                EG13ControlLabel(text: param.name, style: style.label, chip: style.accentInk,
                                 font: theme.type.caption.font)
                if size == .hero {
                    EG13LCDText(text: param.text)
                        .frame(width: EG13Layout.heroReadoutWidth, height: 20)
                } else {
                    Text(param.text)
                        .font(theme.type.caption.font)
                        .foregroundStyle(colors.label)
                        .lineLimit(1)
                        .monospacedDigit()
                }
            }
            .modifier(EG13ControlAccess(interaction: $interaction, label: param.accessibilityLabel,
                                        valueText: param.text, focus: $isFocused))
            if showsMotionChip {
                EG13MotionChip(paramName: param.name)
            }
        }
        .onChange(of: param.descriptor) { _, _ in interaction.cancel(); interaction = param.makeInteraction() }
    }
}

/// The "MOTION" chip under a modulatable knob. Disabled until M4 builds the
/// "Modulate from…" popover; it never shows routes that do not exist.
struct EG13MotionChip: View {
    let paramName: String
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only

    var body: some View {
        Button {} label: {
            Text("MOTION")
                .font(theme.type.caption.font)
                .padding(.horizontal, theme.spacing.xs)
                .frame(height: EG13Layout.motionChipHeight)
                .overlay(Capsule().stroke(colors.tick, lineWidth: 0.6))
        }
        .buttonStyle(.plain)
        .foregroundStyle(colors.tick)
        .disabled(!EG13Features.modulation)
        .help("Modulation arrives in a later version")
        .accessibilityLabel("Modulate \(paramName)")
        .accessibilityHint("Not available yet")
    }
}
