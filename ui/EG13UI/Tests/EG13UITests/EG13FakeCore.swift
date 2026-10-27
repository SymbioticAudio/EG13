import Foundation
import EG13CBridge
import EG13UIFixtures
@testable import EG13UI

/// A scripted plugin for view-model tests: a few parameters, recorded calls.
@MainActor
final class EG13FakeCore: EG13Core {
    enum Call: Equatable {
        case begin(UInt32)
        case edit(UInt32, Double)
        case end(UInt32)
        case resize(UInt32, UInt32)
    }

    var descriptors: [EG13ParamDescriptor]
    var names: [String]
    var values: [Double]
    var peaks: [[Float]] = [[0, 0], [0, 0]]
    /// Source engines, how many P's each uses, and the engine each part has selected.
    var engines = ["Macro Oscillator", "Waveshaper", "Chord"]
    var paramCounts = [8, 5, 3]
    var currentSource = [0, 0]
    var generation: UInt32 = 0
    var voices = [0, 0]
    var calls: [Call] = []
    /// How many times the editor asked for value text (it should ask once per change).
    var textCalls = 0
    /// The full table (`full()`): each engine slot's engines, by slot kind, and the engine each
    /// part has selected. Names of P's follow the selected engine, like the real core.
    struct FakeEngine { var name: String; var params: [String] }
    var slotEngines: [Int: [FakeEngine]] = [:]
    var currentBySlot: [Int: [Int]] = [:]

    init(descriptors: [EG13ParamDescriptor], names: [String]) {
        self.descriptors = descriptors
        self.names = names
        values = descriptors.map(\.defaultValue)
    }

    /// Master level and mode, Part A's level, voice count, MIDI channel and key range,
    /// Part B's MIDI channel, and Part A Source P1–P3.
    static func standard() -> EG13FakeCore {
        let d: [(EG13ParamDescriptor, String)] = [
            (.init(id: EG13ParamID.master(.level), isStepped: false, minValue: 0, maxValue: 1, defaultValue: 0.8), "Level"),
            (.init(id: EG13ParamID.master(.mode), isStepped: true, minValue: 0, maxValue: 3, defaultValue: 0), "Mode"),
            (.init(id: EG13ParamID.master(.splitNote), isStepped: true, minValue: 0, maxValue: 127, defaultValue: 60), "Split Note"),
            (.init(id: EG13ParamID.part(0, .level), isStepped: false, minValue: 0, maxValue: 1, defaultValue: 1), "Level"),
            (.init(id: EG13ParamID.part(0, .pan), isStepped: false, minValue: -1, maxValue: 1, defaultValue: 0), "Pan"),
            (.init(id: EG13ParamID.part(0, .voiceCount), isStepped: true, minValue: 1, maxValue: 8, defaultValue: 8), "Voices"),
            (.init(id: EG13ParamID.part(0, .midiChannel), isStepped: true, minValue: 0, maxValue: 16, defaultValue: 0), "MIDI Channel"),
            (.init(id: EG13ParamID.part(0, .keyLow), isStepped: true, minValue: 0, maxValue: 127, defaultValue: 0), "Key Low"),
            (.init(id: EG13ParamID.part(0, .keyHigh), isStepped: true, minValue: 0, maxValue: 127, defaultValue: 127), "Key High"),
            (.init(id: EG13ParamID.part(1, .midiChannel), isStepped: true, minValue: 0, maxValue: 16, defaultValue: 2), "MIDI Channel"),
            (.init(id: EG13ParamID.source(0, 0), isStepped: true, minValue: 0, maxValue: 2, defaultValue: 0), "Engine"),
            (.init(id: EG13ParamID.source(0, 1), isStepped: false, minValue: 0, maxValue: 1, defaultValue: 0.25), "Model"),
            (.init(id: EG13ParamID.source(0, 2), isStepped: false, minValue: 0, maxValue: 1, defaultValue: 0.5), "Harmonics"),
            (.init(id: EG13ParamID.source(0, 3), isStepped: false, minValue: 0, maxValue: 1, defaultValue: 0.5), "Timbre"),
        ] + (4...8).map { p in
            (EG13ParamDescriptor(id: EG13ParamID.source(0, p), isStepped: false, minValue: 0, maxValue: 1,
                                 defaultValue: 0.5), "P\(p)")
        }
        return EG13FakeCore(descriptors: d.map(\.0), names: d.map(\.1))
    }

