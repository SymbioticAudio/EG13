import CoreGraphics
import EG13CBridge
import Observation
import Testing
@testable import EG13UI

/// The model behind the modulation visuals: chips per card, the ring's geometry, the trace's
/// scale, and the rule that only modulated knobs re-render on a modulation change.
@MainActor
struct EG13ModulationViewTests {
    private final class Flag: @unchecked Sendable { var fired = false }

    static let velocity: UInt16 = 1
    static let modWheel: UInt16 = 9
    static let mod1: UInt16 = 32
    static let filterP1: UInt16 = 32
    static let filterP2: UInt16 = 33
    static let filterP4: UInt16 = 35
    static let sourceP1: UInt16 = 16

    let core: EG13FakeCore
    let synth: EG13Synth

    /// The full fake with the Analog Filter (3 P's) and a Mod 1 Envelope in both parts.
    init() {
        core = EG13FakeCore.fullCatalog()
        for part in 0..<2 {
            core.select(EG13_SLOT_KIND_FILTER, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD1, engine: 1, part: part)
        }
        synth = EG13Synth(core: core, kind: .instrument)
    }

    private func reload() {
        core.generation += 1
        synth.tick()
    }

    private func filterP(_ n: Int, part: Int = 0) -> UInt32 { EG13ParamID.slot(part, EG13_SLOT_KIND_FILTER, n) }

    // MARK: Chips

    @Test("chips list every visible route into the card in route order")
    func chipsInRouteOrder() {
        core.routes[0][0] = .init(source: Self.velocity, destination: Self.filterP2, via: 0)
        core.routes[0][1] = .init(source: Self.modWheel, destination: Self.sourceP1, via: 0)  // another card
        core.routes[0][2] = .init(source: Self.mod1, destination: Self.filterP1, via: 0)
        core.routes[0][3] = .init(source: Self.mod1, destination: Self.filterP4, via: 0)  // a P the engine hides
        core.routes[0][5] = .init(source: 40, destination: Self.filterP1, via: 0)  // Mod 2 is Off: hidden source
        reload()
        let chips = synth.chips(kind: EG13_SLOT_KIND_FILTER, part: 0)
        #expect(chips.map(\.route) == [0, 2], "route order, not source or destination order; hidden routes left out")
        #expect(chips.map(\.text) == ["VEL → RES", "M1 → FREQ"])
        #expect(chips.map(\.source) == [Self.velocity, Self.mod1])
        #expect(synth.chips(kind: EG13_SLOT_KIND_SOURCE, part: 0).map(\.text) == ["MW → MODEL"])
        #expect(synth.chips(kind: EG13_SLOT_KIND_SOURCE, part: 0).map(\.route) == [1])
        #expect(synth.chips(kind: EG13_SLOT_KIND_SHAPER, part: 0).isEmpty)
        #expect(synth.chips(kind: EG13_SLOT_KIND_FILTER, part: 1).isEmpty, "the other part has its own matrix")
    }

    @Test("Pitch and Amp routes show on the Source card; mod slot parameters on that mod's card")
    func pitchAmpAndMods() {
        core.routes[0][0] = .init(source: Self.mod1, destination: 1, via: 0)
        core.routes[0][1] = .init(source: Self.velocity, destination: 2, via: Self.modWheel)
        core.routes[0][2] = .init(source: Self.mod1, destination: 96, via: 0)  // Mod 2 P1
        reload()
        #expect(synth.chips(kind: EG13_SLOT_KIND_SOURCE, part: 0).map(\.text) == ["M1 → PITCH", "VEL → AMP"])
        #expect(EG13MotionChips.card(forDestination: 96) == EG13_SLOT_KIND_MOD2)
        #expect(EG13MotionChips.card(forDestination: 143) == EG13_SLOT_KIND_MOD4)
        #expect(EG13MotionChips.card(forDestination: 3) == nil, "Pan and Part Level are in the bottom bar")
    }

    @Test("source text: M1 for a mod slot's output, M2.2 on a multi-output engine, short names for fixed sources")
    func sourceText() {
        #expect(EG13MotionChips.sourceText(code: 32, name: "Mod 1 Envelope") == "M1")
        #expect(EG13MotionChips.sourceText(code: 40, name: "Mod 2 LFO Out 1") == "M2.1")
        #expect(EG13MotionChips.sourceText(code: 41, name: "Mod 2 LFO Out 2") == "M2.2")
        #expect(EG13MotionChips.sourceText(code: 9, name: "Mod Wheel") == "MW")
        #expect(EG13MotionChips.sourceText(code: 12, name: "CC A") == "CC A")
        #expect(EG13MotionChips.sourceText(code: 99, name: "Strange Thing") == "STRANGE THING")
    }

