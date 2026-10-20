import SwiftUI
import SymbioticTokens

// EG13's controls: flat, layered, palette-agnostic (owner, 2026-10-20). Nothing in this
// file or in Controls/ names an EG13 colour: every colour arrives through
// EG13ControlColors and EG13KnobLook, so the style can move into a shared
// symbiotic-design controls product unchanged. Drawing lives here; behaviour lives in
// EG13ControlInteraction. No third-party assets.

/// The colours a control style draws with, supplied by the plugin (EG13Palette.controls).
public struct EG13ControlColors: Sendable, Equatable {
    public var track: Color            // faint band under the value arc
    public var tick: Color
    public var skirt: Color            // black outer ring / fader body
    public var innerRing: Color        // optional faint ring on the skirt
    public var plainCap: Color         // white cap
    public var darkCap: Color          // small black cap
    public var indicator: Color        // on plain and filled caps
    public var indicatorOnDark: Color  // on dark caps
    public var label: Color            // plain labels and value text
    public var chipText: Color         // text on label chips
    public var lcdBackground: Color
    public var lcdText: Color
    public var focus: Color

    public init(track: Color, tick: Color, skirt: Color, innerRing: Color, plainCap: Color, darkCap: Color,
                indicator: Color, indicatorOnDark: Color, label: Color, chipText: Color,
                lcdBackground: Color, lcdText: Color, focus: Color) {
        self.track = track
        self.tick = tick
        self.skirt = skirt
        self.innerRing = innerRing
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

/// One knob's resolved colours: its cap, its value arc and, if emphasised, its label chip.
public struct EG13KnobLook: Sendable, Equatable {
    public enum Cap: Sendable, Equatable {
        case plain
        case filled(Color)
        case dark
    }
    public var cap: Cap
    public var arc: Color
    /// Fill of the label chip (white text); nil for a plain uppercase label.
    public var chip: Color?

    public init(cap: Cap, arc: Color, chip: Color? = nil) {
        self.cap = cap
        self.arc = arc
        self.chip = chip
    }
}

/// Knob sizes: one hero knob per module card, small knobs elsewhere.
public enum EG13KnobSize: Sendable, Equatable { case hero, detail, bar }

public struct EG13KnobConfiguration: Sendable, Equatable {
    public var normalized: Double
    /// Where the value arc starts: 0 (the minimum), or 0.5 for bipolar parameters.
    public var origin: Double
    /// Modulation range in normalized units; nil hides the mod ring (no routes before M4).
    public var modulation: ClosedRange<Double>?
    public var look: EG13KnobLook
    /// Cap radius as a fraction of the skirt radius (small 10.5/17, hero 23/34).
    public var capRatio: Double
    public var showsValueArc: Bool
    public var showsInnerRing: Bool
    public var tickCount: Int
    public var isFocused: Bool
    public var isEnabled: Bool
}

/// The shared knob design's options (owner, 2026-10-20). Colours are always injected.
public struct EG13KnobOptions: Sendable, Equatable {
    public enum Labels: Sendable, Equatable { case text, chip }
    public var showsValueArc: Bool
    public var showsTicks: Bool
    public var showsInnerRing: Bool
    /// .chip: emphasised knobs get a colour chip; .text: every label is plain text.
    public var labels: Labels

    public init(showsValueArc: Bool, showsTicks: Bool, showsInnerRing: Bool, labels: Labels) {
        self.showsValueArc = showsValueArc
        self.showsTicks = showsTicks
        self.showsInnerRing = showsInnerRing
        self.labels = labels
    }

    /// EG13's defaults, matching the owner's mock: value arc on, ticks off, inner ring off,
    /// chips on emphasised knobs.
    public static let eg13 = EG13KnobOptions(showsValueArc: true, showsTicks: false, showsInnerRing: false,
                                             labels: .chip)

    public static func capRatio(for size: EG13KnobSize) -> Double {
        size == .hero ? 23.0 / 34.0 : 10.5 / 17.0
    }

    public func tickCount(for size: EG13KnobSize) -> Int {
        guard showsTicks else { return 0 }
        return size == .hero ? 11 : 7
    }

    /// The chip to draw under a knob, or nil for a plain label.
    public func chip(for look: EG13KnobLook) -> Color? {
        labels == .chip ? look.chip : nil
    }

    /// What a knob of this size draws for a value.
    public func configuration(normalized: Double, origin: Double, look: EG13KnobLook, size: EG13KnobSize,
                              isFocused: Bool, isEnabled: Bool = true) -> EG13KnobConfiguration {
        EG13KnobConfiguration(normalized: normalized, origin: origin, modulation: nil, look: look,
                              capRatio: Self.capRatio(for: size), showsValueArc: showsValueArc,
                              showsInnerRing: showsInnerRing, tickCount: tickCount(for: size),
                              isFocused: isFocused, isEnabled: isEnabled)
    }
}

public struct EG13SliderConfiguration: Sendable, Equatable {
    public var normalized: Double
    public var origin: Double
    public var arc: Color
    public var isVertical: Bool
    public var isFocused: Bool
    public var isEnabled: Bool
}

public struct EG13StepperConfiguration: Sendable, Equatable {
    public var text: String
    public var isFocused: Bool
    public var isEnabled: Bool
}

/// The one type that decides how controls look. Views never draw controls themselves.
public struct EG13ControlStyle: Sendable {
    public var colors: EG13ControlColors
    public var knobOptions: EG13KnobOptions
    public var knob: @MainActor @Sendable (EG13KnobConfiguration) -> AnyView
    public var slider: @MainActor @Sendable (EG13SliderConfiguration) -> AnyView
    public var stepper: @MainActor @Sendable (EG13StepperConfiguration) -> AnyView

    public init(colors: EG13ControlColors, knobOptions: EG13KnobOptions,
                knob: @escaping @MainActor @Sendable (EG13KnobConfiguration) -> AnyView,
                slider: @escaping @MainActor @Sendable (EG13SliderConfiguration) -> AnyView,
                stepper: @escaping @MainActor @Sendable (EG13StepperConfiguration) -> AnyView) {
        self.colors = colors
        self.knobOptions = knobOptions
        self.knob = knob
        self.slider = slider
        self.stepper = stepper
    }

    /// Flat, layered controls in the given colours.
    public static func flat(colors: EG13ControlColors, knobOptions: EG13KnobOptions = .eg13) -> EG13ControlStyle {
        EG13ControlStyle(
            colors: colors, knobOptions: knobOptions,
            knob: { AnyView(EG13FlatKnob(configuration: $0, colors: colors)) },
            slider: { AnyView(EG13FlatSlider(configuration: $0, colors: colors)) },
            stepper: { AnyView(EG13FlatStepper(configuration: $0, colors: colors)) })
    }

    /// System greys, used only when no plugin style is set (previews).
    public static let neutral = EG13ControlStyle.flat(colors: EG13ControlColors(
        track: .primary.opacity(0.14), tick: .secondary, skirt: .primary, innerRing: .secondary.opacity(0.2),
        plainCap: .secondary, darkCap: .primary, indicator: .primary, indicatorOnDark: .secondary,
        label: .primary, chipText: .secondary, lcdBackground: .primary, lcdText: .secondary, focus: .accentColor))
}

private struct EG13ControlStyleKey: EnvironmentKey {
    static let defaultValue = EG13ControlStyle.neutral
}

extension EnvironmentValues {
    public var eg13ControlStyle: EG13ControlStyle {
        get { self[EG13ControlStyleKey.self] }
        set { self[EG13ControlStyleKey.self] = newValue }
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
    var outerInset: CGFloat
    var length: CGFloat

    func path(in rect: CGRect) -> Path {
        var path = Path()
        guard count > 1 else { return path }
        let center = CGPoint(x: rect.midX, y: rect.midY)
        let outer = min(rect.width, rect.height) / 2 - outerInset
        for i in 0..<count {
            let a = EG13Sweep.angle(Double(i) / Double(count - 1)).radians
            let dx = cos(a), dy = sin(a)
            path.move(to: CGPoint(x: center.x + dx * (outer - length), y: center.y + dy * (outer - length)))
            path.addLine(to: CGPoint(x: center.x + dx * outer, y: center.y + dy * outer))
        }
        return path
    }
}

/// Flat layered knob, from outside in: black skirt ring; inside it, a faint track and the
/// value arc filling the band from the minimum (or centre) to the value; optional ticks
/// and inner ring; the cap (plain, filled or dark); a bold indicator line. No gradients,
/// shading or assets.
struct EG13FlatKnob: View {
    let configuration: EG13KnobConfiguration
    let colors: EG13ControlColors

    var body: some View {
        GeometryReader { geometry in
            let d = min(geometry.size.width, geometry.size.height)
            let n = min(max(configuration.normalized, 0), 1)
            let low = min(configuration.origin, n)
            let high = max(configuration.origin, n)
            let ticks: CGFloat = configuration.tickCount > 1 ? d * 0.08 : 0
            let skirt = d - 2 * ticks
            let cap = skirt * configuration.capRatio
            let band = (skirt - cap) / 2
            ZStack {
                EG13Ticks(count: configuration.tickCount, outerInset: 0, length: ticks * 0.7)
                    .stroke(colors.tick, lineWidth: max(1, d * 0.015))
                Circle().fill(colors.skirt).frame(width: skirt, height: skirt)
                if configuration.showsValueArc {
                    EG13SweepArc(from: 0, to: 1, inset: ticks + band / 2)
                        .stroke(colors.track, style: StrokeStyle(lineWidth: band * 0.7, lineCap: .butt))
                    EG13SweepArc(from: low, to: max(high, low + 0.002), inset: ticks + band / 2)
                        .stroke(configuration.look.arc, style: StrokeStyle(lineWidth: band * 0.7, lineCap: .butt))
                }
                if let modulation = configuration.modulation {
                    EG13SweepArc(from: modulation.lowerBound, to: modulation.upperBound, inset: ticks + band * 0.15)
                        .stroke(configuration.look.arc.opacity(0.6), style: StrokeStyle(lineWidth: band * 0.2))
                }
                if configuration.showsInnerRing {
                    Circle().stroke(colors.innerRing, lineWidth: max(1, d * 0.012))
                        .frame(width: cap + band * 0.5, height: cap + band * 0.5)
                }
                Circle().fill(capColor).frame(width: cap, height: cap)
                Capsule()
                    .fill(isDarkCap ? colors.indicatorOnDark : colors.indicator)
                    .frame(width: max(2, d * 0.07), height: cap * 0.42)
                    .offset(y: -cap * 0.25)
                    .rotationEffect(EG13Sweep.rotation(n))
                if configuration.isFocused {
                    Circle().stroke(colors.focus, lineWidth: 2).frame(width: d, height: d)
                }
            }
            .frame(width: d, height: d)
            .opacity(configuration.isEnabled ? 1 : 0.45)
        }
    }

    private var isDarkCap: Bool { configuration.look.cap == .dark }

    private var capColor: Color {
        switch configuration.look.cap {
        case .plain: return colors.plainCap
        case .filled(let color): return color
        case .dark: return colors.darkCap
        }
    }
}

/// Flat fader: track, value fill, flat black handle with a white line.
struct EG13FlatSlider: View {
    let configuration: EG13SliderConfiguration
    let colors: EG13ControlColors

    var body: some View {
        GeometryReader { geometry in
            let vertical = configuration.isVertical
            let w = geometry.size.width, h = geometry.size.height
            let length = vertical ? h : w
            let across = min(vertical ? w : h, 22)
            let along: CGFloat = 10
            let travel = max(0, length - along)
            let position = { (v: Double) -> CGFloat in
                let t = CGFloat(min(max(v, 0), 1))
                return along / 2 + travel * (vertical ? 1 - t : t)
            }
            let a = position(configuration.origin), b = position(configuration.normalized)
            ZStack {
                Capsule().fill(colors.skirt)
                    .frame(width: vertical ? 6 : travel, height: vertical ? travel : 6)
                    .position(x: w / 2, y: h / 2)
                Capsule().fill(configuration.arc)
                    .frame(width: vertical ? 4 : abs(b - a), height: vertical ? abs(b - a) : 4)
                    .position(x: vertical ? w / 2 : (a + b) / 2, y: vertical ? (a + b) / 2 : h / 2)
                RoundedRectangle(cornerRadius: 2)
                    .fill(colors.skirt)
                    .overlay(Rectangle().fill(colors.indicatorOnDark)
                        .frame(width: vertical ? across * 0.7 : 2, height: vertical ? 2 : across * 0.7))
                    .frame(width: vertical ? across : along, height: vertical ? along : across)
                    .position(x: vertical ? w / 2 : b, y: vertical ? b : h / 2)
                if configuration.isFocused {
                    RoundedRectangle(cornerRadius: 2).stroke(colors.focus, lineWidth: 2)
                }
            }
            .opacity(configuration.isEnabled ? 1 : 0.45)
        }
    }
}

/// Flat LCD well with the value in LCD segment colour.
struct EG13FlatStepper: View {
    let configuration: EG13StepperConfiguration
    let colors: EG13ControlColors
    @Environment(\.symbioticTheme) private var theme  // typography and radii only

    var body: some View {
        EG13LCDText(text: configuration.text, colors: colors)
            .overlay {
                if configuration.isFocused {
                    RoundedRectangle(cornerRadius: theme.radius.s).stroke(colors.focus, lineWidth: 2)
                }
            }
            .opacity(configuration.isEnabled ? 1 : 0.45)
    }
}

/// LCD readout: patch name, model name, values and meters use it (owner, 2026-10-20).
public struct EG13LCDText: View {
    let text: String
    let colors: EG13ControlColors
    @Environment(\.symbioticTheme) private var theme  // typography and radii only

    public init(text: String, colors: EG13ControlColors) {
        self.text = text
        self.colors = colors
    }

    public var body: some View {
        Text(text.uppercased())
            .font(.system(size: theme.type.label.size, weight: theme.type.label.weight.swiftUI, design: .monospaced))
            .lineLimit(1)
            .minimumScaleFactor(0.7)
            .foregroundStyle(colors.lcdText)
            .padding(.horizontal, theme.spacing.xs)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
    }
}
