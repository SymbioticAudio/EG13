import EG13CBridge
import Observation
import Testing
@testable import EG13UI

/// The matrix and modulation model: routes, marks and Motion rows, the hidden-field rule, the mod trace.
@MainActor
struct EG13ModRouteModelTests {
    private final class Flag: @unchecked Sendable { var fired = false }

    // Source codes the fake names (core/mod/mod_routes.h).
    static let velocity: UInt16 = 1
    static let modWheel: UInt16 = 9
    static let mod1Out1: UInt16 = 32
    static let filterP1: UInt16 = 32
    static let filterP4: UInt16 = 35

    let core: EG13FakeCore
    let synth: EG13Synth

    /// The full fake with the Analog Filter (3 P's) and a mod slot 1 Envelope in both parts.
    init() {
        core = EG13FakeCore.fullCatalog()
        for part in 0..<2 {
            core.select(EG13_SLOT_KIND_FILTER, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD1, engine: 1, part: part)
        }
        core.calls = []
        synth = EG13Synth(core: core, kind: .instrument)
    }

    /// A host state load: the engine generation changes, and the next tick reloads.
    private func reload() {
        core.generation += 1
        synth.tick()
    }

    private func filterP(_ n: Int, part: Int = 0) -> UInt32 { EG13ParamID.slot(part, EG13_SLOT_KIND_FILTER, n) }

    @Test("modulate fills the first free route at +25 %")
    func modulateFillsFirstFreeRoute() {
        core.routes[0][0] = .init(source: Self.velocity, destination: 2, via: 0)  // occupied
        reload()
        #expect(synth.firstFreeRoute(part: 0) == 1)
        #expect(synth.modulate(id: filterP(1), from: Self.modWheel))
        #expect(core.routes[0][1] == .init(source: Self.modWheel, destination: Self.filterP1, via: 0))
        #expect(core.routes[0][0].source == Self.velocity, "the occupied route is untouched")
        let amountID = EG13ParamID.routeAmount(0, 1)
        #expect(core.calls == [.begin(amountID), .edit(amountID, 0.25), .end(amountID)])
        let route = synth.routes(part: 0)[1]
        #expect(route.source == Self.modWheel && route.destination == Self.filterP1)
        #expect(route.amount == 0.25)
        #expect(route.sourceName == "Mod Wheel" && route.destinationName == "Filter: Frequency")
        #expect(synth.routes(into: filterP(1), part: 0).map(\.source) == [Self.modWheel])
        #expect(synth.firstFreeRoute(part: 0) == 2)
    }

    @Test("A parameter with no destination cannot be modulated")
    func noDestination() {
        #expect(!synth.modulate(id: EG13ParamID.part(0, .pan), from: Self.modWheel))
        #expect(!synth.modulate(id: EG13ParamID.master(.level), from: Self.modWheel))
        #expect(core.setRouteCalls.isEmpty && core.calls.isEmpty)
    }

    @Test("Part Level maps to destination 4 and can be modulated")
    func partLevelIsModulatable() {
        #expect(core.destination(forParam: EG13ParamID.part(0, .level)) == 4)
        #expect(synth.modulate(id: EG13ParamID.part(1, .level), from: Self.modWheel))
        #expect(synth.routes(part: 1).first { $0.isActive }?.destination == 4)
    }

    @Test("Matrix full disables modulate")
    func matrixFull() {
        for r in 0..<12 { core.routes[0][r] = .init(source: Self.velocity, destination: 2, via: 0) }
        reload()
        #expect(synth.firstFreeRoute(part: 0) == nil)
        #expect(synth.firstFreeRoute(part: 1) == 0, "the other part has its own matrix")
        core.setRouteCalls = []
        core.calls = []
        #expect(!synth.modulate(id: filterP(1), from: Self.modWheel))
        #expect(core.setRouteCalls.isEmpty && core.calls.isEmpty, "nothing is sent")
    }

