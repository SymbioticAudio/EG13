import SwiftUI
import SymbioticTokens

/// The scale of a mod trace: a trace is centred (-1...1) when its output has gone negative, else
/// it is drawn bottom-up (0...1). Pure arithmetic, no allocation.
@MainActor
enum EG13ModTraceScale {
    static func isBipolar(_ trace: EG13ModTrace, output: Int) -> Bool {
        for index in 0..<trace.frameCount {
            if let frame = trace.frame(output: output, at: index), frame.min < -0.0001 { return true }
        }
        return false
    }

    /// The y of a value in a drawing `height` tall: the top is 1, the bottom 0 (or -1 centred).
    static func y(_ value: Float, height: CGFloat, bipolar: Bool) -> CGFloat {
        let clamped = CGFloat(min(max(value, bipolar ? -1 : 0), 1))
        return bipolar ? height / 2 - clamped * height / 2 : height - clamped * height
    }
}

/// A mod slot's last two seconds as a trace on a small LCD well (spec §8, M6b decision 4): one
/// min-max bar per display frame, so a fast LFO reads as a band, and a line through the bars'
/// middles. A frame with no data is a gap and breaks the line. Draws the output given (the slot's
/// first by default); a unipolar output is drawn bottom-up, a bipolar one centred. Newest at right.
///
/// The view's body reads the trace's revision, so it redraws when the trace changes and not
/// otherwise; the Canvas reads the frames in place.
struct EG13ModTraceView: View {
    let trace: EG13ModTrace
    var output = 0
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // radii only

    var body: some View {
        let revision = trace.revision
        let ink = colors.lcdText
        Canvas { context, size in
            _ = revision  // a changed revision is a new drawing
            let count = trace.frameCount
            guard count > 0, size.width > 2, size.height > 2 else { return }
            let bipolar = EG13ModTraceScale.isBipolar(trace, output: output)
            let inset: CGFloat = 2
            let height = size.height - 2 * inset
            let step = (size.width - 2 * inset) / CGFloat(EG13ModTrace.capacity - 1)
            let right = size.width - inset
            if bipolar {
                var centre = Path()
                centre.move(to: CGPoint(x: inset, y: size.height / 2))
                centre.addLine(to: CGPoint(x: right, y: size.height / 2))
                context.stroke(centre, with: .color(ink.opacity(0.25)), lineWidth: 0.5)
            }
            var bars = Path()
            var line = Path()
            var drawing = false
            for index in 0..<count {
                guard let frame = trace.frame(output: output, at: index) else { drawing = false; continue }
                let x = right - CGFloat(count - 1 - index) * step
                let top = inset + EG13ModTraceScale.y(frame.max, height: height, bipolar: bipolar)
                let bottom = inset + EG13ModTraceScale.y(frame.min, height: height, bipolar: bipolar)
                bars.move(to: CGPoint(x: x, y: top))
                bars.addLine(to: CGPoint(x: x, y: max(bottom, top + 1)))
                let middle = (top + bottom) / 2
                if drawing { line.addLine(to: CGPoint(x: x, y: middle)) } else { line.move(to: CGPoint(x: x, y: middle)) }
                drawing = true
            }
            context.stroke(bars, with: .color(ink.opacity(0.5)), lineWidth: max(1, step * 0.8))
            context.stroke(line, with: .color(ink), style: StrokeStyle(lineWidth: 1.2, lineJoin: .round))
        }
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
        .accessibilityHidden(true)
    }
}
