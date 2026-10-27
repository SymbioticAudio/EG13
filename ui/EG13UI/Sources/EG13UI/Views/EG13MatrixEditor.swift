import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// The MATRIX pane of the right-hand column: the part's 12 routes, one row each. A row is its
/// number, the source, via and destination menus, a small amount knob with its value, and a clear
/// button. Rows are drawn from `synth.routeMenus` and `editorControls.matrix`, so what shows is
/// what the reachability test checks.
struct EG13MatrixEditor: View {
    let synth: EG13Synth
    let part: Int
    let state: EG13RightColumnState
    let width: CGFloat
    /// Called with the rows' natural height (with their padding).
    var onHeight: ((CGFloat) -> Void)?
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let controls = synth.editorControls(part: part)
        let menus = synth.routeMenus(part: part)
        ScrollViewReader { proxy in
            EG13Scrolling(onHeight: onHeight) {
                VStack(spacing: EG13Layout.rowGap) {
                    ForEach(controls.matrix.indices, id: \.self) { index in
                        EG13MatrixRow(synth: synth, part: part, index: index, amount: controls.matrix[index],
                                      menus: menus.filter { $0.route == index },
                                      isHighlighted: state.highlightedRoute == index)
                            .id(index)
                    }
                }
                .padding(EG13Layout.cardPadding)
            }
            .task(id: state.openCount) {
                guard let route = state.highlightedRoute else { return }
                await Task.yield()
                proxy.scrollTo(route, anchor: .center)
                try? await Task.sleep(nanoseconds: 2_500_000_000)
                if !Task.isCancelled { state.clearHighlight() }
            }
            .onChange(of: part) { state.clearHighlight() }  // a highlight belongs to one part's route
        }
        .frame(width: width, alignment: .topLeading)
        .background(theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Matrix")
    }
}

struct EG13MatrixRow: View {
    let synth: EG13Synth
    let part: Int
    let index: Int
    let amount: EG13ControlSpec
    let menus: [EG13RouteMenuModel]
    let isHighlighted: Bool
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme

    static let height: CGFloat = 36

    var body: some View {
        HStack(spacing: EG13Layout.rowGap) {
            Text("\(index + 1)")
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(theme.text.secondary.color)
                .frame(width: EG13Layout.routeNumberWidth, alignment: .trailing)
                .accessibilityHidden(true)
            menu(.source, width: EG13Layout.routeSourceWidth)
            Text("via")
                .font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
                .accessibilityHidden(true)
            menu(.via, width: EG13Layout.routeViaWidth)
            menu(.destination, width: EG13Layout.routeDestinationWidth)
            if let param = synth.param(amount.id) {
                EG13Knob(param: param, style: EG13Palette.knobStyle(for: amount.emphasis), size: .small,
                         layout: EG13KnobLayout(compact: true))
                EG13ValueText(text: param.text, font: theme.type.caption.font)
                    .frame(width: EG13Layout.routeValueWidth, alignment: .leading)
                clearButton
            }
        }
        .padding(.horizontal, EG13Layout.rowGap)
        .frame(height: Self.height)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.raised.color))
        .overlay {
            if isHighlighted {
                RoundedRectangle(cornerRadius: theme.radius.s).stroke(colors.focus, lineWidth: 2)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Route \(index + 1)")
    }

    @ViewBuilder private func menu(_ field: EG13RouteField, width: CGFloat) -> some View {
        if let model = menus.first(where: { $0.field == field }) {
            EG13RouteMenu(synth: synth, model: model, width: width)
        }
    }

    /// Source, destination and via None, amount 0 (the amount through a begin/edit/end gesture).
    private var clearButton: some View {
        let route = synth.routes(part: part)[index]
        let canClear = !route.isFree || route.amount != 0
        return Button { synth.clearRoute(part: part, route: index) } label: {
            Icon(.editClose, size: 10).frame(width: 20, height: 20)
        }
        .buttonStyle(.plain)
        .foregroundStyle(colors.label)
        .opacity(canClear ? 1 : 0.35)
        .disabled(!canClear)
        .help("Clear route \(index + 1)")
        .accessibilityLabel("Clear route \(index + 1)")
    }
}
