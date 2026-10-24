import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// Header: wordmark, patch name with previous/next/save (presets arrive in M7), mode,
/// A/B part tabs, settings (zoom) and about.
struct EG13HeaderView: View {
    @Bindable var synth: EG13Synth
    @State private var showsAbout = false
    @Environment(\.symbioticTheme) private var theme

    static let zoomChoices: [Double] = [0.75, 1.0, 1.25, 1.5, 2.0]

    var body: some View {
        HStack(spacing: theme.spacing.l) {
            Text(synth.kind == .effect ? "EG13 FX" : "EG13")
                .font(theme.type.title.font)
                .accessibilityAddTraits(.isHeader)
            patch
            modePicker
            Spacer(minLength: 0)
            partTabs
            Menu {
                ForEach(Self.zoomChoices, id: \.self) { zoom in
                    Button("\(Int(zoom * 100))%") { synth.requestZoom(zoom) }
                }
            } label: { Icon(.appSettings, size: 16) }
                .menuStyle(.borderlessButton)
                .fixedSize()
                .accessibilityLabel("Settings: window size")
            Button { showsAbout = true } label: { Icon(.appAbout, size: 16) }
                .buttonStyle(.plain)
                .accessibilityLabel("About EG13")
                .popover(isPresented: $showsAbout) { about }
        }
        .foregroundStyle(theme.text.inverse.color)
        .padding(.horizontal, EG13Layout.outerPadding)
        .frame(height: EG13Layout.headerHeight)
        .background(theme.surface.inset.color)
    }

    private var patch: some View {
        HStack(spacing: theme.spacing.xs) {
            Button {} label: { Icon(.presetPrevious, size: 12) }.disabled(true)
                .accessibilityLabel("Previous patch")
            EG13LCDText(text: synth.patchName, colors: EG13Palette.controls)
                .frame(width: 160, height: 26)
                .accessibilityLabel("Patch \(synth.patchName)")
            Button {} label: { Icon(.presetNext, size: 12) }.disabled(true)
                .accessibilityLabel("Next patch")
            Button {} label: { Icon(.presetSave, size: 12) }.disabled(true)
                .accessibilityLabel("Save patch")
        }
        .buttonStyle(.plain)
        .help("Presets arrive in a later version")
    }

    @ViewBuilder
    private var modePicker: some View {
        if let mode = synth.masterParam(.mode) {
            Picker("Mode", selection: Binding(
                get: { EG13Mode(rawValue: Int(mode.value)) ?? .single },
                set: { mode.setValue(Double($0.rawValue)) })) {
                ForEach(EG13Mode.allCases, id: \.self) { Text($0.label).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .fixedSize()
            .accessibilityLabel("Mode")
        }
    }

    private var partTabs: some View {
        HStack(spacing: theme.spacing.xs) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in
                Button { synth.selectedPart = part } label: {
                    HStack(spacing: theme.spacing.xs) {
                        Text(EG13PartName.letter(part))
                            .font(theme.type.label.font.weight(.heavy))
                            .frame(width: 20, height: 20)
                            .background(RoundedRectangle(cornerRadius: 3).fill(EG13Palette.part(part).ink.color))
                        Text(synth.sourceEngineName(part: part)).font(theme.type.caption.font)
                    }
                    .padding(.horizontal, theme.spacing.s)
                    .frame(height: 34)
                    .background(RoundedRectangle(cornerRadius: theme.radius.s)
                        .stroke(theme.text.inverse.color.opacity(synth.selectedPart == part ? 0.5 : 0.2)))
                }
                .buttonStyle(.plain)
                .accessibilityLabel(EG13PartName.name(part))
                .accessibilityAddTraits(synth.selectedPart == part ? [.isSelected] : [])
            }
        }
    }

    private var about: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text(synth.kind == .effect ? "EG13 FX" : "EG13").font(theme.type.title.font)
            Text("DSP based on the open-source Eurorack firmware by Émilie Gillet.")
            Text("GPL-3.0. github.com/SymbioticAudio/EG13")
        }
        .font(theme.type.body.font)
        .padding(theme.spacing.l)
        .frame(width: 320)
    }
}
