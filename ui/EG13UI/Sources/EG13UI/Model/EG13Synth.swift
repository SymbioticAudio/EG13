import Observation

/// The editor's model of one plugin instance: every host parameter, the part shown,
/// meters, and the . Reads the plugin at display rate.
@MainActor
@Observable
public final class EG13Synth {
    public enum Kind: Int32, Sendable { case instrument = 0, effect = 1 }

    public let kind: Kind
    public private(set) var params: [EG13Param] = []
    public private(set) var meters = [EG13MeterBallistics(), EG13MeterBallistics()]
    /// The part the window shows (A/B tabs). Editor state only, not saved.
    public var selectedPart = 0
    public var keyboardVisible = false
    /// Patch name; presets arrive in M7.
    public let patchName = "Init"

    @ObservationIgnored private unowned let core: any EG13Core
    @ObservationIgnored private var byID: [UInt32: EG13Param] = [:]
    @ObservationIgnored private var engineValues: [Double?] = [nil, nil]

    public init(core: any EG13Core, kind: Kind) {
        self.core = core
        self.kind = kind
        let frame = core.displayFrame()
        var built: [EG13Param] = []
        for index in 0..<core.paramCount() {
            guard let descriptor = core.paramDescriptor(at: index) else { continue }
            let value = index < frame.values.count ? frame.values[index] : descriptor.defaultValue
            built.append(EG13Param(core: core, index: index, descriptor: descriptor, value: value))
        }
        params = built
        byID = Dictionary(uniqueKeysWithValues: built.map { ($0.id, $0) })
        engineValues = (0..<EG13ParamID.parts).map { engineSelector(part: $0)?.value }
    }

    // MARK: Lookup

    public func param(_ id: UInt32) -> EG13Param? { byID[id] }
    public func partParam(_ part: Int, _ p: EG13PartParam) -> EG13Param? { byID[EG13ParamID.part(part, p)] }
    public func masterParam(_ p: EG13MasterParam) -> EG13Param? { byID[EG13ParamID.master(p)] }

    /// The Source engine selector, if the plugin has one (M2 and later).
    public func engineSelector(part: Int) -> EG13Param? { byID[EG13ParamID.source(part, 0)] }

    /// P1–P8 of a part's Source that the selected engine uses.
    public func sourceParams(part: Int) -> [EG13Param] {
        let used = core.engineParamCount(slot: EG13ParamID.sourceKind, engine: sourceEngine(part: part))
        return (1...8).filter { $0 <= used }.compactMap { byID[EG13ParamID.source(part, $0)] }
    }

    public var sourceEngineNames: [String] {
        let slot = EG13ParamID.sourceKind
        return (0..<core.engineCount(slot: slot)).map { core.engineName(slot: slot, engine: $0) }
    }

    public func sourceEngineName(part: Int) -> String {
        let names = sourceEngineNames
        let selected = sourceEngine(part: part)
        return selected < names.count ? names[selected] : ""
    }

    /// The selected Source engine's index (0 when the plugin has no selector).
    public func sourceEngine(part: Int) -> Int {
        let count = core.engineCount(slot: EG13ParamID.sourceKind)
        let selected = core.currentEngine(slot: EG13ParamID.sourceKind, part: part)
        return selected >= 0 && selected < count ? selected : 0
    }

    /// How a Source knob stands out. The C API has no stable engine id yet, so every knob is
    /// plain until the emphasis metadata is rekeyed (Tasks 7-9 rewrite the cards).
    public func emphasis(of param: EG13Param, part: Int) -> EG13ParamEmphasis { .plain }

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

    /// Reads values and meters from the plugin. elapsed: seconds since the previous call.
    public func refresh(elapsed: Double) {
        let frame = core.displayFrame()
        for (index, value) in frame.values.enumerated() where index < params.count {
            params[index].apply(hostValue: value)
        }
        let engines = (0..<EG13ParamID.parts).map { engineSelector(part: $0)?.value }
        if engines != engineValues {
            engineValues = engines
            reloadDescriptors()
        }
        for part in 0..<min(meters.count, frame.partPeaks.count) {
            let peaks = frame.partPeaks[part]
            meters[part].update(peak: peaks.max() ?? 0, elapsed: elapsed)
        }
    }

    /// Re-reads names, ranges and hidden flags (the engine changed).
    public func reloadDescriptors() {
        for param in params {
            if let descriptor = core.paramDescriptor(at: param.index) { param.reload(descriptor: descriptor) }
        }
    }

    public func endAllGestures() {
        for param in params where param.isEditing { param.endEdit() }
    }

    /// The editor is closing: no gesture may stay open.
    public func shutdown() { endAllGestures() }

    /// Asks the host for an editor window of zoom times the base size.
    public func requestZoom(_ zoom: Double) {
        core.requestResize(width: UInt32((EG13Layout.baseSize.width * zoom).rounded()),
                           height: UInt32((EG13Layout.baseSize.height * zoom).rounded()))
    }
}
