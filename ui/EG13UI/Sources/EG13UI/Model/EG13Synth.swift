import EG13CBridge
import Observation
import QuartzCore

/// The editor's model of one plugin instance: every host parameter, the part shown, meters
/// and the engine slots. The single source of truth the views read, over the core API.
///
/// `tick()` runs at display rate (30 Hz, driven by a Timer the editor owns). It copies the
/// snapshot into the model and never calls begin, edit or end: only the user's gestures
/// do. While a gesture is open on a parameter, `value(_:)` returns the user's last edit for
/// it, not the snapshot's; the override ends with the gesture, whichever way it ends.
@MainActor
@Observable
public final class EG13Synth {
    public enum Kind: Int32, Sendable { case instrument = 0, effect = 1 }

    public static let slotKindCount = 8
    /// Ticks a one-shot edit is held while the snapshot has not caught up (~0.5 s at 30 Hz).
    public static let pendingTicks = 15
    private static let pendingEpsilon = 1e-6
    private static let placeholder = EG13ParamDescriptor(id: .max, isStepped: false, minValue: 0, maxValue: 1,
                                                         defaultValue: 0)

    public let kind: Kind
    public private(set) var params: [EG13Param] = []
    /// Meter levels after ballistics, linear: [part][left, right]. Instant attack, 300 ms decay.
    public private(set) var meters: [[Float]] = [[0, 0], [0, 0]]
    /// Peak holds, linear: [part][left, right]. Held 1.5 s, then they fall.
    public private(set) var meterHolds: [[Float]] = [[0, 0], [0, 0]]
    public private(set) var activeVoices = [0, 0]
    /// The part the window shows (A/B tabs). Editor state only, not saved.
    public var selectedPart = 0
    /// Layout variant B: the Mods page is shown instead of the slot cards (VOICE / MODS switch).
    public var showsMods = false
    /// Patch name; presets arrive in M7.
    public let patchName = "Init"

    private var values: [Double] = []
    private var descriptors: [EG13ParamDescriptor] = []
    private var names: [String] = []
    /// The user's value for each parameter being dragged.
    private var overrides: [UInt32: Double] = [:]
    /// The last value of a gesture that ended, held until the snapshot shows it (the edit
    /// reaches the snapshot only after the host drains the bridge) or pendingTicks pass.
    private var pending: [UInt32: (value: Double, age: Int)] = [:]
    private var slots: [EG13Slot] = []

    @ObservationIgnored private unowned let core: any EG13Core
    @ObservationIgnored private var indexByID: [UInt32: Int] = [:]
    @ObservationIgnored private var paramByID: [UInt32: EG13Param] = [:]
    @ObservationIgnored private var openGestures: Set<UInt32> = []
    @ObservationIgnored private var engineGeneration: UInt32 = 0
    @ObservationIgnored private var ballistics = [[EG13MeterBallistics]](
        repeating: [EG13MeterBallistics(), EG13MeterBallistics()], count: EG13ParamID.parts)
    @ObservationIgnored private let clock: () -> Double
    @ObservationIgnored private var lastTick: Double

    /// clock: seconds, monotonic. Tests pass their own so meter timing needs no sleeps.
    public init(core: any EG13Core, kind: Kind, clock: @escaping () -> Double = { CACurrentMediaTime() }) {
        self.core = core
        self.kind = kind
        self.clock = clock
        lastTick = clock()
        let frame = core.displayFrame()
        engineGeneration = frame.engineGeneration
        for index in 0..<core.paramCount() {
            // Arrays stay aligned with the table: a missing descriptor leaves an unindexed placeholder.
            let descriptor = core.paramDescriptor(at: index) ?? Self.placeholder
            descriptors.append(descriptor)
            if descriptor.id != Self.placeholder.id { indexByID[descriptor.id] = index }
            values.append(index < frame.values.count ? frame.values[index] : descriptor.defaultValue)
        }
        names = (0..<descriptors.count).map { core.paramName(at: $0) }
        params = descriptors.indices.filter { descriptors[$0].id != Self.placeholder.id }
            .map { EG13Param(synth: self, index: $0) }
        paramByID = Dictionary(params.map { ($0.id, $0) }, uniquingKeysWith: { first, _ in first })
        rebuildSlots()
        activeVoices = frame.activeVoices
    }

