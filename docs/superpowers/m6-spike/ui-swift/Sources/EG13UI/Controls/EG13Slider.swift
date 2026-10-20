import SwiftUI
import SymbioticTokens

/// A fader for one parameter, drawn by the environment's EG13ControlStyle.
public struct EG13Slider: View {
    let param: EG13Param
    let arc: Color
    let isVertical: Bool
    let length: CGFloat

    public init(param: EG13Param, arc: Color, isVertical: Bool = true, length: CGFloat) {
        self.param = param
        self.arc = arc
        self.isVertical = isVertical
        self.length = length
    }

    public var body: some View {
        EG13SliderBody(param: param, arc: arc, isVertical: isVertical, length: length)
            .id(ObjectIdentifier(param))
    }
}

private struct EG13SliderBody: View {
    let param: EG13Param
    let arc: Color
    let isVertical: Bool
    let length: CGFloat
    @State private var interaction: EG13ControlInteraction
    @FocusState private var isFocused: Bool
    @Environment(\.symbioticTheme) private var theme  // typography only
    @Environment(\.eg13ControlStyle) private var style

    init(param: EG13Param, arc: Color, isVertical: Bool, length: CGFloat) {
        self.param = param
        self.arc = arc
        self.isVertical = isVertical
        self.length = length
        _interaction = State(initialValue: EG13ControlInteraction(param: param))
    }

    var body: some View {
        VStack(spacing: 0) {
            style.slider(EG13SliderConfiguration(
                normalized: param.normalized, origin: param.isBipolar ? 0.5 : 0, arc: arc,
                isVertical: isVertical, isFocused: isFocused, isEnabled: true))
                .frame(width: isVertical ? 24 : length, height: isVertical ? length : 24)
                .overlay(EG13ControlSurface(interaction: interaction))
            EG13ControlLabel(text: param.name, chip: nil, colors: style.colors, font: theme.type.caption.font)
        }
        .modifier(EG13ControlAccess(interaction: interaction, focus: $isFocused))
    }
}
