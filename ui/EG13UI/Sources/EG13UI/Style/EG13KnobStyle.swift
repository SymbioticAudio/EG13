import AppKit
import SwiftUI
import SymbioticTokens

// EG13's knob design: flat, layered, palette-agnostic (owner, 2026-10-20). Drawing lives
// here (EG13KnobShape); behaviour lives in EG13ControlInteraction. Nothing in this file
// names an EG13 colour: every colour arrives through EG13ControlColors (the environment)
// and EG13KnobStyle.accent, so the style can move into a shared symbiotic-design controls
// product unchanged. No third-party assets.

/// The colours the controls draw with, supplied by the plugin (EG13Palette.controls).
public struct EG13ControlColors: Sendable, Equatable {
    public var track: Color            // faint band under the value arc
    public var tick: Color
    public var skirt: Color            // black outer ring
    public var plainCap: Color         // white cap
    public var darkCap: Color          // black cap
    public var indicator: Color        // on white and accent caps
    public var indicatorOnDark: Color  // on black caps
    public var label: Color            // plain labels and value text
    public var chipText: Color         // text on label chips
    public var lcdBackground: Color
    public var lcdText: Color
    public var focus: Color

    public init(track: Color, tick: Color, skirt: Color, plainCap: Color, darkCap: Color,
                indicator: Color, indicatorOnDark: Color, label: Color, chipText: Color,
                lcdBackground: Color, lcdText: Color, focus: Color) {
        self.track = track
        self.tick = tick
        self.skirt = skirt
        self.plainCap = plainCap
        self.darkCap = darkCap
        self.indicator = indicator
        self.indicatorOnDark = indicatorOnDark
        self.label = label
        self.chipText = chipText
        self.lcdBackground = lcdBackground
        self.lcdText = lcdText
        self.focus = focus
    }
}

private struct EG13ControlColorsKey: EnvironmentKey {
    static let defaultValue = EG13Palette.controls
}

extension EnvironmentValues {
    /// The colours controls draw with; EG13's palette unless a preview injects others.
    public var eg13ControlColors: EG13ControlColors {
        get { self[EG13ControlColorsKey.self] }
        set { self[EG13ControlColorsKey.self] = newValue }
    }
}

/// Knob sizes (owner, 2026-10-25): large, medium and small, as on the real module panels.
public enum EG13KnobSize: Sendable, Equatable, CaseIterable { case large, medium, small }

/// The knob design's options (owner, 2026-10-20). Colours are always injected.
public struct EG13KnobStyle: Sendable, Equatable {
    /// Where the value arc is drawn. Owner, round 1: `.skirtBand` only; the others stay as options.
    public enum ArcPlacement: Sendable, Equatable, CaseIterable {
        /// Filling the black skirt band, over a faint track (the default).
        case skirtBand
        /// A thin ring outside the skirt.
        case outside
        /// A thin ring on the skirt, hugging the cap.
        case capRim
    }
    public enum Cap: Sendable, Equatable, CaseIterable { case white, accent, black }
    public enum LabelStyle: Sendable, Equatable, CaseIterable { case plain, chip }

    public var arc: Bool
    public var arcPlacement: ArcPlacement
    public var ticks: Bool
    public var cap: Cap
    public var label: LabelStyle
    /// The arc, and the accent cap.
    public var accent: Color
    /// The label chip fill: the accent's ink, dark enough for white text.
    public var accentInk: Color

    public init(arc: Bool = true, arcPlacement: ArcPlacement = .skirtBand, ticks: Bool = false,
                cap: Cap = .white, label: LabelStyle = .plain, accent: Color, accentInk: Color? = nil) {
        self.arc = arc
        self.arcPlacement = arcPlacement
        self.ticks = ticks
        self.cap = cap
        self.label = label
        self.accent = accent
        self.accentInk = accentInk ?? accent
    }

    /// EG13's default: arc inside the skirt band over a faint track, no ticks, white cap, grey arc.
    public static let `default` = EG13KnobStyle(accent: EG13Palette.whiteKnobArc.color, accentInk: EG13Palette.legend.color)

    static func capRatio(for size: EG13KnobSize) -> Double {
        size == .large ? 23.0 / 34.0 : 10.5 / 17.0
    }

    func tickCount(for size: EG13KnobSize) -> Int {
        guard ticks else { return 0 }
        return size == .large ? 11 : 7
    }
}

