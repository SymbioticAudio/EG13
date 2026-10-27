import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// Bottom bar: the shown part's Level, Pan, Glide, Voice Mode and Voices, the Part settings
/// popover, master Dry and Out, and a stereo meter for each part in its accent colour.
struct EG13BottomBar: View {
    let synth: EG13Synth
    @State private var showsSettings = false
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let part = synth.selectedPart
        let controls = synth.editorControls(part: part)
        HStack(alignment: .center, spacing: theme.spacing.m) {
            Text(EG13PartName.letter(part))
                .font(theme.type.title.font.weight(.heavy))
                .foregroundStyle(theme.text.inverse.color)
                .frame(width: 26, height: 26)
                .background(RoundedRectangle(cornerRadius: 3).fill(EG13Palette.part(part).ink.color))
                .accessibilityHidden(true)
            ForEach(controls.partBar) { EG13ControlView(synth: synth, spec: $0, part: part) }
            Button { showsSettings = true } label: {
                HStack(spacing: theme.spacing.xs) {
                    Icon(.viewMore, size: 14)
                    Text("PART SETTINGS").font(theme.type.caption.font.weight(.bold))
                }
                .padding(.horizontal, theme.spacing.s)
                .frame(height: 26)
                .background(RoundedRectangle(cornerRadius: theme.radius.s).stroke(theme.text.secondary.color))
            }
            .buttonStyle(.plain)
            .foregroundStyle(theme.text.primary.color)
            .accessibilityLabel("\(EG13PartName.name(part)) settings")
            .popover(isPresented: $showsSettings) {
                EG13PartSettingsView(synth: synth, part: part)
            }
            Spacer(minLength: 0)
            Rectangle().fill(theme.surface.separator.color).frame(width: 1, height: 52)
            VStack(spacing: 2) {
                HStack(spacing: theme.spacing.s) {
                    ForEach(controls.masterBar) { EG13ControlView(synth: synth, spec: $0, part: part) }
                }
                Text("OUTPUT").font(theme.type.caption.font.weight(.bold)).foregroundStyle(theme.text.secondary.color)
            }
            EG13MeterView(synth: synth)
        }
        .padding(.horizontal, theme.spacing.m)
        .frame(height: EG13Layout.bottomBarHeight)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
        .padding(.horizontal, EG13Layout.outerPadding)
        .foregroundStyle(theme.text.primary.color)
    }
}

/// The settings that do not fit the bar: transpose, base note, MIDI channel, key range,
/// trigger, bend range, mono, output, input source, Part FX Input, MPE and the CC A to D
/// sources. The popover's content, also drawn on its own for the design screenshots.
public struct EG13PartSettingsView: View {
    let synth: EG13Synth
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    public init(synth: EG13Synth, part: Int) {
        self.synth = synth
        self.part = part
    }

    public var body: some View {
        let controls = synth.editorControls(part: part)
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            HStack(spacing: theme.spacing.s) {
                Text(EG13PartName.letter(part))
                    .font(theme.type.title.font.weight(.heavy))
                    .foregroundStyle(theme.text.inverse.color)
                    .frame(width: 26, height: 26)
                    .background(RoundedRectangle(cornerRadius: 3).fill(EG13Palette.part(part).ink.color))
                Text("\(EG13PartName.name(part)) settings".uppercased())
                    .font(theme.type.caption.font.weight(.bold))
                    .accessibilityAddTraits(.isHeader)
            }
            EG13KeyRangeView(synth: synth)
                .frame(width: 3 * (EG13Layout.stepperWidth + 56))
            EG13ControlGrid(columns: 3, specs: controls.partSettings, rowSpacing: theme.spacing.m) {
                EG13ControlView(synth: synth, spec: $0, part: part)
            }
            .frame(width: 3 * (EG13Layout.stepperWidth + 56))
        }
        .foregroundStyle(theme.text.primary.color)
        .padding(theme.spacing.l)
        .background(theme.surface.panel.color)
        .symbioticTheme(EG13Theme.theme)
    }
}

/// Peak meters: a stereo pair for each part, in the part's accent colour, with a peak-hold mark.
struct EG13MeterView: View {
    /// Read here, not in the bar: the meters change at 30 Hz and only this view rebuilds.
    let synth: EG13Synth
    /// Linear levels after ballistics: [part][left, right].
    private var meters: [[Float]] { synth.meters }
    /// Peak holds, same shape.
    private var holds: [[Float]] { synth.meterHolds }
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        HStack(alignment: .bottom, spacing: theme.spacing.m) {
            ForEach(meters.indices, id: \.self) { part in
                VStack(spacing: 3) {
                    HStack(spacing: 2) {
                        ForEach(0..<2, id: \.self) { channel in
                            bar(part: part, channel: channel)
                        }
                    }
                    Text(EG13PartName.letter(part)).font(theme.type.caption.font.weight(.bold))
                        .foregroundStyle(theme.text.primary.color)
                }
                .accessibilityElement(children: .ignore)
                .accessibilityLabel("\(EG13PartName.name(part)) output level")
                .accessibilityValue(meterValue(part))
            }
        }
    }

    private func bar(part: Int, channel: Int) -> some View {
        let level = meters[part][channel], hold = holds[part][channel]
        let height = EG13Layout.meterHeight
        let clipping = EG13MeterBallistics.isClipping(level)
        return ZStack(alignment: .bottom) {
            RoundedRectangle(cornerRadius: 2).fill(EG13Palette.lcdBackground.color)
            RoundedRectangle(cornerRadius: 2)
                .fill(clipping ? EG13Palette.white.color : EG13Palette.part(part).bright.color)
                .frame(height: height * EG13MeterBallistics.fraction(level))
            Rectangle().fill(EG13Palette.white.color)
                .frame(height: 1.5)
                .offset(y: -min(height - 1.5, height * EG13MeterBallistics.fraction(hold)))
        }
        .frame(width: EG13Layout.meterWidth, height: height)
        .clipShape(RoundedRectangle(cornerRadius: 2))
    }

    private func meterValue(_ part: Int) -> String {
        let db = EG13MeterBallistics.decibels(meters[part].max() ?? 0)
        return db <= EG13MeterBallistics.floorDB ? "Silent" : "\(Int(db.rounded())) dB"
    }
}