    @Test("Routes into hidden P's show no mark")
    func hiddenParams() {
        // The Analog Filter uses P1-P3; P4 is hidden. A route into each.
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        core.routes[0][1] = .init(source: Self.modWheel, destination: Self.filterP4, via: 0)
        reload()
        #expect(synth.routes(into: filterP(1), part: 0).count == 1)
        #expect(synth.routes(into: filterP(4), part: 0).isEmpty)
        #expect(synth.routeTable[0][1].destinationHidden, "the route keeps its code and is flagged hidden")
        synth.tickModulation()
        #expect(synth.param(filterP(4))?.isRouted == false, "P4 shows no mark")
        #expect(synth.param(filterP(1))?.isRouted == true)
    }

    @Test("A hidden field is sent as 0 when another field of the route changes")
    func hiddenFieldsAreCleared() {
        // Via is Mod 3's output (Mod 3 is Off, so the part hides it); the destination is visible.
        core.routes[0][2] = .init(source: Self.modWheel, destination: Self.filterP1, via: 48)
        reload()
        #expect(synth.routeTable[0][2].viaHidden && !synth.routeTable[0][2].destinationHidden)
        core.setRouteCalls = []
        #expect(synth.setRoute(part: 0, index: 2, source: Self.velocity))
        let call = core.setRouteCalls[0]
        #expect(call.source == Self.velocity && call.destination == Self.filterP1)
        #expect(call.via == 0, "the hidden via is sent as 0")
        #expect(core.routes[0][2].via == 0)
        // The visible fields are kept as they were.
        #expect(synth.setRoute(part: 0, index: 2, destination: 33))
        #expect(core.setRouteCalls.last?.source == Self.velocity && core.setRouteCalls.last?.destination == 33)
        // A code the core rejects leaves the route and returns false.
        #expect(!synth.setRoute(part: 0, index: 2, source: 63))
        #expect(core.routes[0][2].source == Self.velocity)
    }

