import Observation
import Testing
@testable import EG13UI

/// The observation unit is the parameter: a control reading parameter Y is invalidated only by
/// changes to Y (final-review I1). Every test tracks everything a knob's body reads from Y,
/// changes X every way a parameter can change, and expects Y's tracking never to fire.
@MainActor
struct EG13ObservationIsolationTests {
    let core = EG13FakeCore.standard()
    let synth: EG13Synth
    let x: EG13Param
    let y: EG13Param

    init() {
        synth = EG13Synth(core: core, kind: .instrument, clock: { 100 })
        x = synth.partParam(0, .level)!
        y = synth.masterParam(.level)!
    }

    /// Reads what a knob and a stepper read from `param`.
    private func readAll(_ param: EG13Param) {
        _ = (param.value, param.text, param.normalized, param.defaultNormalized, param.isBipolar,
             param.stepCount, param.descriptor, param.name, param.isEditing, param.accessibilityLabel)
    }

    private final class Flag: @unchecked Sendable { var fired = false }

    /// Starts tracking `param`; the flag is set when anything the control read changes.
    private func track(_ param: EG13Param) -> Flag {
        let flag = Flag()
        withObservationTracking { readAll(param) } onChange: { flag.fired = true }
        return flag
    }

    @Test("a drag, its end and the pending hold of X never invalidate Y")
    func editsOfXDoNotTouchY() {
        let flag = track(y)
        x.beginEdit()
        x.setValue(0.4)
        x.setValue(0.8)
        x.endEdit()
        for _ in 0..<(EG13Synth.pendingTicks + 2) { synth.tick() }  // the hold expires
        #expect(!flag.fired)
    }

    @Test("a snapshot change to X (host automation) never invalidates Y")
    func snapshotOfXDoesNotTouchY() {
        let flag = track(y)
        core.values[core.index(of: x.id)] = 0.35
        synth.tick()
        core.values[core.index(of: x.id)] = 0.36
        synth.tick()
        #expect(x.value == 0.36)
        #expect(!flag.fired)
    }

    @Test("an unchanged tick invalidates nothing, not even X")
    func unchangedTickIsSilent() {
        let flagX = track(x)
        let flagY = track(y)
        synth.tick()
        synth.tick()
        #expect(!flagX.fired && !flagY.fired)
    }

    @Test("the tracking does fire for Y's own changes (the test can fail)")
    func yOwnChangesFire() {
        let viaSnapshot = track(y)
        core.values[core.index(of: y.id)] = 0.9
        synth.tick()
        #expect(viaSnapshot.fired)
        let viaEdit = track(y)
        y.setValue(0.5)
        #expect(viaEdit.fired)
    }

    @Test("an edit that does not change the shown value writes nothing")
    func sameValueEditIsSilent() {
        x.beginEdit()
        x.setValue(0.4)
        let flag = track(x)
        synth.edit(x.id, 0.4)  // the same value again
        #expect(!flag.fired)
        x.endEdit()
    }

    @Test("text is cached: one core call per change of the shown value")
    func textFollowsValue() {
        x.setValue(0.5)
        #expect(x.text == core.paramText(at: x.index, value: 0.5))
        let before = core.textCalls
        _ = (x.text, x.text, x.text)
        #expect(core.textCalls == before)
        x.setValue(0.5)  // unchanged
        #expect(core.textCalls == before)
        x.beginEdit()
        let opened = core.textCalls
        x.setValue(0.6)
        #expect(core.textCalls == opened + 1)
        x.endEdit()
    }
}
