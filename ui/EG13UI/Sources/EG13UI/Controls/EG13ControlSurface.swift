import AppKit
import SwiftUI

/// Mouse and scroll input for a control, forwarded to its EG13ControlInteraction.
/// An NSView so it gets click counts, modifier flags, scroll phases and first-mouse clicks
/// in an inactive plugin window, which SwiftUI gestures on macOS 14 do not provide.
struct EG13ControlSurface: NSViewRepresentable {
    let interaction: EG13ControlInteraction

    func makeNSView(context: Context) -> EG13ControlSurfaceView {
        let view = EG13ControlSurfaceView()
        view.interaction = interaction
        return view
    }

    func updateNSView(_ view: EG13ControlSurfaceView, context: Context) {
        view.interaction = interaction
    }
}

final class EG13ControlSurfaceView: NSView {
    var interaction: EG13ControlInteraction?
    private var lastY: CGFloat = 0

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    private func localY(_ event: NSEvent) -> CGFloat {
        let y = convert(event.locationInWindow, from: nil).y
        return isFlipped ? -y : y  // positive upwards
    }

    override func mouseDown(with event: NSEvent) {
        lastY = localY(event)
        interaction?.mouseDown(clickCount: event.clickCount)
    }

    override func mouseDragged(with event: NSEvent) {
        let y = localY(event)
        let delta = Double(y - lastY)
        lastY = y
        interaction?.mouseDragged(deltaY: delta, fine: event.modifierFlags.contains(.shift))
    }

    override func mouseUp(with event: NSEvent) {
        interaction?.mouseUp()
    }

    override func scrollWheel(with event: NSEvent) {
        guard event.momentumPhase.isEmpty, let interaction else { return }
        // Shift turns a vertical wheel into horizontal scrolling on macOS.
        var delta = Double(event.scrollingDeltaY != 0 ? event.scrollingDeltaY : event.scrollingDeltaX)
        if !event.hasPreciseScrollingDeltas { delta *= EG13ControlInteraction.pointsPerScrollStep }
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
        interaction.scroll(deltaY: delta, fine: fine, phase: phase)
    }
}

/// Behaviour shared by EG13Knob, EG13Slider and EG13Stepper: keyboard focus and
/// increments, and VoiceOver label, value and adjustable action.
struct EG13ControlAccess: ViewModifier {
    let interaction: EG13ControlInteraction
    let focus: FocusState<Bool>.Binding

    func body(content: Content) -> some View {
        content
            .focusable()
            .focused(focus)
            .focusEffectDisabled()
            .onKeyPress(keys: [.upArrow, .rightArrow, .downArrow, .leftArrow]) { press in
                let up = press.key == .upArrow || press.key == .rightArrow
                interaction.increment(by: up ? 1 : -1, fine: press.modifiers.contains(.shift))
                return .handled
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(interaction.param.accessibilityLabel)
            .accessibilityValue(interaction.param.text)
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: interaction.increment(by: 1)
                case .decrement: interaction.increment(by: -1)
                @unknown default: break
                }
            }
    }
}
