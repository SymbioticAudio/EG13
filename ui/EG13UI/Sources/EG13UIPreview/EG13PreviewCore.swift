import EG13UI

/// A fake plugin for the preview: parameters live in memory, nothing needs a DAW. The
/// tests have their own recording fake (EG13FakeCore, test target only).
@MainActor
final class EG13PreviewCore: EG13Core {
    static let engineNames = ["Virtual Analog", "Waveshaper", "Two-op FM", "Grain Cloud", "Additive",
                              "Wavetable", "Chord", "Formant"]
    static let voiceModes = ["Poly", "Unison", "Mono", "Legato"]
    static let modes = ["Single", "Layer", "Split", "Multi"]

    private var descriptors: [EG13ParamDescriptor] = []
    private var names: [String] = []
    private var values: [Double] = []
    private var nextDemoID: UInt32 = 0x00F0_0000
    /// Bumped when an engine selector is edited, as the plugin does on an engine switch.
    private var generation: UInt32 = 0
    /// The gallery's parameters each live in a synth of their own (a Param is a handle on one).
    private var demoSynths: [EG13Synth] = []

    /// The real parameter table, so the full editor runs: master, both parts, Source P0-P8.
    static func standard() -> EG13PreviewCore {
        let core = EG13PreviewCore()
        func add(_ id: UInt32, _ name: String, _ min: Double, _ max: Double, _ def: Double, stepped: Bool = false) {
            core.append(.init(id: id, isStepped: stepped, minValue: min, maxValue: max, defaultValue: def), name)
        }
        add(EG13ParamID.master(.level), "Out", 0, 1, 0.8)
        add(EG13ParamID.master(.dry), "Dry", 0, 1, 0)
        add(EG13ParamID.master(.mode), "Mode", 0, 3, 0, stepped: true)
        add(EG13ParamID.master(.splitNote), "Split Note", 0, 127, 60, stepped: true)
        for part in 0..<EG13ParamID.parts {
            add(EG13ParamID.part(part, .level), "Level", 0, 1, 1)
            add(EG13ParamID.part(part, .pan), "Pan", -1, 1, 0)
            add(EG13ParamID.part(part, .glide), "Glide", 0, 1, 0)
            add(EG13ParamID.part(part, .transpose), "Transpose", -24, 24, 0)
            add(EG13ParamID.part(part, .baseNote), "Base Note", 0, 127, 60, stepped: true)
            add(EG13ParamID.part(part, .midiChannel), "MIDI Channel", 0, 16, 0, stepped: true)
            add(EG13ParamID.part(part, .keyLow), "Key Low", 0, 127, 0, stepped: true)
            add(EG13ParamID.part(part, .keyHigh), "Key High", 0, 127, 127, stepped: true)
            add(EG13ParamID.part(part, .voiceMode), "Voice Mode", 0, 3, 0, stepped: true)
            add(EG13ParamID.part(part, .voiceCount), "Voices", 1, 8, 8, stepped: true)
            add(EG13ParamID.part(part, .trigger), "Trigger", 0, 1, 0, stepped: true)
            add(EG13ParamID.part(part, .bendRange), "Bend Range", 0, 24, 2, stepped: true)
            add(EG13ParamID.part(part, .mono), "Mono", 0, 1, 0, stepped: true)
            add(EG13ParamID.part(part, .output), "Output", 0, 7, 0, stepped: true)
            add(EG13ParamID.source(part, 0), "Engine", 0, Double(engineNames.count - 1), Double(part), stepped: true)
            let pNames = ["Harmonics", "Timbre", "Morph", "Frequency", "Decay", "Colour", "Aux Mix", "Level"]
            for p in 1...8 { add(EG13ParamID.source(part, p), pNames[p - 1], 0, 1, 0.5) }
        }
        return core
    }

    private func append(_ descriptor: EG13ParamDescriptor, _ name: String) {
        descriptors.append(descriptor)
        names.append(name)
        values.append(descriptor.defaultValue)
    }

    /// A free-standing parameter for the controls gallery.
    func demoParam(_ name: String, value: Double, min: Double = 0, max: Double = 1, stepped: Bool = false) -> EG13Param {
        nextDemoID += 1
        append(.init(id: nextDemoID, isStepped: stepped, minValue: min, maxValue: max, defaultValue: value), name)
        let synth = EG13Synth(core: self, kind: .instrument)
        demoSynths.append(synth)
        return synth.param(nextDemoID)!
    }

    func param(_ id: UInt32) -> Int { descriptors.firstIndex { $0.id == id }! }

    // MARK: EG13Core

    func paramCount() -> Int { descriptors.count }
    func paramDescriptor(at index: Int) -> EG13ParamDescriptor? { index < descriptors.count ? descriptors[index] : nil }
    func paramName(at index: Int) -> String { names[index] }

    func paramText(at index: Int, value: Double) -> String {
        let d = descriptors[index]
        let i = Int(value.rounded())
        switch d.id {
        case EG13ParamID.master(.mode): return Self.modes[i]
        case EG13ParamID.part(0, .voiceMode), EG13ParamID.part(1, .voiceMode): return Self.voiceModes[i]
        case EG13ParamID.source(0, 0), EG13ParamID.source(1, 0): return Self.engineNames[i]
        default: break
        }
        if d.isStepped { return d.id == EG13ParamID.part(0, .midiChannel) && i == 0 ? "Omni" : "\(i)" }
        if d.minValue < 0 {
            let amount = Int((abs(value) * 100).rounded())
            return amount == 0 ? "C" : (value < 0 ? "L\(amount)" : "R\(amount)")
        }
        return "\(Int((value * 100).rounded()))%"
    }

    func engineCount(slot: UInt32) -> Int { Self.engineNames.count }
    func engineName(slot: UInt32, engine: Int) -> String { Self.engineNames[engine] }
    func engineParamCount(slot: UInt32, engine: Int) -> Int { engine % 2 == 0 ? 8 : 6 }
    func currentEngine(slot: UInt32, part: Int) -> Int { Int(values[param(EG13ParamID.source(part, 0))]) }
    func beginEdit(_ id: UInt32) {}
    func edit(_ id: UInt32, value: Double) {
        values[param(id)] = value
        if id == EG13ParamID.source(0, 0) || id == EG13ParamID.source(1, 0) { generation += 1 }
    }
    func endEdit(_ id: UInt32) { for synth in demoSynths { synth.tick() } }
    func displayFrame() -> EG13DisplayFrame {
        EG13DisplayFrame(values: values, partPeaks: [[0.4, 0.35], [0.2, 0.25]], engineGeneration: generation)
    }
    func requestResize(width: UInt32, height: UInt32) {}
}