/// Knob sweep: 270 degrees, from 7:30 to 4:30.
enum EG13Sweep {
    /// SwiftUI angle (0 = 3 o'clock, clockwise) of a normalized value.
    static func angle(_ normalized: Double) -> Angle {
        .degrees(135 + 270 * min(max(normalized, 0), 1))
    }

    /// Rotation for a view drawn pointing up (12 o'clock).
    static func rotation(_ normalized: Double) -> Angle {
        .degrees(-135 + 270 * min(max(normalized, 0), 1))
    }
}

/// An arc of the knob sweep between two normalized values.
struct EG13SweepArc: Shape {
    var from: Double
    var to: Double
    var inset: CGFloat

    func path(in rect: CGRect) -> Path {
        var path = Path()
        let radius = min(rect.width, rect.height) / 2 - inset
        guard radius > 0, to > from else { return path }
        path.addArc(center: CGPoint(x: rect.midX, y: rect.midY), radius: radius,
                    startAngle: EG13Sweep.angle(from), endAngle: EG13Sweep.angle(to), clockwise: false)
        return path
    }
}

/// The modulation mark: a generic wave, one and a half sine cycles, stroked, small, in the top or
/// bottom third of the cap, whichever the pointer is not in, so the two never cross (owner, round 4).
/// It moves only when the value takes the pointer across horizontal; it is never animated, shows no
/// amount and is never the source's own shape.
enum EG13ModMarkGeometry {
    /// The mark's width as a part of the cap's diameter.
    static let widthRatio: CGFloat = 0.45
    /// Its height (peak to trough) as a part of its width.
    static let heightRatio: CGFloat = 0.3
    /// Sine cycles across the width.
    static let cycles = 1.5
    /// Its centre's distance from the cap's centre, as a part of the cap's radius.
    static let centreOffset: CGFloat = 0.55
    /// The least stroke width, in points, at 100 % (1.5 pt at 75 %).
    static let minStroke: CGFloat = 2
    /// The size of the click area, in points, at 100 %.
    static let minHit = CGSize(width: 14, height: 10)

    /// Whether the pointer is in the upper half of the cap (above horizontal), from its actual angle,
    /// so the wave sits in the bottom third. Horizontal counts as upper.
    static func pointerIsUp(_ normalized: Double) -> Bool {
        cos(EG13Sweep.rotation(normalized).radians) >= -1e-9
    }

    /// The mark's size (the curve, before its stroke is added).
    static func size(capRadius: CGFloat) -> CGSize {
        let width = capRadius * 2 * widthRatio
        return CGSize(width: width, height: width * heightRatio)
    }

    static func strokeWidth(capRadius: CGFloat) -> CGFloat { max(minStroke, capRadius * 0.12) }

    /// The mark's centre from the knob's centre, y down: below it while the pointer is up, else above.
    static func centre(capRadius: CGFloat, normalized: Double) -> CGPoint {
        CGPoint(x: 0, y: capRadius * centreOffset * (pointerIsUp(normalized) ? 1 : -1))
    }

    /// The click area in a knob `d` across, from its top left.
    static func hitRect(d: CGFloat, capRadius: CGFloat, normalized: Double) -> CGRect {
        let width = minHit.width, height = minHit.height
        let c = centre(capRadius: capRadius, normalized: normalized)
        return CGRect(x: d / 2 + c.x - width / 2, y: d / 2 + c.y - height / 2, width: width, height: height)
    }

    /// The cap's radius in a knob drawn `d` across (EG13KnobShape's own arithmetic).
    static func capRadius(d: CGFloat, style: EG13KnobStyle, size: EG13KnobSize) -> CGFloat {
        let tickCount = style.tickCount(for: size)
        let ticks: CGFloat = tickCount > 1 ? d * 0.08 : 0
        let ring: CGFloat = style.arc && style.arcPlacement == .outside ? d * 0.07 : 0
        let skirt = d - 2 * (ticks + ring)
        return skirt * EG13KnobStyle.capRatio(for: size) / 2
    }

    /// Dark on the white cap, white on pink, teal and black.
    static func ink(_ style: EG13KnobStyle) -> Color {
        style.cap == .white ? Color(red: 0.2, green: 0.2, blue: 0.2) : .white
    }
}

/// A sine across its rect, from the left edge to the right, `cycles` cycles long.
struct EG13SineCycle: Shape {
    var cycles = EG13ModMarkGeometry.cycles

