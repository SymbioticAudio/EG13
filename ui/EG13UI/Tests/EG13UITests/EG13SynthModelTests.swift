import EG13CBridge
import Testing
@testable import EG13UI

/// A clock the tests move by hand, so meter timing needs no sleeps.
@MainActor
final class EG13TestClock {
    var now = 100.0
}

@MainActor
struct EG13SynthModelTests {
    let core = EG13FakeCore.standard()
    let clock = EG13TestClock()
    let synth: EG13Synth

    init() {
        let clock = clock
        synth = EG13Synth(core: core, kind: .instrument, clock: { clock.now })
    }

    var source: EG13SlotKind { EG13_SLOT_KIND_SOURCE }

    @Test("Engine change refreshes descriptors and hides unused P's")
    func engineChangeRefreshesDescriptors() {
        let first = synth.slot(source, part: 0)
        #expect(first.engine == 0)
        #expect(first.engineNames == ["Macro Oscillator", "Waveshaper", "Chord"])
        #expect(first.descriptors.map(\.id) == (1...8).map { EG13ParamID.source(0, $0) })

        // The plugin switches engine and renames P5; without a new generation nothing is re-read.
        core.currentSource[0] = 1
        core.names[core.index(of: EG13ParamID.source(0, 5))] = "Density"
        synth.tick()
        #expect(synth.slot(source, part: 0).engine == 0)
        #expect(synth.name(EG13ParamID.source(0, 5)) == "P5")

        core.generation += 1
        synth.tick()
        let second = synth.slot(source, part: 0)
        #expect(second.engine == 1)
        #expect(second.engineName == "Waveshaper")
        #expect(second.descriptors.map(\.id) == (1...5).map { EG13ParamID.source(0, $0) })
        #expect(synth.name(EG13ParamID.source(0, 5)) == "Density")
        #expect(synth.sourceParams(part: 0).count == 5)
    }

    @Test("Snapshot updates do not emit edits")
    func snapshotUpdatesDoNotEmitEdits() {
        let level = EG13ParamID.part(0, .level)
        core.values[core.index(of: level)] = 0.3
        core.generation += 1
        synth.tick()
        core.values[core.index(of: level)] = 0.6
        synth.tick()
        #expect(synth.value(level) == 0.6)
        #expect(synth.text(level) == "60%")
        #expect(core.calls.isEmpty)
    }

    @Test("a dragged knob ignores the snapshot until end")
    func draggedKnobIgnoresSnapshotUntilEnd() {
        let level = EG13ParamID.part(0, .level)
        let i = core.index(of: level)
        synth.begin(level)
        synth.edit(level, 0.2)
        core.values[i] = 0.9  // automation while dragging
        synth.tick()
        #expect(synth.value(level) == 0.2)
        #expect(synth.text(level) == "20%")
        synth.end(level)
        #expect(synth.value(level) == 0.2)  // held: the host has not echoed the edit yet
        for _ in 0..<EG13Synth.pendingTicks { synth.tick() }
        #expect(synth.value(level) == 0.9)
        #expect(core.calls == [.begin(level), .edit(level, 0.2), .end(level)])

        // A cancelled gesture (the knob torn down mid-drag) clears the override too.
        let param = synth.param(level)!
        var knob = param.makeInteraction()
        knob.mouseDown(clickCount: 1)
        knob.mouseDragged(deltaY: -100, fine: false)
        core.values[i] = 0.7
        synth.tick()
        #expect(synth.value(level) != 0.7)
        knob.cancel()
        for _ in 0..<EG13Synth.pendingTicks { synth.tick() }
        #expect(synth.value(level) == 0.7)
        #expect(core.calls.filter { $0 == .begin(level) }.count == 2)
        #expect(core.calls.filter { $0 == .end(level) }.count == 2)
    }

