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

/// Knob sizes: one hero knob per module card, small knobs elsewhere.
public enum EG13KnobSize: Sendable, Equatable { case hero, detail, bar }

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

    /// EG13's default: arc inside the skirt band over a faint track, no ticks, white cap,
    /// in Part A's accent.
    public static let `default` = EG13KnobStyle(accent: partAccent(0), accentInk: EG13Palette.teal.ink.color)

    /// The same style in a part's accent.
    public func accented(part: Int) -> EG13KnobStyle {
        var style = self
        style.accent = partAccent(part)
        style.accentInk = EG13Palette.part(part).ink.color
        return style
    }

    static func capRatio(for size: EG13KnobSize) -> Double {
        size == .hero ? 23.0 / 34.0 : 10.5 / 17.0
    }

    func tickCount(for size: EG13KnobSize) -> Int {
        guard ticks else { return 0 }
        return size == .hero ? 11 : 7
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
    @Environment(\.eg13ControlColors) private var colors

    public init(normalized: Double, origin: Double = 0, style: EG13KnobStyle, size: EG13KnobSize,
                isFocused: Bool = false, isEnabled: Bool = true) {
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

/// A control's name in uppercase: plain, or on a chip with white text.
struct EG13ControlLabel: View {
    let text: String
    let style: EG13KnobStyle.LabelStyle
    var chip: Color?
    let font: Font
    @Environment(\.eg13ControlColors) private var colors

    var body: some View {
        switch style {
        case .chip:
            Text(text.uppercased()).font(font.weight(.bold)).foregroundStyle(colors.chipText).lineLimit(1)
                .padding(.horizontal, 5)
                .background(RoundedRectangle(cornerRadius: 2).fill(chip ?? colors.label))
        case .plain:
            Text(text.uppercased()).font(font.weight(.bold)).foregroundStyle(colors.label).lineLimit(1)
        }
    }
}
