import AppKit
import SwiftUI

/// Mouse and scroll input for a control, forwarded to its EG13ControlInteraction.
/// An NSView so it gets click counts, modifier flags, scroll phases and first-mouse clicks
/// in an inactive plugin window, which SwiftUI gestures on macOS 14 do not provide.
struct EG13ControlSurface: NSViewRepresentable {
    let interaction: Binding<EG13ControlInteraction>
    /// A click area inside the control (from its top left) that is a click, not a drag: the knob's
    /// modulation mark. A drag that starts there adjusts the control like any other.
    var clickRect: CGRect?
    var onClickRect: (() -> Void)?

    func makeNSView(context: Context) -> EG13ControlSurfaceView {
        let view = EG13ControlSurfaceView()
        view.interaction = interaction
        view.clickRect = clickRect
        view.onClickRect = onClickRect
        return view
    }

    func updateNSView(_ view: EG13ControlSurfaceView, context: Context) {
        view.interaction = interaction
        view.clickRect = clickRect
        view.onClickRect = onClickRect
    }

    /// The surface goes away: end any open gesture, whatever state SwiftUI still holds.
    static func dismantleNSView(_ view: EG13ControlSurfaceView, coordinator: ()) {
        view.endGesture()
    }
}

final class EG13ControlSurfaceView: NSView {
    var interaction: Binding<EG13ControlInteraction>? {
        didSet { if let gesture = interaction?.wrappedValue.gesture { self.gesture = gesture } }
    }
    /// The gesture is owned here too, so ending it never depends on SwiftUI's @State.
    private var gesture: EG13Gesture?
    private var lastY: CGFloat = 0
    var clickRect: CGRect?
    var onClickRect: (() -> Void)?
    /// A press on the click area, not yet a click or a drag: where it started.
    private var pendingClick: (start: CGPoint, clickCount: Int)?

    /// The event's position from the view's top left.
    private func topLeft(_ event: NSEvent) -> CGPoint {
        let p = convert(event.locationInWindow, from: nil)
        return CGPoint(x: p.x, y: isFlipped ? p.y : bounds.height - p.y)
    }

    /// A control that leaves the window mid-gesture (the editor closes, the view is
    /// replaced) ends its gesture, so no touch stays open in the host.
    override func viewWillMove(toWindow newWindow: NSWindow?) {
        if newWindow == nil { endGesture() }
        super.viewWillMove(toWindow: newWindow)
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    func endGesture() { gesture?.close() }

    private func localY(_ event: NSEvent) -> CGFloat {
        let y = convert(event.locationInWindow, from: nil).y
        return isFlipped ? -y : y  // positive upwards
    }

    override func mouseDown(with event: NSEvent) {
        lastY = localY(event)
        let point = topLeft(event)
        if let clickRect, onClickRect != nil, event.clickCount == 1, clickRect.contains(point) {
            pendingClick = (point, event.clickCount)
            return
        }
        interaction?.wrappedValue.mouseDown(clickCount: event.clickCount)
    }

    override func mouseDragged(with event: NSEvent) {
        if let pending = pendingClick {
            let point = topLeft(event)
            guard hypot(point.x - pending.start.x, point.y - pending.start.y) > 3 else { return }
            pendingClick = nil  // a drag: the control's own gesture, from where it started
            lastY = localY(event)
            interaction?.wrappedValue.mouseDown(clickCount: pending.clickCount)
        }
        let y = localY(event)
        let delta = Double(y - lastY)
        lastY = y
        interaction?.wrappedValue.mouseDragged(deltaY: delta, fine: event.modifierFlags.contains(.shift))
    }

    override func mouseUp(with event: NSEvent) {
        if pendingClick != nil {
            pendingClick = nil
            onClickRect?()
            return
        }
        interaction?.wrappedValue.mouseUp()
    }

    override func scrollWheel(with event: NSEvent) {
        guard event.momentumPhase.isEmpty, let interaction else { return }
        // Shift turns a vertical wheel into horizontal scrolling on macOS.
        var delta = Double(event.scrollingDeltaY != 0 ? event.scrollingDeltaY : event.scrollingDeltaX)
        // A wheel reports lines; a trackpad reports points.
        if event.hasPreciseScrollingDeltas { delta /= EG13ControlInteraction.pointsPerScrollLine }
        let fine = event.modifierFlags.contains(.shift)
        let phase: EG13ScrollPhase
        if event.phase.contains(.began) {
            phase = .began
        } else if event.phase.contains(.ended) || event.phase.contains(.cancelled) {
            phase = .ended
        } else if event.phase.contains(.changed) {
            phase = .changed
        } else if event.phase.isEmpty {
            phase = .discrete
        } else {
            return  // .mayBegin, .stationary
        }
        interaction.wrappedValue.scroll(lines: delta, fine: fine, phase: phase)
    }
}

/// Behaviour shared by EG13Knob and EG13Stepped: keyboard focus and increments, and
/// VoiceOver label, value and adjustable action.
struct EG13ControlAccess: ViewModifier {
    let interaction: Binding<EG13ControlInteraction>
    let label: String
    let valueText: String
    let focus: FocusState<Bool>.Binding

    func body(content: Content) -> some View {
        content
            .focusable()
            .focused(focus)
            .focusEffectDisabled()
            .onKeyPress(keys: [.upArrow, .rightArrow, .downArrow, .leftArrow]) { press in
                let up = press.key == .upArrow || press.key == .rightArrow
                interaction.wrappedValue.increment(by: up ? 1 : -1, fine: press.modifiers.contains(.shift))
                return .handled
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(label)
            .accessibilityValue(valueText)
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: interaction.wrappedValue.increment(by: 1)
                case .decrement: interaction.wrappedValue.increment(by: -1)
                @unknown default: break
                }
            }
    }
}

private struct EG13InteractiveControlsKey: EnvironmentKey {
    static let defaultValue = true
}

extension EnvironmentValues {
    /// False when a view is rendered to an image (the preview's --snapshot): AppKit-backed
    /// input surfaces cannot be drawn by ImageRenderer, and there is nothing to click.
    public var eg13InteractiveControls: Bool {
        get { self[EG13InteractiveControlsKey.self] }
        set { self[EG13InteractiveControlsKey.self] = newValue }
    }
}

extension View {
    /// Mouse and scroll input for a control, unless the environment is non-interactive.
    func eg13ControlInput(_ interaction: Binding<EG13ControlInteraction>, clickRect: CGRect? = nil,
                          onClickRect: (() -> Void)? = nil) -> some View {
        modifier(EG13ControlInput(interaction: interaction, clickRect: clickRect, onClickRect: onClickRect))
    }
}

private struct EG13ControlInput: ViewModifier {
    let interaction: Binding<EG13ControlInteraction>
    let clickRect: CGRect?
    let onClickRect: (() -> Void)?
    @Environment(\.eg13InteractiveControls) private var interactive

    func body(content: Content) -> some View {
        if interactive {
            content.overlay(EG13ControlSurface(interaction: interaction, clickRect: clickRect, onClickRect: onClickRect))
        } else {
            content
        }
    }
}