    /// The whole plugin: every one of the 323 parameters (158 per part, 7 master) with the real
    /// ids, and engines in all eight slots. Engines differ in how many P's they use; one uses
    /// fewer P's with a gap, as the LFO does. Any combination can be selected with
    /// `select(_:engine:part:)`.
    static func full() -> EG13FakeCore {
        var d: [(EG13ParamDescriptor, String)] = []
        func add(_ id: UInt32, _ name: String, _ lo: Double = 0, _ hi: Double = 1, _ def: Double = 0,
                 stepped: Bool = false) {
            d.append((EG13ParamDescriptor(id: id, isStepped: stepped, minValue: lo, maxValue: hi, defaultValue: def), name))
        }
        for part in 0..<EG13ParamID.parts {
            for kind in 0..<8 {
                let slot = EG13SlotKind(rawValue: UInt32(kind))
                add(EG13ParamID.slot(part, slot, 0), "Engine", 0, 31, 0, stepped: true)
                for p in 1...EG13ParamID.slotParamCount(slot) { add(EG13ParamID.slot(part, slot, p), "(unused)") }
                if EG13ParamID.isModSlot(slot) { add(EG13ParamID.modScope(part, slot), "Scope", 0, 1, 0, stepped: true) }
            }
            for route in 0..<EG13ParamID.routeCount { add(EG13ParamID.routeAmount(part, route), "Route \(route + 1)", -1, 1) }
            for p in EG13PartParam.allCases {
                let stepped = p != .level && p != .pan && p != .glide
                add(EG13ParamID.part(part, p), "Part \(p)", stepped ? 0 : 0, stepped ? 16 : 1, 0, stepped: stepped)
            }
        }
        for m in EG13MasterParam.allCases { add(EG13ParamID.master(m), "Master \(m)", 0, 3, 0, stepped: m != .level && m != .dry) }
        let core = EG13FakeCore(descriptors: d.map(\.0), names: d.map(\.1))
        func engine(_ name: String, _ count: Int) -> FakeEngine { FakeEngine(name: name, params: (0..<count).map { "\(name) P\($0 + 1)" }) }
        let gap = FakeEngine(name: "Gapped", params: ["A", "B", "C", EG13Slot.unusedName, EG13Slot.unusedName, "F"])
        core.slotEngines = [
            0: [engine("Macro", 8), engine("Digital", 7), engine("Input", 3), engine("Modal", 16)],
            1: [engine("Off", 0), engine("Analog", 3), engine("Classic", 4)],
            2: [engine("Off", 0), engine("RingMod", 9), engine("Eq", 12)],
            3: [engine("Off", 0), engine("Env", 13), gap, engine("Random", 16)],
            4: [engine("Off", 0), engine("Env", 13), gap, engine("Random", 16)],
            5: [engine("Off", 0), engine("Env", 13), gap, engine("Random", 16)],
            6: [engine("Off", 0), engine("Env", 13), gap, engine("Random", 16)],
            7: [engine("Off", 0), engine("Granular", 13), engine("Reverb", 9), engine("Delay", 16)],
        ]
        for kind in 0..<8 { core.currentBySlot[kind] = [0, 0] }
        core.dynamicNames = true
        return core
    }

    var dynamicNames = false

    // MARK: Matrix

