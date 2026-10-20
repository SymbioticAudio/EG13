import SwiftUI
import SymbioticTokens

/// Turns modulation features on in M4 (motion chips open "Modulate from…").
public enum EG13Features {
    public static let modulation = false
}

/// A knob for one parameter. Shape, layers and options come from the environment's
/// EG13ControlStyle; its colours arrive resolved in `look`. Palette-agnostic.
public struct EG13Knob: View {
    let param: EG13Param
    let look: EG13KnobLook
    let size: EG13KnobSize
    let showsMotionChip: Bool

    public init(param: EG13Param, look: EG13KnobLook, size: EG13KnobSize, showsMotionChip: Bool = false) {
        self.param = param
        self.look = look
        self.size = size
        self.showsMotionChip = showsMotionChip
    }

    public var body: some View {
        // A new param is a new control: its interaction state must not carry over.
        EG13KnobBody(param: param, look: look, size: size, showsMotionChip: showsMotionChip)
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
    let look: EG13KnobLook
    let size: EG13KnobSize
    let showsMotionChip: Bool
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography and spacing only
    @Environment(\.eg13ControlStyle) private var style

    init(param: EG13Param, look: EG13KnobLook, size: EG13KnobSize, showsMotionChip: Bool) {
        self.param = param
        self.look = look
        self.size = size
        self.showsMotionChip = showsMotionChip
        _interaction = State(initialValue: EG13ControlInteraction(param: param))
    }

    var body: some View {
        let diameter = EG13Knob.diameter(size)
        VStack(spacing: theme.spacing.xxs) {
            VStack(spacing: theme.spacing.xxs) {
                style.knob(style.knobOptions.configuration(
                    normalized: param.normalized, origin: param.isBipolar ? 0.5 : 0,
                    look: look, size: size, isFocused: isFocused))
                    .frame(width: diameter, height: diameter)
                    .overlay(EG13ControlSurface(interaction: interaction))
                EG13ControlLabel(text: param.name, chip: style.knobOptions.chip(for: look),
                                 colors: style.colors, font: theme.type.caption.font)
                if size == .hero {
                    EG13LCDText(text: param.text, colors: style.colors)
                        .frame(width: EG13Layout.heroReadoutWidth, height: 20)
                } else {
                    Text(param.text)
                        .font(theme.type.caption.font)
                        .foregroundStyle(style.colors.label)
                        .lineLimit(1)
                        .monospacedDigit()
                }
            }
            .modifier(EG13ControlAccess(interaction: interaction, focus: $isFocused))
            if showsMotionChip {
                EG13MotionChip(paramName: param.name, colors: style.colors)
            }
        }
    }
}

/// A control's name in uppercase: plain, or on a chip with white text.
struct EG13ControlLabel: View {
    let text: String
    let chip: Color?
    let colors: EG13ControlColors
    let font: Font

    var body: some View {
        if let chip {
            Text(text.uppercased()).font(font.weight(.bold)).foregroundStyle(colors.chipText).lineLimit(1)
                .padding(.horizontal, 5)
                .background(RoundedRectangle(cornerRadius: 2).fill(chip))
        } else {
            Text(text.uppercased()).font(font.weight(.bold)).foregroundStyle(colors.label).lineLimit(1)
        }
    }
}

/// The "MOTION" chip under a modulatable knob. Disabled until M4 builds the
/// "Modulate from…" popover; it never shows routes that do not exist.
struct EG13MotionChip: View {
    let paramName: String
    let colors: EG13ControlColors
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
