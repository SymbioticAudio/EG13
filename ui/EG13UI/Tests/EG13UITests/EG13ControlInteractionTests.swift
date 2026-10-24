import Testing
@testable import EG13UI

@MainActor
struct EG13ControlInteractionTests {
    let core = EG13FakeCore.standard()

    func interaction(_ id: UInt32) -> EG13ControlInteraction {
        let i = core.index(of: id)
        return EG13ControlInteraction(param: EG13Param(core: core, index: i, descriptor: core.descriptors[i],
                                                       value: core.values[i]))
    }

    @Test func aDragIsOneGestureAndSweepsTheRangeIn200Points() {
        let knob = interaction(EG13ParamID.source(0, 2))  // 0.5
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 50, fine: false)
        knob.mouseDragged(deltaY: 20, fine: false)
        knob.mouseUp()
        #expect(abs(knob.param.value - 0.85) < 1e-9)
        let id = knob.param.id
        #expect(core.calls.first == .begin(id))
        #expect(core.calls.last == .end(id))
        #expect(core.calls.filter { if case .edit = $0 { return true } else { return false } }.count == 2)
    }

    @Test func shiftDragsTenTimesSlower() {
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 100, fine: true)
        knob.mouseUp()
        #expect(abs(knob.param.value - 0.55) < 1e-9)
    }

    @Test func draggingPastTheEndRespondsAtOnceWhenReversed() {
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: 400, fine: false)
        #expect(knob.param.value == 1)
        knob.mouseDragged(deltaY: -20, fine: false)
        #expect(abs(knob.param.value - 0.9) < 1e-9)
        knob.mouseUp()
    }

    @Test func doubleClickResetsToTheDefaultInOneGesture() {
        let knob = interaction(EG13ParamID.source(0, 1))  // default 0.25
        knob.param.setValue(0.9)
        core.calls.removeAll()
        knob.mouseDown(clickCount: 1)
        knob.mouseUp()
        knob.mouseDown(clickCount: 2)
        knob.mouseDragged(deltaY: 30, fine: false)  // ignored after a double-click
        knob.mouseUp()
        #expect(knob.param.value == 0.25)
        let id = knob.param.id
        #expect(core.calls == [.begin(id), .end(id), .begin(id), .edit(id, 0.25), .end(id)])
    }

    @Test func aTrackpadScrollIsOneGesture() {
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.scroll(deltaY: 10, fine: false, phase: .began)
        knob.scroll(deltaY: 10, fine: false, phase: .changed)
        knob.scroll(deltaY: 0, fine: false, phase: .ended)
        #expect(abs(knob.param.value - 0.6) < 1e-9)
        let id = knob.param.id
        #expect(core.calls.first == .begin(id))
        #expect(core.calls.last == .end(id))
        #expect(core.calls.count == 4)
    }

    @Test func eachWheelNotchIsItsOwnGesture() {
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.scroll(deltaY: EG13ControlInteraction.pointsPerScrollStep, fine: false, phase: .discrete)
        knob.scroll(deltaY: EG13ControlInteraction.pointsPerScrollStep, fine: false, phase: .discrete)
        #expect(abs(knob.param.value - 0.6) < 1e-9)
        #expect(core.calls.count == 6)
    }

    @Test func steppedParametersScrollOneStepPerTenPoints() {
        let voices = interaction(EG13ParamID.part(0, .voiceCount))  // 8
        voices.scroll(deltaY: -4, fine: false, phase: .began)
        #expect(voices.param.value == 8)
        voices.scroll(deltaY: -7, fine: false, phase: .changed)
        #expect(voices.param.value == 7)
        voices.scroll(deltaY: -20, fine: false, phase: .ended)
        #expect(voices.param.value == 5)
    }

    @Test func keysMoveOneStepOrOnePercentAndStopAtTheEnds() {
        let voices = interaction(EG13ParamID.part(0, .voiceCount))
        voices.increment(by: -1)
        #expect(voices.param.value == 7)
        voices.increment(by: 5)
        #expect(voices.param.value == 8)
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.increment(by: 1)
        #expect(abs(knob.param.value - 0.51) < 1e-9)
        knob.increment(by: 1, fine: true)
        #expect(abs(knob.param.value - 0.511) < 1e-9)
    }

    @Test func aKeyPressDuringADragDoesNotEndTheDrag() {
        let knob = interaction(EG13ParamID.source(0, 2))
        knob.mouseDown(clickCount: 1)
        knob.increment(by: 1)
        #expect(knob.param.isEditing)
        knob.mouseUp()
        #expect(!knob.param.isEditing)
    }
}