    @Test("A hidden destination is also sent as 0 when the source changes")
    func hiddenDestinationCleared() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP4, via: 0)
        reload()
        core.setRouteCalls = []
        #expect(synth.setRoute(part: 0, index: 0, source: Self.velocity))
        #expect(core.setRouteCalls[0].destination == 0)
    }

    @Test("Generation change reloads routes; a plain tick does not")
    func generationReloads() {
        synth.tick()
        core.routes[1][4] = .init(source: Self.velocity, destination: 2, via: 0)  // a host state load
        synth.tick()
        #expect(synth.routes(part: 1)[4].isFree, "no generation change: no reload")
        core.generation += 1
        synth.tick()
        #expect(synth.routes(part: 1)[4].source == Self.velocity)
        #expect(synth.routes(part: 1)[4].destinationName == "Amp")
        #expect(synth.firstFreeRoute(part: 1) == 0)
    }

    @Test("Own setRoute and removeRoute reload at once")
    func ownChangesReload() {
        #expect(synth.setRoute(part: 0, index: 5, source: Self.velocity, destination: 2))
        #expect(synth.routes(part: 0)[5].source == Self.velocity, "no tick needed")
        synth.edit(EG13ParamID.routeAmount(0, 5), 0.6)
        synth.removeRoute(part: 0, index: 5)
        #expect(synth.routes(part: 0)[5].isFree)
        #expect(core.routes[0][5] == .init())
        #expect(synth.value(EG13ParamID.routeAmount(0, 5)) == 0)
    }

    @Test("Pickers list only the codes the engines name")
    func pickers() {
        let sources = synth.sources(part: 0)
        #expect(sources.contains { $0.code == 1 && $0.name == "Velocity" })
        #expect(sources.contains { $0.code == 32 && $0.name == "Mod 1 Envelope" })
        #expect(!sources.contains { $0.code == 40 }, "Mod 2 is Off")
        let destinations = synth.destinations(part: 0)
        #expect(destinations.contains { $0.code == Self.filterP1 && $0.name == "Filter: Frequency" })
        #expect(!destinations.contains { $0.code == Self.filterP4 })
    }

    // MARK: Mark

    @Test("A modulation value change does not invalidate any parameter; a route change invalidates its target only")
    func modulationIsolation() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: 32, via: 0)
        core.routes[0][1] = .init(source: Self.modWheel, destination: 33, via: 0)
        reload()
        core.modValues[0][32] = 0.2
        core.modValues[0][33] = 0.2
        synth.tickModulation()
        let x = synth.param(filterP(1))!, y = synth.param(filterP(2))!
        let touchedY = Flag(), touchedX = Flag()
        func track() {
            touchedY.fired = false
            touchedX.fired = false
            withObservationTracking {
                _ = (y.isRouted, y.value, y.text)
            } onChange: { touchedY.fired = true }
            withObservationTracking {
                _ = x.isRouted
            } onChange: { touchedX.fired = true }
        }
        track()
        core.modValues[0][32] = 0.4
        synth.tickModulation()
        #expect(!touchedX.fired && !touchedY.fired, "values never invalidate")
        core.routes[0][0] = .init()
        reload()
        synth.tickModulation()
        #expect(touchedX.fired, "the tracking does fire for X's own route change")
        #expect(!touchedY.fired, "Y was not invalidated")
        #expect(EG13Motion.amountText(0) == "0 %")
        #expect(EG13Motion.amountText(1) == "+100 %")
    }

    // MARK: Mod trace

    @Test("mod trace keeps 30 frames")
    func traceKeeps30() {
        for i in 0..<100 {
            core.modOutMin[0][1][0] = Float(i)
            core.modOutMax[0][1][0] = Float(i) + 0.5
            synth.tick()
        }
        let frames = synth.modTrace(part: 0, slot: 1).frames(output: 0)
        // 15 Hz: one frame per two ticks, each the range over both (the core resets its range on every read).
        #expect(frames.count == 30)
        #expect(frames.first == EG13ModTraceFrame(min: 39, max: 40.5))
        #expect(frames.last == EG13ModTraceFrame(min: 97, max: 98.5))
        #expect(synth.modTrace(part: 0, slot: 0).frames(output: 0).allSatisfy { $0 == nil }, "other slots hold gaps")
        #expect(synth.modTrace(part: 1, slot: 1).frames(output: 0).count == 30)
    }

    @Test("A frame with no data records a gap, not a value")
    func traceGaps() {
        core.modOutMin[0][0][0] = 0.1
        core.modOutMax[0][0][0] = 0.3
        synth.tickModulation()
        core.modOutMin[0][0][0] = .infinity  // the output produced nothing
        core.modOutMax[0][0][0] = -.infinity
        synth.tickModulation()
        core.modOutMin[0][0][0] = 0.2
        core.modOutMax[0][0][0] = 0.2
        synth.tickModulation()
        let frames = synth.modTrace(part: 0, slot: 0).frames(output: 0)
        #expect(Array(frames.suffix(3)) == [EG13ModTraceFrame(min: 0.1, max: 0.3), nil, EG13ModTraceFrame(min: 0.2, max: 0.2)])
    }

    @Test("a static patch's traces stop invalidating")
    func staticTraceIsQuiet() {
        // A parked per-part output (a gate-off envelope, a stopped LFO) reports the same value
        // every frame: once the whole window holds it, a push changes nothing a card can show.
        core.modOutMin[0][0][0] = 0.4
        core.modOutMax[0][0][0] = 0.4
        for _ in 0..<(2 * EG13ModTrace.capacity + 4) { synth.tickModulation() }
        let trace = synth.modTrace(part: 0, slot: 0)
        let touched = Flag()
        func track() {
            touched.fired = false
            withObservationTracking { _ = trace.frames } onChange: { touched.fired = true }
        }
        track()
        synth.tickModulation()
        synth.tickModulation()
        #expect(!touched.fired, "a uniform window of the same value does not invalidate")
        core.modOutMin[0][0][0] = 0.5  // positive control: a new value does
        core.modOutMax[0][0][0] = 0.5
        track()
        synth.tickModulation()
        #expect(touched.fired, "a changed value invalidates")
        track()
        synth.tickModulation()
        #expect(touched.fired, "the same value again still scrolls while the window holds older frames")
        #expect(trace.latest(output: 0) == EG13ModTraceFrame(min: 0.5, max: 0.5))
    }

    @Test("A full trace of gaps does not invalidate its card")
    func idleTraceIsQuiet() {
        for _ in 0..<70 { synth.tick() }
        let trace = synth.modTrace(part: 0, slot: 0)
        let touched = Flag()
        withObservationTracking { _ = trace.frames } onChange: { touched.fired = true }
        synth.tick()
        #expect(!touched.fired)
    }
}
