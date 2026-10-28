import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens
#if canImport(SymbioticBrand)
import SymbioticBrand
#endif

/// Header on the rack grey: the EG13 wordmark, the A/B part tabs in the part accents (each names
/// what is loaded into its part, with "*" when that part is edited), the Presets button and the
/// MASTER group (Mode, Latency Mode, Split Note, Voice Gain, Soft Clip), each with a small label.
/// The cog opens the Settings and About panel.
struct EG13HeaderView: View {
    @Bindable var synth: EG13Synth
    /// The cog was pressed (the root view shows the Settings and About panel over the editor).
    var onSettings: () -> Void = {}
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let part = synth.selectedPart
        let header = synth.editorControls(part: part).header
        // No gap doubled around the Spacer: every point goes to the part tabs' names.
        HStack(spacing: 0) {
            wordmark.fixedSize()
                .padding(.trailing, theme.spacing.s)
            partTabs.layoutPriority(1)
                .padding(.trailing, theme.spacing.s)
            EG13PresetBar(synth: synth)
                .environment(\.eg13ControlColors, EG13Palette.headerControls)
            Spacer(minLength: theme.spacing.s)
            masterGroup(header, part: part)
                .padding(.trailing, theme.spacing.s)
            Button(action: onSettings) { Icon(.appSettings, size: 16) }
                .buttonStyle(.plain)
                .accessibilityLabel("Settings and About")
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

    /// The widest a tab's preset name gets before it is cut with "…" (the full name is the help).
    static let partNameMaxWidth: CGFloat = 150
    /// The MASTER group leaves the tabs little room at the editor's width, so the shown tab's name
    /// comes first; the other tab keeps at least this much of its name.
    static let otherPartNameMinWidth: CGFloat = 40

    private var partTabs: some View {
        HStack(spacing: theme.spacing.xs) {
            ForEach(0..<EG13ParamID.parts, id: \.self) { part in
                let selected = synth.selectedPart == part
                let presets = synth.presets
                let edited = presets.isEdited(part: part)
                Button { synth.selectedPart = part } label: {
                    HStack(spacing: theme.spacing.xs) {
                        Text(EG13PartName.letter(part))
                            .font(theme.type.label.font.weight(.heavy))
                            .foregroundStyle(selected ? EG13Palette.part(part).ink.color : theme.text.inverse.color)
                            .frame(width: 22, height: 22)
                            .background(RoundedRectangle(cornerRadius: 3)
                                .fill(selected ? EG13Palette.white.color : EG13Palette.part(part).ink.color))
                        // The "*" is its own text, so a long name is cut before it and the mark always shows.
                        EG13CappedWidth(maxWidth: Self.partNameMaxWidth,
                                        minWidth: selected ? 0 : Self.otherPartNameMinWidth) {
                            HStack(spacing: 0) {
                                Text(presets.partName(part))
                                    .lineLimit(1)
                                    .truncationMode(.tail)
                                if edited { Text("*").fixedSize() }
                            }
                        }
                        .font(theme.type.caption.font.weight(.bold))
                    }
                    .padding(.horizontal, theme.spacing.xs + 2)
                    .frame(height: 30)
                    .background(RoundedRectangle(cornerRadius: theme.radius.s)
                        .fill(selected ? EG13Palette.part(part).ink.color : Color.clear))
                    .overlay(RoundedRectangle(cornerRadius: theme.radius.s)
                        .stroke(EG13Palette.part(part).bright.color, lineWidth: selected ? 0 : 1))
                }
                .buttonStyle(.plain)
                .layoutPriority(selected ? 1 : 0)
                .help(presets.partHelp(part))
                .accessibilityLabel("\(EG13PartName.name(part)): \(presets.partName(part))\(edited ? ", edited" : "")")
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

/// Its content at its natural width, but never wider than `maxWidth` or the width offered (nor
/// narrower than `minWidth`, or its natural width if that is less): a one-line text in it is cut
/// with "…" past that, and a short one takes only the room it needs (a plain `.frame(maxWidth:)`
/// would grow to the maximum whenever the header has room).
struct EG13CappedWidth: Layout {
    var maxWidth: CGFloat
    var minWidth: CGFloat = 0

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        guard let content = subviews.first else { return .zero }
        return content.sizeThatFits(ProposedViewSize(width: width(content, proposal), height: proposal.height))
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        subviews.first?.place(at: bounds.origin, proposal: ProposedViewSize(width: bounds.width, height: bounds.height))
    }

    private func width(_ content: LayoutSubview, _ proposal: ProposedViewSize) -> CGFloat {
        let natural = content.sizeThatFits(.unspecified).width
        return max(min(natural, minWidth), min(natural, maxWidth, proposal.width ?? .infinity))
    }
}
