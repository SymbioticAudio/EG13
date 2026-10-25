import SwiftUI
import SymbioticTokens

/// The matrix: the part's 12 route amounts as secondary (black cap) knobs labelled "Route 1" to
/// "Route 12". Choosing each route's source and destination is M6b.
struct EG13MatrixAmounts: View {
    let synth: EG13Synth
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let controls = synth.editorControls(part: part)
        HStack(spacing: theme.spacing.s) {
            Text("MATRIX")
                .font(theme.type.caption.font.weight(.bold))
                .foregroundStyle(theme.text.secondary.color)
                .accessibilityAddTraits(.isHeader)
                .frame(width: 52)
            EG13ControlGrid(columns: EG13ParamID.routeCount, specs: controls.matrix) { spec in
                EG13ControlView(synth: synth, spec: spec, part: part)
            }
        }
        .padding(.horizontal, theme.spacing.s)
        .frame(height: EG13Layout.matrixHeight)
        .frame(maxWidth: .infinity)
        .background(theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Matrix route amounts")
    }
}