    struct FakeRoute: Equatable { var source: UInt16 = 0, destination: UInt16 = 0, via: UInt16 = 0 }
    /// The patch's routes: [part][route]. All free to start with.
    var routes = [[FakeRoute]](repeating: [FakeRoute](repeating: FakeRoute(), count: 12), count: 2)
    /// Codes the part's engines hide although the engines would name them.
    var hiddenSources: Set<UInt16> = []
    var hiddenDestinations: Set<UInt16> = []
    var setRouteCalls: [(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16)] = []
    /// What the next display frame reports: [part][destination], NaN for none; and the mod outputs'
    /// min and max since the last frame, [part][slot][output] (+inf, -inf for no data).
    var modValues = [[Float]](repeating: [Float](repeating: .nan, count: 144), count: 2)
    var modOutMin = [[[Float]]](repeating: [[Float]](repeating: [Float](repeating: .infinity, count: 8), count: 4), count: 2)
    var modOutMax = [[[Float]]](repeating: [[Float]](repeating: [Float](repeating: -.infinity, count: 8), count: 4), count: 2)

    private func modEngineNames(part: Int) -> [String] {
        (3...6).map { kind in
            guard let engines = slotEngines[kind], let current = currentBySlot[kind] else { return "Off" }
            return engines[current[part]].name
        }
    }

    func route(part: Int, index: Int) -> EG13ModRoute {
        guard routes.indices.contains(part), routes[part].indices.contains(index) else { return EG13ModRoute() }
        let r = routes[part][index]
        let id = EG13ParamID.routeAmount(part, index)
        let amount = descriptors.firstIndex { $0.id == id }.map { values[$0] } ?? 0
        return makeRoute(part: part, source: r.source, destination: r.destination, via: r.via, amount: amount)
    }

    func setRoute(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16) -> Bool {
        setRouteCalls.append((part, index, source, destination, via))
        guard routes.indices.contains(part), routes[part].indices.contains(index),
              source == 0 || sourceName(part: part, code: source) != nil,
              via == 0 || sourceName(part: part, code: via) != nil,
              destination == 0 || destinationName(part: part, code: destination) != nil else { return false }
        routes[part][index] = FakeRoute(source: source, destination: destination, via: via)
        return true
    }

    func sourceName(part: Int, code: UInt16) -> String? {
        guard !hiddenSources.contains(code) else { return nil }
        return EG13MatrixNames.sourceName(code: code, modEngines: modEngineNames(part: part))
    }

    func destinationName(part: Int, code: UInt16) -> String? {
        guard !hiddenDestinations.contains(code), dynamicNames else { return nil }
        return EG13MatrixNames.destinationName(code: code) { kind in
            let engine = self.slotEngines[kind]![self.currentBySlot[kind]![part]]
            return engine.params
        }
    }

    func destination(forParam id: UInt32) -> UInt16 {
        let part = Int(id >> 16), slot = Int((id >> 8) & 0xFF), p = Int(id & 0xFF)
        if slot == 0 && p == 1 && part < 2 { return 4 }  // Part Level
        let bases = [1: 16, 2: 32, 3: 48, 4: 64, 5: 80, 6: 96, 7: 112, 8: 128]
        guard part < 2, let base = bases[slot], p >= 1, p <= 16 else { return 0 }
        if slot == 2 && p > 4 || slot == 3 && p > 12 { return 0 }
        return UInt16(base + p - 1)
    }

    func version() -> String { "test" }

    /// The full table with the plugin's real engines and P names (EG13EngineCatalog), for the
    /// tests that check names, labels and panel data against every engine.
    static func fullCatalog() -> EG13FakeCore {
        let core = full()
        for kind in 0..<8 {
            core.slotEngines[kind] = EG13EngineCatalog.engines(kind: kind).map { FakeEngine(name: $0.name, params: $0.params) }
        }
        return core
    }

    /// Selects an engine in one slot of one part, as the plugin does (a new engine generation).
    func select(_ kind: EG13SlotKind, engine: Int, part: Int) {
        currentBySlot[Int(kind.rawValue)]![part] = engine
        generation += 1
    }

    func index(of id: UInt32) -> Int { descriptors.firstIndex { $0.id == id }! }