    @Test("meters decay and hold")
    func metersDecayAndHold() {
        /// n display frames at 30 Hz.
        func advance(frames: Int) {
            for _ in 0..<frames {
                clock.now += 1.0 / 30
                synth.tick()
            }
        }
        core.peaks = [[1, 0.5], [0, 0]]
        clock.now += 1.0 / 30
        synth.tick()
        #expect(synth.meters[0] == [1, 0.5])
        #expect(synth.meterHolds[0] == [1, 0.5])
        #expect(synth.meters[1] == [0, 0])

        advance(frames: 5)  // 167 ms
        // dB-linear: full scale to the -60 dB floor in 300 ms, so 5 frames is -33.3 dB.
        #expect(abs(EG13MeterBallistics.decibels(synth.meters[0][0]) - (-200.0 * 5 / 30)) < 0.01)
        // The -6 dB peak falls from its own level to the floor in 300 ms: 180 dB/s.
        #expect(abs(EG13MeterBallistics.decibels(synth.meters[0][1]) - (-6.0206 - 180 * 5.0 / 30)) < 0.05)
        #expect(synth.meterHolds[0] == [1, 0.5])

        advance(frames: 37)  // 1.4 s after the peak: still held
        #expect(synth.meters[0] == [0, 0])
        #expect(synth.meterHolds[0] == [1, 0.5])

        advance(frames: 9)  // 1.7 s: past the 1.5 s hold, falling
        #expect(synth.meterHolds[0][0] < 1 && synth.meterHolds[0][0] > 0)

        advance(frames: 12)  // 2.1 s: gone
        #expect(synth.meterHolds[0] == [0, 0])
    }

    @Test("a steady signal below the last peak never reads under itself")
    func meterHoldsASteadySignalBelowThePeak() {
        var meter = EG13MeterBallistics()
        meter.update(peak: 1, dt: 1.0 / 30)
        // -12 dB steady: the decay passes it after a few frames, and from then on the meter
        // must read the signal, never a frame below it.
        for _ in 0..<30 {
            meter.update(peak: 0.25, dt: 1.0 / 30)
            #expect(meter.level >= 0.25)
        }
        #expect(meter.level == 0.25)
    }

    @Test("a stale snapshot does not change a value after end until it catches up")
    func staleSnapshotHeldAfterEnd() {
        let level = EG13ParamID.part(0, .level)
        let i = core.index(of: level)
        synth.begin(level)
        synth.edit(level, 0.2)
        synth.end(level)
        core.values[i] = 1  // the host has not applied the edit yet
        for _ in 0..<5 { synth.tick() }
        #expect(synth.value(level) == 0.2)
        core.values[i] = 0.2  // caught up
        synth.tick()
        #expect(synth.value(level) == 0.2)
        core.values[i] = 0.6  // pending cleared: the host is followed again
        synth.tick()
        #expect(synth.value(level) == 0.6)
        #expect(core.calls == [.begin(level), .edit(level, 0.2), .end(level)])
    }

    @Test("a pending value expires after the timeout")
    func pendingValueExpires() {
        let level = EG13ParamID.part(0, .level)
        synth.begin(level)
        synth.edit(level, 0.2)
        synth.end(level)
        core.values[core.index(of: level)] = 0.9  // the host never applies it
        for _ in 0..<(EG13Synth.pendingTicks - 1) { synth.tick() }
        #expect(synth.value(level) == 0.2)
        synth.tick()
        #expect(synth.value(level) == 0.9)
    }

    @Test("a one-shot edit holds like a drag end, and a new begin replaces the pending value")
    func oneShotEditHolds() {
        let level = EG13ParamID.part(0, .level)
        let i = core.index(of: level)
        synth.param(level)!.setValue(0.4)  // no drag: wrapped in its own gesture
        core.values[i] = 1
        synth.tick()
        #expect(synth.value(level) == 0.4)
        synth.begin(level)  // a new gesture drops the old pending value
        #expect(synth.value(level) == 1)
        synth.end(level)
        #expect(synth.value(level) == 1)
    }
}
