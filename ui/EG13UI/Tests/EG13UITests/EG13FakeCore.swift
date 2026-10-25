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

    func index(of id: UInt32) -> Int { descriptors.firstIndex { $0.id == id }! }

    func paramCount() -> Int { descriptors.count }
    func paramDescriptor(at index: Int) -> EG13ParamDescriptor? { index < descriptors.count ? descriptors[index] : nil }
    func paramName(at index: Int) -> String { names[index] }
    func paramText(at index: Int, value: Double) -> String {
        descriptors[index].isStepped ? "\(Int(value))" : "\(Int((value * 100).rounded()))%"
    }
    func engineCount(slot: UInt32) -> Int { slot == EG13ParamID.sourceKind ? engines.count : 0 }
    func engineParamCount(slot: UInt32, engine: Int) -> Int { paramCounts[engine] }
    func currentEngine(slot: UInt32, part: Int) -> Int { slot == EG13ParamID.sourceKind ? currentSource[part] : 0 }
    func engineName(slot: UInt32, engine: Int) -> String { engines[engine] }
    func beginEdit(_ id: UInt32) { calls.append(.begin(id)) }
    func edit(_ id: UInt32, value: Double) {
        calls.append(.edit(id, value))
        values[index(of: id)] = value
    }
    func endEdit(_ id: UInt32) { calls.append(.end(id)) }
    /// Peaks are read-and-reset, like the real snapshot.
    func displayFrame() -> EG13DisplayFrame {
        let frame = EG13DisplayFrame(values: values, partPeaks: peaks, activeVoices: voices,
                                     engineGeneration: generation)
        peaks = [[0, 0], [0, 0]]
        return frame
    }
    func requestResize(width: UInt32, height: UInt32) { calls.append(.resize(width, height)) }
}
