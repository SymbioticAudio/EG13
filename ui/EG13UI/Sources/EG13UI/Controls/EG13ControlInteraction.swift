/// Scroll phases as reported by the control surface.
public enum EG13ScrollPhase: Sendable {
    case began, changed, ended
    /// A mouse wheel with no phases: one notch is one complete gesture.
    case discrete
}

/// The behaviour every EG13 control shares, independent of how it is drawn: drag and fine
/// drag (shift), double-click to default, scroll, keyboard and VoiceOver increments. A
/// pure value type: it reads the current value and reports every change through the
/// begin / edit(value) / end callbacks, which the host wraps as one CLAP gesture each
/// (spec §8). Every begin is matched by one end, including when a drag is cancelled.
@MainActor
public struct EG13ControlInteraction {
    /// Points of vertical drag that sweep the whole range.
    public static let pointsForFullRange: Double = 200
    /// Shift divides drag, scroll and key speed by ten.
    public static let fineFactor: Double = 0.1
    /// A key press or accessibility increment moves 1/100 of the range (one step if stepped).
    public static let keyStep: Double = 0.01
    /// One scroll line moves 1/100 of the range (one step if stepped).
    public static let scrollLineStep: Double = 0.01
    /// Trackpads report points; this many make one scroll line.
    public static let pointsPerScrollLine: Double = 10

    public let minValue: Double
    public let maxValue: Double
    public let defaultValue: Double
    public let isStepped: Bool

    private let current: () -> Double
    private let begin: () -> Void
    private let edit: (Double) -> Void
    private let end: () -> Void

    private var gestureOpen = false
    private var ignoringDrag = false
    private var dragAnchor: Double = 0
    private var dragOffset: Double = 0
    private var scrollRemainder: Double = 0

    public init(minValue: Double, maxValue: Double, defaultValue: Double, isStepped: Bool,
                current: @escaping () -> Double, begin: @escaping () -> Void,
                edit: @escaping (Double) -> Void, end: @escaping () -> Void) {
        self.minValue = minValue
        self.maxValue = maxValue
        self.defaultValue = defaultValue
        self.isStepped = isStepped
        self.current = current
        self.begin = begin
        self.edit = edit
        self.end = end
    }

    // MARK: Value mapping

    private var span: Double { maxValue - minValue }

    /// 0...1 across the range.
    public func normalized(_ value: Double) -> Double {
        span > 0 ? min(max((value - minValue) / span, 0), 1) : 0
    }

    /// The value for a position across the range: clamped, and snapped to an integer
    /// step when the parameter is stepped.
    public func value(forNormalized n: Double) -> Double {
        let clamped = min(max(n.isNaN ? 0 : n, 0), 1)
        let v = minValue + clamped * span
        return isStepped ? v.rounded() : v
    }

    // MARK: Gesture

    public var isInGesture: Bool { gestureOpen }

    private mutating func open() {
        guard !gestureOpen else { return }
        gestureOpen = true
        begin()
    }

    private mutating func close() {
        guard gestureOpen else { return }
        gestureOpen = false
        end()
    }

    private func send(_ value: Double) {
        if value != current() { edit(value) }
    }

    // MARK: Mouse

    /// clickCount 2 resets to the default in one gesture; any other click starts a drag.
    public mutating func mouseDown(clickCount: Int) {
        if clickCount >= 2 {
            close()
            begin()
            edit(defaultValue)
            end()
            ignoringDrag = true
            return
        }
        ignoringDrag = false
        open()
        dragAnchor = normalized(current())
        dragOffset = 0
    }

    /// deltaY in points, positive upwards.
    public mutating func mouseDragged(deltaY: Double, fine: Bool) {
        guard !ignoringDrag, gestureOpen else { return }
        dragOffset += deltaY * (fine ? Self.fineFactor : 1) / Self.pointsForFullRange
        // Keep the offset inside the range so reversing direction responds at once.
        dragOffset = min(max(dragOffset, -dragAnchor), 1 - dragAnchor)
        send(value(forNormalized: dragAnchor + dragOffset))
    }

    public mutating func mouseUp() {
        ignoringDrag = false
        close()
    }

    /// The drag or scroll is abandoned (the surface left the window, the app resigned
    /// active): the gesture ends where it is, so no touch stays open in the host.
    public mutating func cancel() {
        ignoringDrag = false
        scrollRemainder = 0
        close()
    }

    // MARK: Scroll

    /// lines: scroll lines, positive upwards. A wheel notch is one line; a trackpad reports
    /// points (pointsPerScrollLine make one line).
    public mutating func scroll(lines: Double, fine: Bool, phase: EG13ScrollPhase) {
        switch phase {
        case .began:
            scrollRemainder = 0
            open()
            applyScroll(lines, fine: fine)
        case .changed:
            applyScroll(lines, fine: fine)
        case .ended:
            applyScroll(lines, fine: fine)
            close()
        case .discrete:
            let wasOpen = gestureOpen
            open()
            applyScroll(lines, fine: fine)
            if !wasOpen { close() }
        }
    }

    private mutating func applyScroll(_ lines: Double, fine: Bool) {
        let amount = lines * (fine ? Self.fineFactor : 1)
        if isStepped {
            scrollRemainder += amount
            let steps = scrollRemainder.rounded(.towardZero)
            guard steps != 0 else { return }
            scrollRemainder -= steps
            send(value(forNormalized: normalized(current() + steps)))
        } else {
            send(value(forNormalized: normalized(current()) + amount * Self.scrollLineStep))
        }
    }

    // MARK: Keys and accessibility

    /// Keyboard arrows and VoiceOver: one step (stepped) or 1/100 of the range (continuous;
    /// a tenth of that when fine). Inside a gesture it joins it; outside, it is its own.
    public mutating func increment(by steps: Int, fine: Bool = false) {
        let wasOpen = gestureOpen
        let target: Double
        if isStepped {
            target = current() + Double(steps)
        } else {
            target = minValue + (normalized(current()) + Double(steps) * Self.keyStep * (fine ? Self.fineFactor : 1)) * span
        }
        let snapped = value(forNormalized: normalized(target))
        guard snapped != current() else { return }
        open()
        edit(snapped)
        if !wasOpen { close() }
    }
}