    @Test("chips never truncate: the first N and a +k chip")
    func chipFit() {
        let texts = ["M1 → CUTOFF", "M2 → RES", "VEL → MODE", "MW → FREQ"]
        let widths = texts.map(EG13ChipFit.width)
        let all = widths.reduce(0, +) + CGFloat(texts.count - 1) * EG13ChipFit.spacing
        #expect(EG13ChipFit.shown(texts: texts, available: all) == 4)
        #expect(EG13ChipFit.shown(texts: texts, available: all + 100) == 4)
        for available in stride(from: CGFloat(0), through: all, by: 3) {
            let shown = EG13ChipFit.shown(texts: texts, available: available)
            guard shown < texts.count else { continue }
            let used = widths.prefix(shown).reduce(0, +) + CGFloat(shown) * EG13ChipFit.spacing
                + EG13ChipFit.width("+\(texts.count - shown)")
            #expect(used <= available || shown == 0, "\(shown) chips and the +k chip fit in \(available)")
        }
        #expect(EG13ChipFit.shown(texts: texts, available: all - 1) < 4)
        #expect(EG13ChipFit.shown(texts: [], available: 0) == 0)
        // One more chip would not fit: the count is the most that does.
        let tight = widths[0] + EG13ChipFit.spacing + EG13ChipFit.width("+3")
        #expect(EG13ChipFit.shown(texts: texts, available: tight) == 1)
        #expect(EG13ChipFit.shown(texts: texts, available: tight - 1) == 0)
    }