    // MARK: Values

    /// The value a control shows: the user's while its gesture is open, else the snapshot's.
    public func value(_ id: UInt32) -> Double {
        if let user = overrides[id] { return user }
        if let held = pending[id] { return held.value }
        guard let index = indexByID[id], index < values.count else { return 0 }
        return values[index]
    }

    public func text(_ id: UInt32) -> String {
        guard let index = indexByID[id] else { return "" }
        return core.paramText(at: index, value: value(id))
    }

    /// The parameter's current name (engine-aware: "Density" for Granular's P5).
    public func name(_ id: UInt32) -> String {
        guard let index = indexByID[id], index < names.count else { return "" }
        return names[index]
    }

    func descriptorAt(_ index: Int) -> EG13ParamDescriptor { descriptors[index] }

    public func descriptor(_ id: UInt32) -> EG13ParamDescriptor? {
        guard let index = indexByID[id], index < descriptors.count else { return nil }
        return descriptors[index]
    }

    func text(at index: Int, value: Double) -> String { core.paramText(at: index, value: value) }

    // MARK: Gestures (host: begin, any number of edits, end)

    public func isEditing(_ id: UInt32) -> Bool { openGestures.contains(id) }

    public func begin(_ id: UInt32) {
        guard openGestures.insert(id).inserted else { return }
        pending[id] = nil
        core.beginEdit(id)
    }

    /// Inside a gesture: sends the edit. Outside one: wraps it in its own.
    public func edit(_ id: UInt32, _ newValue: Double) {
        let opened = !openGestures.contains(id)
        if opened { begin(id) }
        overrides[id] = newValue
        core.edit(id, value: newValue)
        if opened { end(id) }
    }

    /// Ends the gesture. The knob keeps the user's last value until the snapshot catches up
    /// (see `pending`), then follows the snapshot. A no-op when none is open.
    public func end(_ id: UInt32) {
        guard openGestures.remove(id) != nil else { return }
        if let last = overrides[id] { pending[id] = (last, 0) }
        overrides[id] = nil
        core.endEdit(id)
    }

    public func endAllGestures() {
        for id in openGestures.sorted() { end(id) }
    }

    /// The editor is closing: no gesture may stay open.
    public func shutdown() { endAllGestures() }

    // MARK: Lookup

    public func param(_ id: UInt32) -> EG13Param? { paramByID[id] }
    public func partParam(_ part: Int, _ p: EG13PartParam) -> EG13Param? { paramByID[EG13ParamID.part(part, p)] }
    public func masterParam(_ p: EG13MasterParam) -> EG13Param? { paramByID[EG13ParamID.master(p)] }

    /// The Source engine selector, if the plugin has one (M2 and later).
    public func engineSelector(part: Int) -> EG13Param? { paramByID[EG13ParamID.source(part, 0)] }

    /// The engine slot of a part: selected engine, engine names and the P's it uses.
    public func slot(_ kind: EG13SlotKind, part: Int) -> EG13Slot {
        let index = part * Self.slotKindCount + Int(kind.rawValue)
        guard part >= 0, index >= 0, index < slots.count else { return .empty(kind, part: part) }
        return slots[index]
    }

    /// The part's engine-slot P's the selected engine uses, as handles.
    public func slotParams(_ kind: EG13SlotKind, part: Int) -> [EG13Param] {
        slot(kind, part: part).descriptors.compactMap { paramByID[$0.id] }
    }

    public var mode: EG13Mode { EG13Mode(rawValue: Int(masterParam(.mode)?.value ?? 0)) ?? .single }

