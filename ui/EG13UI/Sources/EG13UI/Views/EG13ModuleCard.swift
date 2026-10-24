import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// A module column card (spec §8, owner 2026-10-20): a light header strip with a small
/// uppercase category label, a lowercase engine title and the engine selector; a hero
/// area; an icon band; a detail panel. Categories are not colour-coded.
struct EG13ModuleCard<Selector: View, Hero: View, Detail: View>: View {
    let category: EG13Category
    let engineTitle: String
    let icon: IconRole
    var isPlaceholder = false
    @ViewBuilder let selector: () -> Selector
    @ViewBuilder let hero: () -> Hero
    @ViewBuilder let detail: () -> Detail
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: theme.spacing.s) {
                VStack(alignment: .leading, spacing: 0) {
                    Text(category.title)
                        .font(theme.type.caption.font.weight(.bold))
                        .foregroundStyle(theme.text.secondary.color)
                        .accessibilityAddTraits(.isHeader)
                    Text(engineTitle.lowercased())
                        .font(.system(size: theme.type.title.size, weight: .light))
                        .foregroundStyle(theme.text.primary.color)
                        .lineLimit(1)
                }
                Spacer(minLength: 0)
                selector()
            }
            .padding(.horizontal, theme.spacing.s)
            .frame(height: EG13Layout.cardHeaderHeight)
            .background(theme.surface.raised.color)
            hero()
                .padding(.vertical, theme.spacing.s)
            Icon(icon, size: 20)
                .foregroundStyle(theme.text.secondary.color)
                .frame(maxWidth: .infinity, minHeight: EG13Layout.iconBandHeight)
            detail()
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
                .padding(theme.spacing.s)
        }
        .frame(width: EG13Layout.cardWidth())
        .frame(maxHeight: .infinity)
        .background(isPlaceholder ? EG13Palette.placeholderPanel.color : theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(category.title.capitalized): \(engineTitle)")
    }
}

/// A slot that has no engines yet: labelled, empty, and no fake controls.
struct EG13PlaceholderCard: View {
    let category: EG13Category
    let icon: IconRole
    let note: String
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        EG13ModuleCard(category: category, engineTitle: "not yet", icon: icon, isPlaceholder: true) {
            EmptyView()
        } hero: {
            Text(note)
                .font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
                .frame(height: EG13Layout.heroKnobDiameter)
        } detail: {
            EmptyView()
        }
    }
}
