import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// Bottom bar: part settings of the shown part on the left; master Dry and Out and the
/// part meters on the right (owner, 2026-10-20).
struct EG13BottomBar: View {
    let synth: EG13Synth
    @State private var showsMore = false
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let part = synth.selectedPart
        let plain = EG13Palette.look(for: .plain, part: part)
        HStack(spacing: theme.spacing.s) {
            Text(EG13PartName.letter(part))
                .font(theme.type.title.font.weight(.heavy))
                .foregroundStyle(theme.text.inverse.color)
                .frame(width: 26, height: 26)
                .background(RoundedRectangle(cornerRadius: 3).fill(EG13Palette.part(part).ink.color))
                .accessibilityHidden(true)
            stepper(part, .voiceCount, "Voices")
            stepper(part, .voiceMode, "Voice mode")
            stepper(part, .midiChannel, "Channel")
            stepper(part, .output, "Output")
            Button { showsMore = true } label: { Icon(.viewMore, size: 16) }
                .buttonStyle(.plain)
                .foregroundStyle(theme.text.primary.color)
                .accessibilityLabel("\(EG13PartName.name(part)) more settings")
                .popover(isPresented: $showsMore) { more(part) }
            knob(synth.partParam(part, .glide), plain)
            knob(synth.partParam(part, .transpose), plain)
            knob(synth.partParam(part, .level), plain)
            knob(synth.partParam(part, .pan), plain)
            Spacer(minLength: 0)
            Rectangle().fill(theme.surface.separator.color).frame(width: 1, height: 52)
            knob(synth.masterParam(.dry), EG13Palette.masterLook)
            knob(synth.masterParam(.level), EG13Palette.masterLook)
            EG13MeterView(meters: synth.meters)
        }
        .padding(.horizontal, EG13Layout.outerPadding)
        .frame(height: EG13Layout.bottomBarHeight)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
        .padding(.horizontal, EG13Layout.outerPadding)
    }

    @ViewBuilder
    private func stepper(_ part: Int, _ p: EG13PartParam, _ label: String) -> some View {
        if let param = synth.partParam(part, p) {
            EG13Stepper(param: param, label: label)
        }
    }

    @ViewBuilder
    private func knob(_ param: EG13Param?, _ look: EG13KnobLook) -> some View {
        if let param {
            EG13Knob(param: param, look: look, size: .bar)
        }
    }

    /// Settings that do not fit the bar: key range, trigger, base note, bend range, mono.
    private func more(_ part: Int) -> some View {
        Grid(horizontalSpacing: theme.spacing.l, verticalSpacing: theme.spacing.m) {
            GridRow {
                stepper(part, .keyLow, "Key low")
                stepper(part, .keyHigh, "Key high")
                stepper(part, .trigger, "Trigger")
            }
            GridRow {
                stepper(part, .baseNote, "Base note")
                stepper(part, .bendRange, "Bend range")
                stepper(part, .mono, "Mono")
            }
        }
        .padding(theme.spacing.l)
        .background(theme.surface.panel.color)
        .environment(\.eg13ControlStyle, EG13Theme.controlStyle)
        .symbioticTheme(EG13Theme.theme)
    }
}

/// Peak meters for both parts, on the LCD.
struct EG13MeterView: View {
    let meters: [EG13MeterBallistics]
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(alignment: .bottom, spacing: theme.spacing.xs) {
            ForEach(meters.indices, id: \.self) { part in
                VStack(spacing: 3) {
                    ZStack(alignment: .bottom) {
                        RoundedRectangle(cornerRadius: 2).fill(EG13Palette.lcdBackground.color)
                        RoundedRectangle(cornerRadius: 2)
                            .fill(meters[part].isClipping ? EG13Palette.pink.bright.color : EG13Palette.lcdSegment.color)
                            .frame(height: EG13Layout.meterHeight * meters[part].fraction)
                    }
                    .frame(width: EG13Layout.meterWidth, height: EG13Layout.meterHeight)
                    Text(EG13PartName.letter(part)).font(theme.type.caption.font.weight(.bold))
                        .foregroundStyle(theme.text.primary.color)
                }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("\(EG13PartName.name(part)) output level")
                .accessibilityValue(meters[part].levelDB <= EG13MeterBallistics.floorDB
                    ? "Silent" : "\(Int(meters[part].levelDB.rounded())) dB")
            }
        }
    }
}
