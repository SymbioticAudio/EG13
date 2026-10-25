import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens
#if canImport(SymbioticBrand)
import SymbioticBrand
#endif

/// Header on the rack grey: the EG13 wordmark, the A/B part tabs in the part accents (the part's Source engine is on its card), in
/// variant B the VOICE / MODS switch, and the MASTER group (Mode, Latency Mode, Split Note,
/// Voice Gain, Soft Clip), each with a small label. The cog holds the window size and About.
struct EG13HeaderView: View {
    @Bindable var synth: EG13Synth
    let variant: EG13EditorVariant
    @State private var showsSettings = false
    @Environment(\.symbioticTheme) private var theme

    static let zoomChoices: [Double] = [0.75, 1.0, 1.25, 1.5, 2.0]

    var body: some View {
        let part = synth.selectedPart
        let header = synth.editorControls(part: part).header
        HStack(spacing: theme.spacing.m) {
            wordmark
            partTabs
            if variant == .modsPage { pageSwitch }
            Spacer(minLength: 0)
            masterGroup(header, part: part)
            Button { showsSettings = true } label: { Icon(.appSettings, size: 16) }
                .buttonStyle(.plain)
                .accessibilityLabel("Settings and About")
                .popover(isPresented: $showsSettings) { settings }
        }
        .foregroundStyle(theme.text.inverse.color)
        .padding(.horizontal, EG13Layout.outerPadding + 4)
        .frame(height: EG13Layout.headerHeight)
        .background(theme.surface.inset.color)
    }

    private var title: String { synth.kind == .effect ? "EG13 FX" : "EG13" }

    /// The brand glyph when SymbioticBrand is linked (forks build without it), then the name.
    private var wordmark: some View {
        HStack(spacing: theme.spacing.s) {
            #if canImport(SymbioticBrand)
            Icon(.brandLogo, size: 22)
            #endif
            Text(title)
                .font(theme.type.title.font.weight(.semibold))
                .accessibilityAddTraits(.isHeader)
        }
    }

    private var partTabs: some View {
        HStack(spacing: theme.spacing.xs) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in
                let selected = synth.selectedPart == part
                Button { synth.selectedPart = part } label: {
                    Text(EG13PartName.letter(part))
                        .font(theme.type.label.font.weight(.heavy))
                        .foregroundStyle(selected ? EG13Palette.part(part).ink.color : theme.text.inverse.color)
                        .frame(width: 22, height: 22)
                        .background(RoundedRectangle(cornerRadius: 3)
                            .fill(selected ? EG13Palette.white.color : EG13Palette.part(part).ink.color))
                    .padding(.horizontal, theme.spacing.s)
                    .frame(height: 30)
                    .background(RoundedRectangle(cornerRadius: theme.radius.s)
                        .fill(selected ? EG13Palette.part(part).ink.color : Color.clear))
                    .overlay(RoundedRectangle(cornerRadius: theme.radius.s)
                        .stroke(EG13Palette.part(part).bright.color, lineWidth: selected ? 0 : 1))
                }
                .buttonStyle(.plain)
                .accessibilityLabel(EG13PartName.name(part))
                .accessibilityAddTraits(selected ? [.isSelected, .isButton] : .isButton)
            }
        }
    }

    /// Variant B: the slot cards or the Mods, each with the full width.
    private var pageSwitch: some View {
        EG13LCDSegments(labels: ["Voice", "Mods"], selected: synth.showsMods ? 1 : 0,
                        accessibilityPrefix: "Show") { synth.showsMods = $0 == 1 }
    }

    /// The master settings in a labelled group, so it reads as "these are global".
    private func masterGroup(_ controls: [EG13ControlSpec], part: Int) -> some View {
        HStack(alignment: .center, spacing: theme.spacing.m) {
            Text("MASTER")
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(theme.text.inverse.color.opacity(0.6))
                .accessibilityHidden(true)
            ForEach(controls) { spec in
                if spec.kind == .segmented {
                    VStack(spacing: 2) {
                        EG13ControlView(synth: synth, spec: spec, part: part)
                        Text("MODE").font(theme.type.caption.font.weight(.bold))
                    }
                } else {
                    EG13ControlView(synth: synth, spec: spec, part: part)
                }
            }
        }
        .padding(.horizontal, theme.spacing.m)
        .padding(.vertical, 4)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(EG13Palette.white.color.opacity(0.08)))
        .environment(\.eg13ControlColors, EG13Palette.headerControls)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Master settings")
    }

    private var settings: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            VStack(alignment: .leading, spacing: theme.spacing.xs) {
                Text("WINDOW SIZE").font(theme.type.caption.font.weight(.bold))
                    .accessibilityAddTraits(.isHeader)
                HStack(spacing: theme.spacing.xs) {
                    ForEach(Self.zoomChoices, id: \.self) { zoom in
                        Button("\(Int(zoom * 100))%") { synth.requestZoom(zoom) }
                    }
                }
            }
            Divider()
            VStack(alignment: .leading, spacing: theme.spacing.s) {
                Text("ABOUT").font(theme.type.caption.font.weight(.bold))
                    .accessibilityAddTraits(.isHeader)
                Text(title).font(theme.type.title.font)
                Text("DSP based on the open-source Eurorack firmware by Émilie Gillet.")
                Text("GPL-3.0. github.com/SymbioticAudio/EG13")
            }
            .font(theme.type.body.font)
        }
        .foregroundStyle(theme.text.primary.color)
        .padding(theme.spacing.l)
        .frame(width: 320, alignment: .leading)
        .background(theme.surface.panel.color)
        .symbioticTheme(EG13Theme.theme)
    }
}