    public var keyMap: EG13KeyMap {
        let ranges = (0..<EG13ParamID.parts).map { part -> ClosedRange<Int> in
            let low = Int(partParam(part, .keyLow)?.value ?? 0)
            let high = Int(partParam(part, .keyHigh)?.value ?? 127)
            return min(low, high)...max(low, high)
        }
        return EG13KeyMap(mode: mode, splitNote: Int(masterParam(.splitNote)?.value ?? 60),
                          keyRanges: ranges, selectedPart: selectedPart)
    }

    // MARK: Display rate

    /// One display frame: reads the snapshot, rebuilds descriptors only if the engine
    /// generation changed, advances the meters by the time since the last tick.
    public func tick() {
        let now = clock()
        let dt = max(0, now - lastTick)
        lastTick = now
        let frame = core.displayFrame()
        if frame.engineGeneration != engineGeneration {
            engineGeneration = frame.engineGeneration
            reloadDescriptors()
        }
        var updated = values
        for index in 0..<min(updated.count, frame.values.count) { updated[index] = frame.values[index] }
        if updated != values { values = updated }  // an unchanged array must not invalidate views
        expirePending()
        if frame.activeVoices != activeVoices { activeVoices = frame.activeVoices }
        updateMeters(frame.partPeaks, dt: dt)
    }

    /// Drops held values the snapshot now shows, or that waited long enough.
    private func expirePending() {
        guard !pending.isEmpty else { return }
        var next = pending
        for (id, held) in pending {
            let seen = indexByID[id].map { $0 < values.count && abs(values[$0] - held.value) <= Self.pendingEpsilon } ?? true
            if seen || held.age + 1 >= Self.pendingTicks { next[id] = nil } else { next[id] = (held.value, held.age + 1) }
        }
        pending = next
    }

    private func updateMeters(_ peaks: [[Float]], dt: Double) {
        var levels = meters, holds = meterHolds
        for part in 0..<min(ballistics.count, peaks.count) {
            for channel in 0..<2 {
                let peak = channel < peaks[part].count ? peaks[part][channel] : 0
                ballistics[part][channel].update(peak: peak, dt: dt)
                levels[part][channel] = ballistics[part][channel].level
                holds[part][channel] = ballistics[part][channel].hold
            }
        }
        if levels != meters { meters = levels }
        if holds != meterHolds { meterHolds = holds }
    }

    /// Re-reads names, ranges and slots (the engine generation changed).
    private func reloadDescriptors() {
        for index in descriptors.indices {
            if let descriptor = core.paramDescriptor(at: index) { descriptors[index] = descriptor }
        }
        names = descriptors.indices.map { core.paramName(at: $0) }
        rebuildSlots()
    }

    private func rebuildSlots() {
        var built: [EG13Slot] = []
        for part in 0..<EG13ParamID.parts {
            for raw in 0..<Self.slotKindCount {
                let slotKind = EG13SlotKind(rawValue: UInt32(raw))
                let slotID = UInt32(raw)
                let count = core.engineCount(slot: slotID)
                let selected = core.currentEngine(slot: slotID, part: part)
                let engine = selected >= 0 && selected < count ? selected : 0
                let used = count > 0 ? core.engineParamCount(slot: slotID, engine: engine) : 0
                let visible = used >= 1 ? (1...used).compactMap { p in
                    descriptor(EG13ParamID.slot(part, slotKind, p))
                } : []
                built.append(EG13Slot(
                    kind: slotKind, part: part, engine: engine,
                    engineNames: (0..<count).map { core.engineName(slot: slotID, engine: $0) },
                    descriptors: visible))
            }
        }
        slots = built
    }

    /// Asks the host for an editor window of zoom times the base size.
    public func requestZoom(_ zoom: Double) {
        core.requestResize(width: UInt32((EG13Layout.baseSize.width * zoom).rounded()),
                           height: UInt32((EG13Layout.baseSize.height * zoom).rounded()))
    }
}