    func paramCount() -> Int { descriptors.count }
    func paramDescriptor(at index: Int) -> EG13ParamDescriptor? { index < descriptors.count ? descriptors[index] : nil }
    func paramName(at index: Int) -> String {
        guard dynamicNames else { return names[index] }
        let id = descriptors[index].id
        let slotID = Int((id >> 8) & 0xFF), p = Int(id & 0xFF), part = Int(id >> 16)
        let kinds = [1: 0, 2: 1, 3: 2, 4: 7, 5: 3, 6: 4, 7: 5, 8: 6]
        guard part < EG13ParamID.parts, let kind = kinds[slotID], p >= 1, p <= EG13ParamID.slotParamCount(EG13SlotKind(rawValue: UInt32(kind))) else {
            return names[index]
        }
        let engine = slotEngines[kind]![currentBySlot[kind]![part]]
        return p <= engine.params.count ? engine.params[p - 1] : EG13Slot.unusedName
    }
    func paramText(at index: Int, value: Double) -> String {
        textCalls += 1
        return descriptors[index].isStepped ? "\(Int(value))" : "\(Int((value * 100).rounded()))%"
    }
    func engineCount(slot: UInt32) -> Int {
        dynamicNames ? slotEngines[Int(slot)]!.count : slot == EG13ParamID.sourceKind ? engines.count : 0
    }
    func engineParamCount(slot: UInt32, engine: Int) -> Int {
        dynamicNames ? slotEngines[Int(slot)]![engine].params.count : paramCounts[engine]
    }
    func currentEngine(slot: UInt32, part: Int) -> Int {
        dynamicNames ? currentBySlot[Int(slot)]![part] : slot == EG13ParamID.sourceKind ? currentSource[part] : 0
    }
    func engineName(slot: UInt32, engine: Int) -> String {
        dynamicNames ? slotEngines[Int(slot)]![engine].name : engines[engine]
    }
    func beginEdit(_ id: UInt32) { calls.append(.begin(id)) }
    func edit(_ id: UInt32, value: Double) {
        calls.append(.edit(id, value))
        values[index(of: id)] = value
    }
    func endEdit(_ id: UInt32) { calls.append(.end(id)) }
    /// Peaks are read-and-reset, like the real snapshot.
    func displayFrame() -> EG13DisplayFrame {
        let frame = EG13DisplayFrame(values: values, partPeaks: peaks, activeVoices: voices,
                                     engineGeneration: generation, modValues: modValues,
                                     modOutMin: modOutMin, modOutMax: modOutMax)
        peaks = [[0, 0], [0, 0]]
        return frame
    }
    func requestResize(width: UInt32, height: UInt32) { calls.append(.resize(width, height)) }

    // MARK: Presets

    /// The factory list the fake reports.
    var factory: [EG13PresetEntry] = []
    /// What `applyPreset` returns, and every call it received.
    var applyResult = EG13PresetApplyResult(status: .ok)
    var applied: [(json: String, target: EG13PresetTarget)] = []
    /// The browser string the project holds, and every string set (in order).
    var browser = ""
    var browserWrites: [String] = []
    var pluginKind = "instrument"
    func factoryPresets() -> [EG13PresetEntry] { factory }
    func applyPreset(json: String, target: EG13PresetTarget) -> EG13PresetApplyResult {
        applied.append((json, target))
        return applyResult
    }
    /// A small valid preset file naming the part and the name.
    func savePreset(part: Int?, name: String) -> String {
        let root: [String: Any] = [
            "eg13Preset": 1, "kind": part == nil ? "performance" : "patch", "plugin": pluginKind,
            "name": name, "bank": "User", "state": ["schemaVersion": 6, "params": [String: Double]()],
        ]
        return String(decoding: (try? JSONSerialization.data(withJSONObject: root)) ?? Data(), as: UTF8.self)
    }
    func browserState() -> String { browser }
    func setBrowserState(_ state: String) {
        browser = state
        browserWrites.append(state)
    }
}
