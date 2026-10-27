import CoreGraphics
import EG13CBridge
import Observation
import Testing
@testable import EG13UI

/// The model behind the modulation visuals: Motion lists, the mark's geometry, the trace's
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

    // MARK: Motion list

    @Test("the Motion list has every visible route into the card in route order, in full names")
    func motionListInRouteOrder() {
        core.routes[0][0] = .init(source: Self.velocity, destination: Self.filterP2, via: 0)
        core.routes[0][1] = .init(source: Self.modWheel, destination: Self.sourceP1, via: 0)  // another card
        core.routes[0][2] = .init(source: Self.mod1, destination: Self.filterP1, via: 0)
        core.routes[0][3] = .init(source: Self.mod1, destination: Self.filterP4, via: 0)  // a P the engine hides
        core.routes[0][5] = .init(source: 40, destination: Self.filterP1, via: 0)  // Mod 2 is Off: hidden source
        core.values[core.index(of: EG13ParamID.routeAmount(0, 0))] = -0.05
        core.values[core.index(of: EG13ParamID.routeAmount(0, 2))] = 0.3
        reload()
        let items = synth.motionItems(kind: EG13_SLOT_KIND_FILTER, part: 0)
        #expect(items.count == 2, "the Motion button's n")
        #expect(items.map(\.route) == [0, 2], "route order, not source or destination order; hidden routes left out")
        #expect(items.map(\.source) == [Self.velocity, Self.mod1])
        let names = synth.routes(part: 0)
        #expect(items[0].text == (names[0].sourceName ?? "") + " → "
            + EG13Motion.destinationText(name: names[0].destinationName ?? ""))
        #expect(items[1].text.hasPrefix("Mod 1 ") && items[1].text.contains(" → "), "source and destination in full")
        #expect(!items[1].text.contains(":"), "the destination without its card prefix")
        #expect(items.map(\.amountText) == ["-5 %", "+30 %"])
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_SOURCE, part: 0).map(\.route) == [1])
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_SHAPER, part: 0).isEmpty, "no routes: no button")
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_FILTER, part: 1).isEmpty, "the other part has its own matrix")
    }

    @Test("Pitch and Amp routes list on the Source card; mod slot parameters on that mod's card")
    func pitchAmpAndMods() {
        core.routes[0][0] = .init(source: Self.mod1, destination: 1, via: 0)
        core.routes[0][1] = .init(source: Self.velocity, destination: 2, via: Self.modWheel)
        core.routes[0][2] = .init(source: Self.mod1, destination: 96, via: 0)  // Mod 2 P1
        core.select(EG13_SLOT_KIND_MOD2, engine: 1, part: 0)
        reload()
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_SOURCE, part: 0).map(\.route) == [0, 1])
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_SOURCE, part: 0).map { $0.text.hasSuffix("Pitch") || $0.text.hasSuffix("Amp") }
            == [true, true])
        #expect(EG13Motion.card(forDestination: 96) == EG13_SLOT_KIND_MOD2)
        #expect(EG13Motion.card(forDestination: 143) == EG13_SLOT_KIND_MOD4)
        #expect(EG13Motion.card(forDestination: 3) == nil, "Pan and Part Level are in the bottom bar")
        #expect(synth.motionItems(kind: EG13_SLOT_KIND_MOD2, part: 0).map(\.route) == [2])
    }

    @Test("destination and amount text")
    func motionText() {
        #expect(EG13Motion.destinationText(name: "Filter: Cutoff") == "Cutoff")
        #expect(EG13Motion.destinationText(name: "Pitch") == "Pitch")
        #expect(EG13Motion.amountText(0.3) == "+30 %")
        #expect(EG13Motion.amountText(-0.456) == "-46 %")
        #expect(EG13Motion.amountText(0) == "0 %")
        #expect(EG13Motion.amountText(1) == "+100 %")
    }

    // MARK: 15 Hz

    @Test("traces refresh every second tick; meters every tick")
    func fifteenHertz() {
        let trace = synth.modTrace(part: 0, slot: 0)
        reload()  // a new generation refreshes on its first tick
        let before = trace.frameCount
        synth.tick()  // an off tick
        #expect(trace.frameCount == before, "an off tick pushes no frame")
        synth.tick()  // a refresh tick
        #expect(trace.frameCount == before + 1)
        // A meter moves on every tick.
        core.peaks = [[0.8, 0.8], [0, 0]]
        synth.tick()
        #expect(synth.meters[0][0] > 0, "meters stay at 30 Hz")
    }

    @Test("a trace frame holds the range over both ticks it covers")
    func traceFoldsSkippedTick() {
        let trace = synth.modTrace(part: 0, slot: 0)
        let before = trace.frameCount
        core.modOutMin[0][0][0] = 0.4; core.modOutMax[0][0][0] = 0.5
        synth.tick()  // refresh tick: a frame
        #expect(trace.frameCount == before + 1)
        core.modOutMin[0][0][0] = 0.1; core.modOutMax[0][0][0] = 0.3
        synth.tick()  // off tick: nothing pushed
        #expect(trace.frameCount == before + 1)
        core.modOutMin[0][0][0] = 0.6; core.modOutMax[0][0][0] = 0.9
        synth.tick()
        #expect(trace.frameCount == before + 2)
        #expect(trace.latest(output: 0) == EG13ModTraceFrame(min: 0.1, max: 0.9))
    }

    @Test("a Motion row's level is its source's latest trace max; fixed sources have none")
    func chipLevel() {
        #expect(synth.sourceLevel(part: 0, code: Self.velocity) == nil, "steady")
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 0, "no data yet: dark")
        core.modOutMin[0][0][0] = 0.2
        core.modOutMax[0][0][0] = 0.7
        synth.tickModulation()
        #expect(abs((synth.sourceLevel(part: 0, code: Self.mod1) ?? -1) - 0.7) < 1e-6)
        core.modOutMax[0][0][0] = 1.7  // over range: clamped
        synth.tickModulation()
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 1)
        core.modOutMin[0][0][0] = .infinity
        core.modOutMax[0][0][0] = -.infinity  // a gap
        synth.tickModulation()
        #expect(synth.sourceLevel(part: 0, code: Self.mod1) == 0)
        core.modOutMin[0][1][1] = -0.5
        core.modOutMax[0][1][1] = 0.4
        synth.tickModulation()
        #expect(abs((synth.sourceLevel(part: 0, code: 41) ?? -1) - 0.4) < 1e-6, "slot 2, output 2")
        #expect(synth.sourceLevel(part: 1, code: Self.mod1) == 0, "the other part's trace")
    }

    // MARK: Mark

    @Test("the mark shows only for routed knobs")
    func markOnlyForRoutedKnobs() {
        #expect(synth.param(filterP(1))?.isRouted == false)
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        core.routes[0][1] = .init(source: Self.mod1, destination: Self.filterP4, via: 0)  // a P the engine hides
        core.routes[0][2] = .init(source: 40, destination: Self.filterP2, via: 0)  // hidden source (Mod 2 Off)
        reload()
        synth.tickModulation()
        #expect(synth.param(filterP(1))?.isRouted == true, "routes(into:) is not empty")
        #expect(synth.param(filterP(2))?.isRouted == false, "its only route has a hidden source")
        #expect(synth.param(filterP(1, part: 1))?.isRouted == false, "the other part")
        #expect(synth.routes(into: filterP(1), part: 0).isEmpty == false && synth.routes(into: filterP(2), part: 0).isEmpty)
        #expect(synth.motionItems(into: filterP(1), part: 0).map(\.route) == [0], "the popover lists this knob's routes")
        #expect(synth.motionItems(into: filterP(2), part: 0).isEmpty)
        core.routes[0][0] = .init()
        reload()
        synth.tickModulation()
        #expect(synth.param(filterP(1))?.isRouted == false, "hidden again when the route goes")
    }

    @Test("the mark doesn't change on modulation-value updates")
    func markIgnoresModulationValues() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        reload()
        synth.tickModulation()
        let x = synth.param(filterP(1))!
        let touched = Flag(), knobTouched = Flag()
        /// What the mark reads (a knob's body reads nothing of the modulation).
        withObservationTracking { _ = x.isRouted } onChange: { touched.fired = true }
        withObservationTracking { _ = (x.value, x.text, x.normalized, x.isBipolar, x.isRouted) } onChange: {
            knobTouched.fired = true
        }
        for step in 1...20 {
            core.modValues[0][Int(Self.filterP1)] = Float(0.3 + 0.02 * Double(step))
            synth.tickModulation()
        }
        #expect(!touched.fired && !knobTouched.fired)
        // Positive control: the tracking does fire when the route goes.
        core.routes[0][0] = .init()
        reload()
        synth.tickModulation()
        #expect(touched.fired && knobTouched.fired, "removing the route is seen")
    }

    @Test("the mark is 45 % of the cap wide, at least 2 pt, 1.5 cycles, with a 14 x 10 pt click area")
    func markGeometry() {
        let style = EG13KnobStyle.default
        for size in [EG13KnobSize.large, .medium, .small] {
            let d = EG13Knob.diameter(size)
            let capRadius = EG13ModMarkGeometry.capRadius(d: d, style: style, size: size)
            let mark = EG13ModMarkGeometry.size(capRadius: capRadius)
            #expect(abs(mark.width / (capRadius * 2) - 0.45) < 0.0001)
            #expect(EG13ModMarkGeometry.strokeWidth(capRadius: capRadius) >= 2, "1.5 pt at 75 %")
            #expect(EG13ModMarkGeometry.cycles == 1.5)
            for n in [0.0, 0.5, 1.0] {
                let hit = EG13ModMarkGeometry.hitRect(d: d, capRadius: capRadius, normalized: n)
                #expect(hit.width >= 14 && hit.height >= 10)
                let c = EG13ModMarkGeometry.centre(capRadius: capRadius, normalized: n)
                #expect(abs(hit.midX - (d / 2 + c.x)) < 0.0001 && abs(hit.midY - (d / 2 + c.y)) < 0.0001)
                let stroke = EG13ModMarkGeometry.strokeWidth(capRadius: capRadius)
                let corner = hypot(abs(c.x) + mark.width / 2 + stroke / 2, abs(c.y) + mark.height / 2 + stroke / 2)
                #expect(corner <= capRadius, "\(size) mark stays on the cap")
            }
        }
    }

    @Test("the wave sits in the bottom third while the pointer is up, the top third while it is down")
    func markPlacement() {
        #expect(EG13ModMarkGeometry.pointerIsUp(0.5) && EG13ModMarkGeometry.pointerIsUp(0.2))
        #expect(!EG13ModMarkGeometry.pointerIsUp(0.0) && !EG13ModMarkGeometry.pointerIsUp(1.0))
        #expect(!EG13ModMarkGeometry.pointerIsUp(0.1) && !EG13ModMarkGeometry.pointerIsUp(0.9))
        let r: CGFloat = 20
        #expect(EG13ModMarkGeometry.centre(capRadius: r, normalized: 0.5).y > 0, "bottom")
        #expect(EG13ModMarkGeometry.centre(capRadius: r, normalized: 1.0).y < 0, "top")
    }

    @Test("the wave never intersects the pointer")
    func waveNeverIntersectsPointer() {
        let style = EG13KnobStyle.default
        for size in [EG13KnobSize.large, .medium, .small] {
            let d = EG13Knob.diameter(size)
            let capRadius = EG13ModMarkGeometry.capRadius(d: d, style: style, size: size)
            let mark = EG13ModMarkGeometry.size(capRadius: capRadius)
            let stroke = EG13ModMarkGeometry.strokeWidth(capRadius: capRadius)
            let half = max(2, d * 0.07) / 2  // the pointer's half width
            for step in 0...100 {
                let n = Double(step) / 100
                let c = EG13ModMarkGeometry.centre(capRadius: capRadius, normalized: n)
                let box = CGRect(x: c.x - mark.width / 2 - stroke / 2, y: c.y - mark.height / 2 - stroke / 2,
                                 width: mark.width + stroke, height: mark.height + stroke)
                // The pointer: a segment from 0.08 to 0.92 cap radii along its angle (y down), as drawn.
                let angle = EG13Sweep.rotation(n).radians
                let dx = CGFloat(sin(angle)), dy = CGFloat(-cos(angle))
                var overlaps = false
                for i in 0...200 {
                    let rho = capRadius * (0.08 + 0.84 * CGFloat(i) / 200)
                    let p = CGPoint(x: dx * rho, y: dy * rho)
                    if box.insetBy(dx: -half, dy: -half).contains(p) { overlaps = true }
                }
                #expect(!overlaps, "\(size) at \(n): the wave's box clears the pointer")
            }
        }
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
        #expect(trace.frameCount == 30)
        #expect(trace.frame(output: 0, at: 0) == EG13ModTraceFrame(min: 40, max: 41), "the oldest")
        #expect(trace.latest(output: 0) == EG13ModTraceFrame(min: 69, max: 70))
        #expect(trace.frame(output: 0, at: 30) == nil && trace.frame(output: 9, at: 0) == nil)
    }
}

extension EG13Synth {
    /// A tick that refreshes the modulation fields: one more tick first when the next would not.
    func tickModulation() {
        if !nextTickRefreshesModulation { tick() }
        tick()
    }
}