    func path(in rect: CGRect) -> Path {
        var path = Path()
        let steps = 32
        for i in 0...steps {
            let t = Double(i) / Double(steps)
            let point = CGPoint(x: rect.minX + rect.width * CGFloat(t),
                                y: rect.midY - rect.height / 2 * CGFloat(sin(2 * Double.pi * cycles * t)))
            if i == 0 { path.move(to: point) } else { path.addLine(to: point) }
        }
        return path
    }
}

/// Tick marks around the sweep.
struct EG13Ticks: Shape {
    var count: Int
    var length: CGFloat

    func path(in rect: CGRect) -> Path {
        var path = Path()
        guard count > 1 else { return path }
        let center = CGPoint(x: rect.midX, y: rect.midY)
        let outer = min(rect.width, rect.height) / 2
        for i in 0..<count {
            let a = EG13Sweep.angle(Double(i) / Double(count - 1)).radians
            let dx = cos(a), dy = sin(a)
            path.move(to: CGPoint(x: center.x + dx * (outer - length), y: center.y + dy * (outer - length)))
            path.addLine(to: CGPoint(x: center.x + dx * outer, y: center.y + dy * outer))
        }
        return path
    }
}

/// Draws a knob, from outside in: optional ticks; the black skirt; the value arc over a
/// faint track (in the skirt band, outside the skirt or hugging the cap); an optional
/// the cap; a bold indicator line. Flat: no gradients, shading or assets.
/// Pure drawing: it takes a value and answers no input.
public struct EG13KnobShape: View {
    let normalized: Double
    /// Where the value arc starts: 0 (the minimum), or 0.5 for bipolar parameters.
    let origin: Double
    let style: EG13KnobStyle
    let size: EG13KnobSize
    var isFocused = false
    var isEnabled = true
    /// Draws the modulation mark: some route targets this knob. Static; it says nothing of amount.
    var showsModulationMark = false
    @Environment(\.eg13ControlColors) private var colors

    public init(normalized: Double, origin: Double = 0, style: EG13KnobStyle, size: EG13KnobSize,
                isFocused: Bool = false, isEnabled: Bool = true, showsModulationMark: Bool = false) {
        self.showsModulationMark = showsModulationMark
        self.normalized = normalized
        self.origin = origin
        self.style = style
        self.size = size
        self.isFocused = isFocused
        self.isEnabled = isEnabled
    }

    public var body: some View {
        GeometryReader { geometry in
            let d = min(geometry.size.width, geometry.size.height)
            let n = min(max(normalized, 0), 1)
            let low = min(origin, n)
            let high = max(origin, n)
            let tickCount = style.tickCount(for: size)
            let ticks: CGFloat = tickCount > 1 ? d * 0.08 : 0
            let outside = style.arc && style.arcPlacement == .outside
            let ring: CGFloat = outside ? d * 0.07 : 0
            let skirt = d - 2 * (ticks + ring)
            let cap = skirt * EG13KnobStyle.capRatio(for: size)
            let band = (skirt - cap) / 2
            // Arc stroke width and its radius inset from the outer edge of the drawing.
            let (arcWidth, arcInset): (CGFloat, CGFloat) = {
                switch style.arcPlacement {
                case .skirtBand: return (band * 0.7, ticks + ring + band / 2)
                case .outside: return (ring * 0.6, ticks + ring * 0.5)
                case .capRim: return (band * 0.28, ticks + ring + band * 0.78)
                }
            }()
            ZStack {
                EG13Ticks(count: tickCount, length: ticks * 0.7)
                    .stroke(colors.tick, lineWidth: max(1, d * 0.015))
                Circle().fill(colors.skirt).frame(width: skirt, height: skirt)
                if style.arc {
                    EG13SweepArc(from: 0, to: 1, inset: arcInset)
                        .stroke(style.arcPlacement == .outside ? colors.track.opacity(0.6) : colors.track,
                                style: StrokeStyle(lineWidth: arcWidth, lineCap: .butt))
                    EG13SweepArc(from: low, to: max(high, low + 0.002), inset: arcInset)
                        .stroke(style.accent, style: StrokeStyle(lineWidth: arcWidth, lineCap: .butt))
                }
                Circle().fill(capColor).frame(width: cap, height: cap)
                if showsModulationMark {
                    let capRadius = cap / 2
                    let mark = EG13ModMarkGeometry.size(capRadius: capRadius)
                    EG13SineCycle()
                        .stroke(EG13ModMarkGeometry.ink(style),
                                style: StrokeStyle(lineWidth: EG13ModMarkGeometry.strokeWidth(capRadius: capRadius),
                                                   lineCap: .round, lineJoin: .round))
                        .frame(width: mark.width, height: mark.height)
                        .offset(y: EG13ModMarkGeometry.centre(capRadius: capRadius, normalized: n).y)
                        .allowsHitTesting(false)
                        .accessibilityHidden(true)
                }
                Capsule()
                    .fill(style.cap == .black ? colors.indicatorOnDark : colors.indicator)
                    .frame(width: max(2, d * 0.07), height: cap * 0.42)
                    .offset(y: -cap * 0.25)
                    .rotationEffect(EG13Sweep.rotation(n))
                if isFocused {
                    Circle().stroke(colors.focus, lineWidth: 2).frame(width: d, height: d)
                }
            }
            .frame(width: d, height: d)
            .opacity(isEnabled ? 1 : 0.45)
        }
    }

