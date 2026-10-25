import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens
#if canImport(SymbioticBrand)
import SymbioticBrand
#endif

/// Header on the rack grey: the EG13 wordmark, the A/B part tabs in the part accents, the
/// master Mode, and the master settings (Split Note, Voice Gain, Soft Clip, Latency Mode and
/// the window size) and About.
struct EG13HeaderView: View {
    @Bindable var synth: EG13Synth
    @State private var showsAbout = false
    @State private var showsMaster = false
    @Environment(\.symbioticTheme) private var theme

    static let zoomChoices: [Double] = [0.75, 1.0, 1.25, 1.5, 2.0]

    var body: some View {
        let part = synth.selectedPart
        HStack(spacing: theme.spacing.l) {
            wordmark
            partTabs
            Spacer(minLength: 0)
            ForEach(synth.editorControls(part: part).header) { EG13ControlView(synth: synth, spec: $0, part: part) }
            Button { showsMaster = true } label: { Icon(.appSettings, size: 16) }
                .buttonStyle(.plain)
                .accessibilityLabel("Master settings")
                .popover(isPresented: $showsMaster) { masterSettings }
            Button { showsAbout = true } label: { Icon(.appAbout, size: 16) }
                .buttonStyle(.plain)
                .accessibilityLabel("About EG13")
                .popover(isPresented: $showsAbout) { about }
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
                    HStack(spacing: theme.spacing.xs) {
                        Text(EG13PartName.letter(part))
                            .font(theme.type.label.font.weight(.heavy))
                            .foregroundStyle(selected ? EG13Palette.part(part).ink.color : theme.text.inverse.color)
                            .frame(width: 22, height: 22)
                            .background(RoundedRectangle(cornerRadius: 3)
                                .fill(selected ? EG13Palette.white.color : EG13Palette.part(part).ink.color))
                        Text(synth.slot(EG13_SLOT_KIND_SOURCE, part: part).engineName)
                            .font(theme.type.caption.font.weight(.bold))
                            .lineLimit(1)
                    }
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

    private var masterSettings: some View {
        let part = synth.selectedPart
        let controls = synth.editorControls(part: part)
        return VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text("MASTER SETTINGS").font(theme.type.caption.font.weight(.bold))
                .accessibilityAddTraits(.isHeader)
            EG13ControlGrid(columns: 2, specs: controls.masterSettings, rowSpacing: theme.spacing.m) {
                EG13ControlView(synth: synth, spec: $0, part: part)
            }
            .frame(width: 2 * (EG13Layout.stepperWidth + 56))
            VStack(alignment: .leading, spacing: theme.spacing.xs) {
                Text("WINDOW SIZE").font(theme.type.caption.font.weight(.bold))
                HStack(spacing: theme.spacing.xs) {
                    ForEach(Self.zoomChoices, id: \.self) { zoom in
                        Button("\(Int(zoom * 100))%") { synth.requestZoom(zoom) }
                    }
                }
            }
        }
        .foregroundStyle(theme.text.primary.color)
        .padding(theme.spacing.l)
        .background(theme.surface.panel.color)
        .symbioticTheme(EG13Theme.theme)
    }

    private var about: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text(title).font(theme.type.title.font)
            Text("DSP based on the open-source Eurorack firmware by Émilie Gillet.")
            Text("GPL-3.0. github.com/SymbioticAudio/EG13")
        }
        .font(theme.type.body.font)
        .padding(theme.spacing.l)
        .frame(width: 320)
    }
}
