import AppKit
import SwiftUI
import Testing
@testable import EG13UI

/// A value behind an interaction, recording every callback.
@MainActor
final class Recorder {
    enum Event: Equatable { case begin, edit(Double), end }
    var value: Double
    var events: [Event] = []

    init(value: Double) { self.value = value }

    func interaction(min: Double = 0, max: Double = 1, defaultValue: Double = 0.5,
                     stepped: Bool = false) -> EG13ControlInteraction {
        EG13ControlInteraction(
            minValue: min, maxValue: max, defaultValue: defaultValue, isStepped: stepped,
            current: { self.value }, begin: { self.events.append(.begin) },
            edit: { self.value = $0; self.events.append(.edit($0)) }, end: { self.events.append(.end) })
    }

    var begins: Int { events.filter { $0 == .begin }.count }
    var ends: Int { events.filter { $0 == .end }.count }
    var edits: [Double] { events.compactMap { if case .edit(let v) = $0 { return v } else { return nil } } }
}

@MainActor
struct EG13ControlInteractionTests {
    @Test func dragOf200PointsSpansTheRange() {
        let r = Recorder(value: 0)
        var knob = r.interaction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 120, fine: false)
        knob.mouseDragged(deltaY: 80, fine: false)
        knob.mouseUp()
        #expect(abs(r.value - 1) < 1e-9)
        #expect(r.events.first == .begin)
        #expect(r.events.last == .end)
        #expect(r.edits.count == 2)
        // Half the distance is half the range, from wherever the drag started.
        let half = Recorder(value: 0.25)
        var other = half.interaction()
        other.mouseDown(clickCount: 1)
        other.mouseDragged(deltaY: 100, fine: false)
        other.mouseUp()
        #expect(abs(half.value - 0.75) < 1e-9)
    }

    @Test func shiftDragIsTenTimesFiner() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 100, fine: true)
        knob.mouseUp()
        #expect(abs(r.value - 0.55) < 1e-9)
    }

    @Test func draggingPastTheEndRespondsAtOnceWhenReversed() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 400, fine: false)
        #expect(r.value == 1)
        knob.mouseDragged(deltaY: -20, fine: false)
        #expect(abs(r.value - 0.9) < 1e-9)
        knob.mouseUp()
    }

    @Test func doubleClickResetsToTheDefaultInOneGesture() {
        let r = Recorder(value: 0.9)
        var knob = r.interaction(defaultValue: 0.25)
        knob.mouseDown(clickCount: 1)
        knob.mouseUp()
        r.events.removeAll()
        knob.mouseDown(clickCount: 2)
        knob.mouseDragged(deltaY: 30, fine: false)  // ignored after a double-click
        knob.mouseUp()
        #expect(r.value == 0.25)
        #expect(r.events == [.begin, .edit(0.25), .end])
    }

    @Test func doubleClickAtTheDefaultSendsNothing() {
        let r = Recorder(value: 0.25)
        var knob = r.interaction(defaultValue: 0.25)
        knob.mouseDown(clickCount: 2)
        knob.mouseDragged(deltaY: 30, fine: false)
        knob.mouseUp()
        #expect(r.events.isEmpty)
    }

    @Test func steppedValuesSnapToIntegerSteps() {
        let r = Recorder(value: 1)
        var voices = r.interaction(min: 1, max: 8, defaultValue: 8, stepped: true)
        voices.mouseDown(clickCount: 1)
        voices.mouseDragged(deltaY: 40, fine: false)  // 0.2 of 7 steps = 1.4 -> 2
        #expect(r.value == 2)
        voices.mouseDragged(deltaY: 40, fine: false)  // 2.8 -> 4
        #expect(r.value == 4)
        voices.mouseDragged(deltaY: 400, fine: false)
        #expect(r.value == 8)
        voices.mouseUp()
        #expect(r.edits.allSatisfy { $0 == $0.rounded() })
        // Dragging within one step sends nothing new.
        let still = Recorder(value: 4)
        var hold = still.interaction(min: 1, max: 8, defaultValue: 8, stepped: true)
        hold.mouseDown(clickCount: 1)
        hold.mouseDragged(deltaY: 2, fine: false)
        hold.mouseUp()
        #expect(still.edits.isEmpty)
    }

    @Test func everyGestureIsBeginEndBalancedIncludingACancelledDrag() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction(defaultValue: 0.2)
        knob.mouseDown(clickCount: 1); knob.mouseDragged(deltaY: 10, fine: false); knob.mouseUp()
        knob.mouseDown(clickCount: 2); knob.mouseUp()
        knob.scroll(lines: 1, fine: false, phase: .began)
        knob.scroll(lines: 1, fine: false, phase: .changed)
        knob.scroll(lines: 0, fine: false, phase: .ended)
        knob.scroll(lines: 1, fine: false, phase: .discrete)
        knob.increment(by: 1)
        #expect(r.begins == r.ends)
        // A cancelled drag ends where it is, once, and a second cancel does nothing.
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 20, fine: false)
        let before = r.value
        knob.cancel()
        knob.cancel()
        knob.mouseUp()
        #expect(r.begins == r.ends)
        #expect(r.value == before)
        #expect(r.events.last == .end)
        // Further drag events after the cancel change nothing.
        knob.mouseDragged(deltaY: 50, fine: false)
        #expect(r.value == before)
        // A cancel with no gesture open sends nothing.
        let count = r.events.count
        knob.cancel()
        #expect(r.events.count == count)
    }

    @Test func aTrackpadScrollIsOneGestureAndALineIsOneHundredthOfTheRange() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.scroll(lines: 1, fine: false, phase: .began)
        knob.scroll(lines: 1, fine: false, phase: .changed)
        knob.scroll(lines: 0, fine: false, phase: .ended)
        #expect(abs(r.value - 0.52) < 1e-9)
        #expect(r.events.first == .begin && r.events.last == .end)
        #expect(r.begins == 1)
    }

    @Test func eachWheelNotchIsItsOwnGestureAndShiftIsFiner() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.scroll(lines: 1, fine: false, phase: .discrete)
        knob.scroll(lines: 1, fine: true, phase: .discrete)
        #expect(abs(r.value - 0.511) < 1e-9)
        #expect(r.begins == 2 && r.ends == 2)
    }

    @Test func steppedParametersScrollOneStepPerLine() {
        let r = Recorder(value: 8)
        var voices = r.interaction(min: 1, max: 8, defaultValue: 8, stepped: true)
        voices.scroll(lines: -0.4, fine: false, phase: .began)
        #expect(r.value == 8)
        voices.scroll(lines: -0.7, fine: false, phase: .changed)
        #expect(r.value == 7)
        voices.scroll(lines: -2, fine: false, phase: .ended)
        #expect(r.value == 5)
    }

    @Test func keysMoveOneStepOrOneHundredthAndStopAtTheEnds() {
        let r = Recorder(value: 7)
        var voices = r.interaction(min: 1, max: 8, defaultValue: 8, stepped: true)
        voices.increment(by: 5)
        #expect(r.value == 8)
        voices.increment(by: -1)
        #expect(r.value == 7)
        let c = Recorder(value: 0.5)
        var knob = c.interaction()
        knob.increment(by: 1)
        #expect(abs(c.value - 0.51) < 1e-9)
        knob.increment(by: 1, fine: true)
        #expect(abs(c.value - 0.511) < 1e-9)
        // At the end, nothing changes and no gesture is opened.
        let top = Recorder(value: 1)
        var end = top.interaction()
        end.increment(by: 1)
        #expect(top.events.isEmpty)
    }

    @Test func aKeyPressDuringADragJoinsTheDrag() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.mouseDown(clickCount: 1)
        knob.increment(by: 1)
        #expect(knob.isInGesture)
        knob.mouseUp()
        #expect(!knob.isInGesture)
        #expect(r.begins == 1 && r.ends == 1)
    }

    @Test func aParameterInteractionSendsGesturesToTheCore() {
        let core = EG13FakeCore.standard()
        let synth = EG13Synth(core: core, kind: .instrument)
        let param = synth.param(EG13ParamID.source(0, 2))!
        var knob = param.makeInteraction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 50, fine: false)
        knob.mouseUp()
        #expect(abs(param.value - 0.75) < 1e-9)
        #expect(core.calls == [.begin(param.id), .edit(param.id, 0.75), .end(param.id)])
    }

    @Test func dragClampsAtTheMinimumAndSteppedDoubleClickResets() {
        let r = Recorder(value: 0.1)
        var knob = r.interaction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: -400, fine: false)
        #expect(r.value == 0)
        knob.mouseDragged(deltaY: 20, fine: false)
        #expect(abs(r.value - 0.1) < 1e-9)
        knob.mouseUp()
        let v = Recorder(value: 3)
        var voices = v.interaction(min: 1, max: 8, defaultValue: 8, stepped: true)
        voices.mouseDown(clickCount: 2)
        voices.mouseUp()
        #expect(v.events == [.begin, .edit(8), .end])
    }

    @Test func scrollPhasesWithoutABeginStillEditInsideAGesture() {
        let r = Recorder(value: 0.5)
        var knob = r.interaction()
        knob.scroll(lines: 1, fine: false, phase: .changed)
        knob.scroll(lines: 1, fine: false, phase: .ended)
        #expect(r.begins == 1 && r.ends == 1)
        #expect(r.events.first == .begin && r.events.last == .end)
    }

    @Test func replacingAnInteractionMidGestureEndsTheOldOne() {
        // What the knob does when the parameter's descriptor changes (an engine switch).
        let r = Recorder(value: 0.5)
        var old = r.interaction()
        old.mouseDown(clickCount: 1)
        old.mouseDragged(deltaY: 10, fine: false)
        old.cancel()
        old = r.interaction()
        #expect(r.begins == 1 && r.ends == 1)
        #expect(!old.isInGesture)
    }

    @Test func tearingDownTheSurfaceMidDragEndsTheGestureEvenWithStaleState() {
        let r = Recorder(value: 0.5)
        let live = r.interaction()
        // A binding whose state is already gone: reads give a fresh copy, writes are lost.
        let stale = Binding<EG13ControlInteraction>(get: { live }, set: { _ in })
        let view = EG13ControlSurfaceView(frame: NSRect(x: 0, y: 0, width: 40, height: 40))
        view.interaction = stale
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 40, height: 40),
                              styleMask: [.titled], backing: .buffered, defer: false)
        window.contentView?.addSubview(view)
        let down = NSEvent.mouseEvent(with: .leftMouseDown, location: NSPoint(x: 20, y: 20), modifierFlags: [],
                                      timestamp: 0, windowNumber: window.windowNumber, context: nil,
                                      eventNumber: 0, clickCount: 1, pressure: 1)!
        view.mouseDown(with: down)
        #expect(r.begins == 1 && r.ends == 0)
        view.removeFromSuperview()
        #expect(r.begins == 1 && r.ends == 1)
        // A second teardown path (SwiftUI's dismantle) does not end it twice.
        EG13ControlSurface.dismantleNSView(view, coordinator: ())
        #expect(r.ends == 1)
    }

    @Test func dismantlingTheSurfaceMidDragEndsTheGesture() {
        let r = Recorder(value: 0.5)
        let live = r.interaction()
        let view = EG13ControlSurfaceView(frame: .zero)
        view.interaction = Binding(get: { live }, set: { _ in })
        let down = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0,
                                      windowNumber: 0, context: nil, eventNumber: 0, clickCount: 1, pressure: 1)!
        view.mouseDown(with: down)
        EG13ControlSurface.dismantleNSView(view, coordinator: ())
        #expect(r.begins == 1 && r.ends == 1)
    }
}