    private var capColor: Color {
        switch style.cap {
        case .white: return colors.plainCap
        case .accent: return style.accent
        case .black: return colors.darkCap
        }
    }
}

/// LCD readout: patch name, model name, values and meters use it (owner, 2026-10-20).
public struct EG13LCDText: View {
    let text: String
    var isFocused = false
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.symbioticTheme) private var theme  // typography and radii only

    public init(text: String, isFocused: Bool = false) {
        self.text = text
        self.isFocused = isFocused
    }

    public var body: some View {
        Text(text.uppercased())
            // Heavier and at least 12 pt: the green-on-grey wells must read at 75 % (owner, round 1).
            .font(.system(size: max(theme.type.label.size, 12), weight: .bold, design: .monospaced))
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .foregroundStyle(colors.lcdText)
            .padding(.horizontal, theme.spacing.xs)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
            .overlay {
                if isFocused {
                    RoundedRectangle(cornerRadius: theme.radius.s).stroke(colors.focus, lineWidth: 2)
                }
            }
    }
}

/// Where a knob label breaks. Labels never shrink and are never cut: one line if the text fits the
/// label width at the label font, otherwise it wraps at its spaces, up to two lines. `fits` is false
/// when even that is not enough (a single word too wide); EG13LayoutTests asserts it is true for
/// every label of every engine.
public enum EG13LabelFit {
    /// The font a label is set in: the theme's caption, bold.
    public static var font: NSFont {
        NSFont.systemFont(ofSize: EG13Theme.theme.type.caption.size, weight: .bold)
    }

    public static func width(_ text: String) -> CGFloat {
        ceil((text as NSString).size(withAttributes: [.font: font]).width)
    }

    public static func lines(_ text: String, width limit: CGFloat) -> (lines: [String], fits: Bool) {
        let text = text.uppercased()
        if self.width(text) <= limit { return ([text], true) }
        let words = text.split(separator: " ").map(String.init)
        guard words.count > 1 else { return ([text], false) }
        // Break at the space that leaves the shorter longest line.
        var best: (lines: [String], widest: CGFloat)?
        for split in 1..<words.count {
            let lines = [words[..<split].joined(separator: " "), words[split...].joined(separator: " ")]
            let widest = lines.map(width).max() ?? 0
            if best == nil || widest < best!.widest { best = (lines, widest) }
        }
        return (best!.lines, best!.widest <= limit)
    }
}

/// A control's name in uppercase: plain, or on a chip with white text.
struct EG13ControlLabel: View {
    let text: String
    let style: EG13KnobStyle.LabelStyle
    var chip: Color?
    let font: Font
    /// The most width the text may take; nil for one natural line.
    var width: CGFloat?
    @Environment(\.eg13ControlColors) private var colors

    var body: some View {
        let lines = width.map { EG13LabelFit.lines(text, width: $0).lines } ?? [text.uppercased()]
        let label = VStack(spacing: 0) {
            ForEach(lines.indices, id: \.self) { index in
                Text(lines[index])
                    .font(font.weight(.bold))
                    .foregroundStyle(style == .chip ? colors.chipText : colors.label)
                    .lineLimit(1)
                    .fixedSize()
            }
        }
        .padding(.horizontal, 3)
        switch style {
        case .chip:
            label.background(RoundedRectangle(cornerRadius: 2).fill(chip ?? colors.label))
        case .plain:
            label
        }
    }
}
