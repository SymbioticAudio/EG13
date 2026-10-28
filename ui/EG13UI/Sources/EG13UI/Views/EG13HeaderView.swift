import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens
#if canImport(SymbioticBrand)
import SymbioticBrand
#endif

/// Header on the rack grey: the EG13 wordmark, the compact A/B part tabs in the part accents, the
/// Presets button and the MASTER group (Mode, Latency Mode, Split Note, Voice Gain, Soft Clip),
/// each with a small label. The cog opens the Settings and About panel. Under that row, the name
/// strip (`EG13PartNameStrip`) names what is loaded into each part (owner, header v2 round 2).
struct EG13HeaderView: View {
    @Bindable var synth: EG13Synth
    /// The cog was pressed (the root view shows the Settings and About panel over the editor).
    var onSettings: () -> Void = {}
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let part = synth.selectedPart
        let header = synth.editorControls(part: part).header
        VStack(spacing: 0) {
            HStack(spacing: theme.spacing.s) {
                wordmark.fixedSize()
                partTabs.fixedSize()
                EG13PresetBar(synth: synth)
                    .environment(\.eg13ControlColors, EG13Palette.headerControls)
                Spacer(minLength: 0)
                masterGroup(header, part: part)
                Button(action: onSettings) { Icon(.appSettings, size: 16) }
                    .buttonStyle(.plain)
                    .accessibilityLabel("Settings and About")
            }
            .padding(.horizontal, EG13Layout.outerPadding + 4)
            .frame(height: EG13Layout.headerHeight)
            EG13PartNameStrip(synth: synth)
        }
        .foregroundStyle(theme.text.inverse.color)
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

    /// Compact letter tabs; the names are in the strip under the header.
    private var partTabs: some View {
        HStack(spacing: theme.spacing.xs) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in
                let selected = synth.selectedPart == part
                Button { synth.selectedPart = part } label: {
                    EG13PartChip(part: part, selected: selected, size: 22)
                        .padding(.horizontal, theme.spacing.xs + 2)
                        .frame(height: 30)
                        .background(RoundedRectangle(cornerRadius: theme.radius.s)
                            .fill(selected ? EG13Palette.part(part).ink.color : Color.clear))
                        .overlay(RoundedRectangle(cornerRadius: theme.radius.s)
                            .stroke(EG13Palette.part(part).bright.color, lineWidth: selected ? 0 : 1))
                }
                .buttonStyle(.plain)
                .help(synth.presets.partHelp(part))
                .accessibilityLabel(EG13PartName.name(part))
                .accessibilityAddTraits(selected ? [.isSelected, .isButton] : .isButton)
            }
        }
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
        .fixedSize()  // keeps its full size; the preset bar gives way
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(EG13Palette.white.color.opacity(0.08)))
        .environment(\.eg13ControlColors, EG13Palette.headerControls)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Master settings")
    }
}

/// A part's letter in a small square: white with the part's ink when selected, the ink with white
/// text otherwise (the tabs and the name strip use the same chip).
struct EG13PartChip: View {
    let part: Int
    let selected: Bool
    var size: CGFloat = 22
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        Text(EG13PartName.letter(part))
            .font(theme.type.label.font.weight(.heavy))
            .foregroundStyle(selected ? EG13Palette.part(part).ink.color : theme.text.inverse.color)
            .frame(width: size, height: size)
            .background(RoundedRectangle(cornerRadius: 3)
                .fill(selected ? EG13Palette.white.color : EG13Palette.part(part).ink.color))
    }
}

/// The slim strip under the header row: one half per part, each its letter chip and the name of
/// what is loaded into the part (`EG13PresetBrowser.partName`), with "*" when the part is edited.
/// The shown part's half is filled like its tab; clicking a half shows that part. A long name is
/// cut with "…"; hover for the full name (and a credited preset's credit and licence).
struct EG13PartNameStrip: View {
    @Bindable var synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    /// The half's text: the name, then "*" if the part is edited.
    static func text(_ presets: EG13PresetBrowser, part: Int) -> String {
        presets.partName(part) + (presets.isEdited(part: part) ? "*" : "")
    }

    static func accessibilityLabel(_ presets: EG13PresetBrowser, part: Int) -> String {
        "\(EG13PartName.name(part)): \(presets.partName(part))\(presets.isEdited(part: part) ? ", edited" : "")"
    }

    var body: some View {
        HStack(spacing: theme.spacing.s) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in half(part) }
        }
        .padding(.horizontal, EG13Layout.outerPadding)
        .padding(.vertical, 2)
        .frame(height: EG13Layout.nameStripHeight)
        .overlay(alignment: .top) {
            Rectangle().fill(EG13Palette.white.color.opacity(0.08)).frame(height: 1)
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Loaded presets")
    }

    private func half(_ part: Int) -> some View {
        let presets = synth.presets
        let selected = synth.selectedPart == part
        return Button { synth.selectedPart = part } label: {
            HStack(spacing: theme.spacing.s) {
                EG13PartChip(part: part, selected: selected, size: 16)
                // The "*" is its own text, so a long name is cut before it and the mark always shows.
                HStack(spacing: 0) {
                    Text(presets.partName(part))
                        .lineLimit(1)
                        .truncationMode(.tail)
                    if presets.isEdited(part: part) { Text("*").fixedSize() }
                }
                .font(theme.type.caption.font.weight(.bold))
                Spacer(minLength: 0)
            }
            .padding(.horizontal, theme.spacing.xs)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: theme.radius.s)
                .fill(selected ? EG13Palette.part(part).ink.color : Color.clear))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .help(presets.partHelp(part))
        .accessibilityLabel(Self.accessibilityLabel(presets, part: part))
        .accessibilityHint("Shows this part")
        .accessibilityAddTraits(selected ? [.isSelected, .isButton] : .isButton)
    }
}
