import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// An LCD-style menu for one field of a route: the closed well shows the short text of the choice
/// ("M2 ENV", "FLT CUTOFF"); the items carry the full names, grouped by card (destinations) or
/// family (sources), with "None" first. Choosing is one `setRoute` call (a hidden field of the
/// same route is cleared by the model). The tooltip and VoiceOver have the full name.
struct EG13RouteMenu: View {
    let synth: EG13Synth
    let model: EG13RouteMenuModel
    let width: CGFloat
    @Environment(\.eg13InteractiveControls) private var interactive
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography and radii only

    static let height: CGFloat = 24
    static let chevron: CGFloat = 8

    /// The width the closed text may take in a well of `width`: less the padding each side and the chevron.
    static func textRoom(width: CGFloat, padding: CGFloat) -> CGFloat { width - 2 * padding - chevron - 4 }

    static func font(_ theme: Theme) -> Font {
        .system(size: max(theme.type.caption.size, 11), weight: .bold, design: .monospaced)
    }

    var body: some View {
        Group {
            if interactive {
                Menu { items } label: { well }
                    .menuStyle(.borderlessButton)
                    .menuIndicator(.hidden)
            } else {
                well  // an image render cannot draw the AppKit menu: its closed state
            }
        }
        .fixedSize()
        .help("\(Self.fieldName(model.field)): \(model.fullText)")
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Route \(model.route + 1) \(Self.fieldName(model.field).lowercased())")
        .accessibilityValue(model.fullText)
    }

    static func fieldName(_ field: EG13RouteField) -> String {
        switch field {
        case .source: return "Source"
        case .via: return "Via"
        case .destination: return "Destination"
        }
    }

    @ViewBuilder private var items: some View {
        choice("None", code: 0)
        ForEach(model.groups) { group in groupMenu(group) }
    }

    private func groupMenu(_ group: EG13RouteGroup) -> AnyView {
        AnyView(Menu(group.title) {
            ForEach(group.choices) { item in choice(item.name, code: item.code) }
            ForEach(group.subgroups) { sub in groupMenu(sub) }
        })
    }

    // A Toggle in a menu is drawn with the menu's own check mark (menu state, not an image).
    private func choice(_ title: String, code: UInt16) -> some View {
        Toggle(title, isOn: Binding(get: { code == model.selected },
                                    set: { _ in synth.chooseRoute(part: model.part, route: model.route,
                                                                  field: model.field, code: code) }))
    }

    private var well: some View {
        HStack(spacing: 4) {
            Text(model.closedText.uppercased())
                .font(Self.font(theme))
                .lineLimit(1)
                .fixedSize()
                .foregroundStyle(colors.lcdText.opacity(model.selected == 0 || model.isHidden ? 0.55 : 1))
            Spacer(minLength: 0)
            Icon(.navDown, size: Self.chevron).foregroundStyle(colors.lcdText.opacity(0.7))
        }
        .padding(.horizontal, theme.spacing.xs)
        .frame(width: width, height: Self.height)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
    }
}
