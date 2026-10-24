/// Scroll phases as reported by the control surface.
public enum EG13ScrollPhase: Sendable {
    case began, changed, ended
    /// A mouse wheel with no phases: one notch is one complete gesture.
    case discrete
}

/// The behaviour every EG13 control shares, independent of how it is drawn: drag and fine
/// drag (shift), double-click to default, scroll, keyboard and VoiceOver increments, each
/// wrapped in a CLAP gesture. Pure logic over an EG13Param, so it is unit-tested.
@MainActor
public final class EG13ControlInteraction {
    /// Points of vertical drag (or scroll) that sweep the whole range.
    public static let pointsForFullRange: Double = 200
    /// Shift divides drag and scroll speed by ten.
    public static let fineFactor: Double = 0.1
    /// Continuous parameters move 1% per key press (0.1% with shift).
    public static let keyStep: Double = 0.01
    /// Stepped parameters move one step per this many points of scroll.
    public static let pointsPerScrollStep: Double = 10

    public let param: EG13Param
    private var dragAnchor: Double = 0
    private var dragOffset: Double = 0
    private var ignoringDrag = false
    private var scrollRemainder: Double = 0

    public init(param: EG13Param) {
        self.param = param
    }

    /// clickCount 2 resets to the default; any other click starts a drag gesture.
    public func mouseDown(clickCount: Int) {
        if clickCount >= 2 {
            if param.isEditing { param.endEdit() }
            param.resetToDefault()
            ignoringDrag = true
            return
        }
        ignoringDrag = false
        param.beginEdit()
        dragAnchor = param.normalized
        dragOffset = 0
    }

    /// deltaY in points, positive upwards.
    public func mouseDragged(deltaY: Double, fine: Bool) {
        guard !ignoringDrag, param.isEditing else { return }
        dragOffset += deltaY * (fine ? Self.fineFactor : 1) / Self.pointsForFullRange
        // Keep the offset inside the range so reversing direction responds at once.
        dragOffset = min(max(dragOffset, -dragAnchor), 1 - dragAnchor)
        param.setNormalized(dragAnchor + dragOffset)
    }

    public func mouseUp() {
        ignoringDrag = false
        param.endEdit()
    }

    /// deltaY in points, positive upwards (a wheel notch is reported as pointsPerScrollStep).
    public func scroll(deltaY: Double, fine: Bool, phase: EG13ScrollPhase) {
        switch phase {
        case .began:
            scrollRemainder = 0
            param.beginEdit()
            applyScroll(deltaY, fine: fine)
        case .changed:
            applyScroll(deltaY, fine: fine)
        case .ended:
            applyScroll(deltaY, fine: fine)
            param.endEdit()
        case .discrete:
            param.perform { applyScroll(deltaY, fine: fine) }
        }
    }

    private func applyScroll(_ deltaY: Double, fine: Bool) {
        let points = deltaY * (fine ? Self.fineFactor : 1)
        if param.stepCount != nil {
            scrollRemainder += points
            let steps = (scrollRemainder / Self.pointsPerScrollStep).rounded(.towardZero)
            guard steps != 0 else { return }
            scrollRemainder -= steps * Self.pointsPerScrollStep
            param.setValue(param.value + steps)
        } else {
            param.setNormalized(param.normalized + points / Self.pointsForFullRange)
        }
    }

    /// Keyboard arrows and VoiceOver: one step (stepped) or 1% (continuous; 0.1% when fine).
    public func increment(by steps: Int, fine: Bool = false) {
        param.perform {
            if param.stepCount != nil {
                param.setValue(param.value + Double(steps))
            } else {
                let size = Self.keyStep * (fine ? Self.fineFactor : 1)
                param.setNormalized(param.normalized + Double(steps) * size)
            }
        }
    }
}