    @Test("a chip's level is its source's latest trace max; fixed sources have none")
    func chipLevel() {
        #expect(synth.sourceLevel(part: 0, code: Self.velocity) == nil, "steady")
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 0, "no data yet: dark")
        core.modOutMin[0][0][0] = 0.2
        core.modOutMax[0][0][0] = 0.7
        synth.tick()
        #expect(abs((synth.sourceLevel(part: 0, code: Self.mod1) ?? -1) - 0.7) < 1e-6)
        core.modOutMax[0][0][0] = 1.7  // over range: clamped
        synth.tick()
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 1)
        core.modOutMin[0][0][0] = .infinity
        core.modOutMax[0][0][0] = -.infinity  // a gap
        synth.tick()
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 0)
        core.modOutMin[0][1][1] = -0.5
        core.modOutMax[0][1][1] = 0.4
        synth.tick()
        #expect(abs((synth.sourceLevel(part: 0, code: 41) ?? -1) - 0.4) < 1e-6, "slot 2, output 2")
        #expect(synth.sourceLevel(part: 1, code: Self.mod1) == 0, "the other part's trace")
    }

    // MARK: Ring

    @Test("ring range is the clamped sum of amounts")
    func ringRange() {
        let id = filterP(2)
        let param = synth.param(id)!
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP2, via: 0)
        core.routes[0][1] = .init(source: Self.velocity, destination: Self.filterP2, via: 0)
        core.values[core.index(of: EG13ParamID.routeAmount(0, 0))] = 0.8
        core.values[core.index(of: EG13ParamID.routeAmount(0, 1))] = 0.5
        core.values[core.index(of: id)] = 0.5
        reload()
        #expect(param.modulationRange == 0.5...1.0, "0.5 + 1.3 stops at 100 %")
        #expect(param.modRing == EG13ModRing(from: 0.5, to: 1.0, dot: nil), "no live value: the range")
        core.values[core.index(of: EG13ParamID.routeAmount(0, 0))] = -0.8
        core.values[core.index(of: EG13ParamID.routeAmount(0, 1))] = -0.5
        synth.tick()
        #expect(param.modulationRange == 0.0...0.5, "0.5 - 1.3 stops at 0 %")
        #expect(synth.param(filterP(1))?.modRing == nil, "an unrouted knob has no ring")
    }

    @Test("with a live value the ring runs from the set value to it, with a dot there")
    func ringLive() {
        #expect(EG13ModRing.make(base: 0.4, live: 0.7, range: 0.4...0.9) == EG13ModRing(from: 0.4, to: 0.7, dot: 0.7))
        #expect(EG13ModRing.make(base: 0.4, live: 0.1, range: 0.0...0.4) == EG13ModRing(from: 0.1, to: 0.4, dot: 0.1))
        #expect(EG13ModRing.make(base: 0.4, live: 0.4, range: 0.4...0.9) == EG13ModRing(from: 0.4, to: 0.4, dot: 0.4))
        #expect(EG13ModRing.make(base: 0.4, live: nil, range: 0.4...0.9) == EG13ModRing(from: 0.4, to: 0.9, dot: nil))
        #expect(EG13ModRing.make(base: 0.4, live: nil, range: nil) == nil)
        #expect(EG13ModRing.make(base: 0.4, live: 1.4, range: nil) == EG13ModRing(from: 0.4, to: 1, dot: 1), "clamped")
    }

    // MARK: Trace scale

    @Test("a trace is centred when any output value is negative, bottom-up otherwise")
    func traceScale() {
        let trace = EG13ModTrace()
        #expect(!EG13ModTraceScale.isBipolar(trace, output: 0), "an empty trace")
        trace.push(min: [0.1], max: [0.9])
        #expect(!EG13ModTraceScale.isBipolar(trace, output: 0))
        trace.push(min: [-0.3], max: [0.2])
        #expect(EG13ModTraceScale.isBipolar(trace, output: 0))
        #expect(!EG13ModTraceScale.isBipolar(trace, output: 1), "another output holds only gaps")
        #expect(EG13ModTraceScale.y(0.5, height: 20, bipolar: false) == 10)
        #expect(EG13ModTraceScale.y(0, height: 20, bipolar: true) == 10)
        #expect(EG13ModTraceScale.y(1, height: 20, bipolar: true) == 0)
        #expect(EG13ModTraceScale.y(-1, height: 20, bipolar: true) == 20)
        #expect(EG13ModTraceScale.y(1.5, height: 20, bipolar: false) == 0, "clamped")
    }

    @Test("trace frames are read in place: latest and by position")
    func traceAccess() {
        let trace = EG13ModTrace()
        #expect(trace.frameCount == 0 && trace.latest(output: 0) == nil)
        for i in 0..<70 { trace.push(min: [Float(i)], max: [Float(i) + 1]) }
        #expect(trace.frameCount == 60)
        #expect(trace.frame(output: 0, at: 0) == EG13ModTraceFrame(min: 10, max: 11), "the oldest")
        #expect(trace.latest(output: 0) == EG13ModTraceFrame(min: 69, max: 70))
        #expect(trace.frame(output: 0, at: 60) == nil && trace.frame(output: 9, at: 0) == nil)
    }

    // MARK: Observation isolation

    @Test("only modulated knobs re-render on a modulation change")
    func ringIsolation() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        reload()
        core.modValues[0][Int(Self.filterP1)] = 0.2
        synth.tick()
        let x = synth.param(filterP(1))!  // modulated
        let y = synth.param(filterP(2))!  // not
        let other = synth.param(EG13ParamID.source(0, 1))!
        let touchedX = Flag(), touchedY = Flag(), touchedOther = Flag()
        /// What a knob's body reads from its parameter.
        func read(_ param: EG13Param) {
            _ = (param.value, param.text, param.normalized, param.isBipolar, param.modRing)
        }
        func track() {
            touchedX.fired = false; touchedY.fired = false; touchedOther.fired = false
            withObservationTracking { read(x) } onChange: { touchedX.fired = true }
            withObservationTracking { read(y) } onChange: { touchedY.fired = true }
            withObservationTracking { read(other) } onChange: { touchedOther.fired = true }
        }
        track()
        core.modValues[0][Int(Self.filterP1)] = 0.5
        synth.tick()
        #expect(touchedX.fired, "the modulated knob re-renders (the test can fail)")
        #expect(!touchedY.fired && !touchedOther.fired, "the others do not")
        track()
        for step in 1...20 {  // twenty ticks of movement and trace data
            core.modValues[0][Int(Self.filterP1)] = Float(0.5 + 0.02 * Double(step))
            core.modOutMin[0][0][0] = 0
            core.modOutMax[0][0][0] = Float(step) / 20
            synth.tick()
        }
        #expect(touchedX.fired && !touchedY.fired && !touchedOther.fired)
    }
}
