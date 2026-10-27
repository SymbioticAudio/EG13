import Foundation
import EG13CBridge
import EG13UIFixtures
import EG13UI

/// A fake plugin for the preview: parameters live in memory, nothing needs a DAW. It has the
/// plugin's whole table (158 parameters per part, 7 master) with the real ids, and the real
/// engine and P names (EG13EngineCatalog). The tests have their own recording fake
/// (EG13FakeCore, test target only).
@MainActor
final class EG13PreviewCore: EG13Core {
    /// The controls gallery's engine menu (not a plugin table).
    static let engineNames = ["Virtual Analog", "Waveshaper", "Two-op FM", "Grain Cloud", "Additive",
                              "Wavetable", "Chord", "Formant"]
    static let voiceModes = ["Poly", "Mono", "Legato"]
    static let modes = ["Single", "Layer", "Split", "Multi"]
    static let scopes = ["Per voice", "Per part"]
    static let triggers = ["MIDI", "Always on"]
    static let onOff = ["Off", "On"]
    static let outputs = ["Main", "Bus 2", "Other part"]
    static let inputSources = ["External", "Other part", "Both"]
    static let mpeModes = ["Off", "Lower zone", "Upper zone"]
    static let partFxInputs = ["Part mix", "Input", "Part mix + Input"]
    static let voiceGains = ["-12 dB", "0 dB"]
    static let latencyModes = ["Live", "Studio"]

    /// The engines of each slot kind (EG13SlotKind order).
    static func engines(_ kind: Int) -> [EG13CatalogEngine] {
        switch kind {
        case 0: return EG13EngineCatalog.source
        case 1: return EG13EngineCatalog.filter
        case 2: return EG13EngineCatalog.shaper
        case 7: return EG13EngineCatalog.partFx
        default: return EG13EngineCatalog.mod
        }
    }

    private var descriptors: [EG13ParamDescriptor] = []
    private var names: [String] = []
    private var values: [Double] = []
    private var indexByID: [UInt32: Int] = [:]
    private var nextDemoID: UInt32 = 0x00F0_0000
    /// The engine selected in each slot kind, per part.
    private var current = [[Int]](repeating: [0, 0], count: 8)
    /// Bumped when an engine selector is edited, as the plugin does on an engine switch.
    private var generation: UInt32 = 0
    /// The gallery's parameters each live in a synth of their own (a Param is a handle on one).
    private var demoSynths: [EG13Synth] = []

    /// The init patch's engines: State Variable Filter, two envelopes and an LFO.
    static func standard() -> EG13PreviewCore {
        let core = EG13PreviewCore()
        core.build()
        core.select(EG13_SLOT_KIND_FILTER, engine: 3, part: 0)
        core.select(EG13_SLOT_KIND_FILTER, engine: 3, part: 1)
        for part in 0..<EG13ParamID.parts {
            core.select(EG13_SLOT_KIND_MOD1, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD2, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD3, engine: 2, part: part)
            core.select(EG13_SLOT_KIND_SOURCE, engine: part == 0 ? 0 : 3, part: part)
            core.select(EG13_SLOT_KIND_PART_FX, engine: part == 0 ? 1 : 4, part: part)
        }
        return core
    }

    /// Selects an engine in one slot of one part, as choosing it in the menu does.
    func select(_ kind: EG13SlotKind, engine: Int, part: Int) {
        let kindIndex = Int(kind.rawValue)
        current[kindIndex][part] = engine
        values[indexByID[EG13ParamID.slot(part, kind, 0)]!] = Double(engine)
        generation += 1
    }

    private func build() {
        func add(_ id: UInt32, _ name: String, _ min: Double, _ max: Double, _ def: Double, stepped: Bool = false) {
            append(.init(id: id, isStepped: stepped, minValue: min, maxValue: max, defaultValue: def), name)
        }
        for part in 0..<EG13ParamID.parts {
            for raw in 0..<8 {
                let kind = EG13SlotKind(rawValue: UInt32(raw))
                add(EG13ParamID.slot(part, kind, 0), raw == 0 || EG13ParamID.isModSlot(kind) ? "Engine" : "Type",
                    0, 31, 0, stepped: true)
                for p in 1...EG13ParamID.slotParamCount(kind) {
                    add(EG13ParamID.slot(part, kind, p), "(unused)", 0, 1, 0.5)
                }
                if EG13ParamID.isModSlot(kind) { add(EG13ParamID.modScope(part, kind), "Scope", 0, 1, 0, stepped: true) }
            }
            for route in 0..<EG13ParamID.routeCount {
                add(EG13ParamID.routeAmount(part, route), "Route \(route + 1)", -1, 1, route < 3 ? [1, 0.3, 0.1][route] : 0)
            }
            add(EG13ParamID.part(part, .level), "Level", 0, 1, 1)
            add(EG13ParamID.part(part, .pan), "Pan", -1, 1, 0)
            add(EG13ParamID.part(part, .glide), "Glide", 0, 1, 0)
            add(EG13ParamID.part(part, .transpose), "Transpose", -24, 24, 0, stepped: true)
            add(EG13ParamID.part(part, .baseNote), "Base Note", 0, 127, 60, stepped: true)
            add(EG13ParamID.part(part, .midiChannel), "MIDI Channel", 0, 16, 0, stepped: true)
            add(EG13ParamID.part(part, .keyLow), "Key Low", 0, 127, 0, stepped: true)
            add(EG13ParamID.part(part, .keyHigh), "Key High", 0, 127, 127, stepped: true)
            add(EG13ParamID.part(part, .voiceMode), "Voice Mode", 0, 2, 0, stepped: true)
            add(EG13ParamID.part(part, .voiceCount), "Voices", 1, 8, 8, stepped: true)
            add(EG13ParamID.part(part, .trigger), "Trigger", 0, 1, 0, stepped: true)
            add(EG13ParamID.part(part, .bendRange), "Bend Range", 0, 24, 2, stepped: true)
            add(EG13ParamID.part(part, .mono), "Mono", 0, 1, 0, stepped: true)
            add(EG13ParamID.part(part, .output), "Output", 0, 2, 0, stepped: true)
            add(EG13ParamID.part(part, .inputSource), "Input Source", 0, 2, 0, stepped: true)
            add(EG13ParamID.part(part, .mpeMode), "MPE", 0, 2, 0, stepped: true)
            add(EG13ParamID.part(part, .mpeBendRange), "MPE Bend Range", 0, 96, 48, stepped: true)
            add(EG13ParamID.part(part, .ccA), "CC A", 0, 127, 2, stepped: true)
            add(EG13ParamID.part(part, .ccB), "CC B", 0, 127, 4, stepped: true)
            add(EG13ParamID.part(part, .ccC), "CC C", 0, 127, 11, stepped: true)
            add(EG13ParamID.part(part, .ccD), "CC D", 0, 127, 16, stepped: true)
            add(EG13ParamID.part(part, .partFxInput), "Part FX Input", 0, 2, 0, stepped: true)
        }
        add(EG13ParamID.master(.level), "Level", 0, 1, 0.8)
        add(EG13ParamID.master(.dry), "Dry", 0, 1, 0)
        add(EG13ParamID.master(.mode), "Mode", 0, 3, 0, stepped: true)
        add(EG13ParamID.master(.splitNote), "Split Note", 0, 127, 60, stepped: true)
        add(EG13ParamID.master(.voiceGain), "Voice Gain", 0, 1, 0, stepped: true)
        add(EG13ParamID.master(.softClip), "Soft Clip", 0, 1, 1, stepped: true)
        add(EG13ParamID.master(.latencyMode), "Latency Mode", 0, 1, 0, stepped: true)
        // Varied starting positions, so the knobs in screenshots do not all point the same way.
        for index in values.indices where !descriptors[index].isStepped && descriptors[index].minValue == 0
            && descriptors[index].maxValue == 1 && descriptors[index].defaultValue == 0.5 {
            let id = descriptors[index].id
            // Split up so every Swift version type-checks it quickly.
            let low: UInt32 = (id & 0xFF) * 37
            let mid: UInt32 = ((id >> 8) & 0xFF) * 11
            let high: UInt32 = ((id >> 16) & 0xFF) * 5
            let seed = Double(low + mid + high)
            let fraction: Double = (seed * 0.381966).truncatingRemainder(dividingBy: 1)
            values[index] = 0.12 + 0.76 * fraction
        }
    }

    private func append(_ descriptor: EG13ParamDescriptor, _ name: String) {
        indexByID[descriptor.id] = descriptors.count
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

    /// Slot kind (EG13SlotKind order) and part of a slot parameter id, with its P (0 is the selector).
    private func slotOf(_ id: UInt32) -> (kind: Int, part: Int, p: Int)? {
        let part = Int(id >> 16), slot = Int((id >> 8) & 0xFF), p = Int(id & 0xFF)
        let kinds = [1: 0, 2: 1, 3: 2, 4: 7, 5: 3, 6: 4, 7: 5, 8: 6]
        guard part < EG13ParamID.parts, let kind = kinds[slot] else { return nil }
        return (kind, part, p)
    }

    // MARK: EG13Core

    func paramCount() -> Int { descriptors.count }
    func paramDescriptor(at index: Int) -> EG13ParamDescriptor? { index < descriptors.count ? descriptors[index] : nil }

    func paramName(at index: Int) -> String {
        let id = descriptors[index].id
        guard let (kind, part, p) = slotOf(id), p >= 1,
              p <= EG13ParamID.slotParamCount(EG13SlotKind(rawValue: UInt32(kind))) else { return names[index] }
        let engine = Self.engines(kind)[current[kind][part]]
        return p <= engine.params.count ? engine.params[p - 1] : "(unused)"
    }

    func paramText(at index: Int, value: Double) -> String {
        let d = descriptors[index]
        let i = Int(value.rounded())
        func pick(_ list: [String]) -> String { list[min(max(i, 0), list.count - 1)] }
        if let (kind, _, p) = slotOf(d.id) {
            if p == 0 { return pick(Self.engines(kind).map(\.name)) }
            if d.isStepped { return pick(Self.scopes) }  // a mod slot's Scope
            return "\(Int((value * 100).rounded()))%"
        }
        switch d.id {
        case EG13ParamID.master(.mode): return pick(Self.modes)
        case EG13ParamID.master(.voiceGain): return pick(Self.voiceGains)
        case EG13ParamID.master(.softClip): return pick(Self.onOff)
        case EG13ParamID.master(.latencyMode): return pick(Self.latencyModes)
        case EG13ParamID.master(.splitNote): return "\(i)"
        default: break
        }
        if let part = EG13ParamID.partIndex(of: d.id) {
            func isPart(_ p: EG13PartParam) -> Bool { d.id == EG13ParamID.part(part, p) }
            if isPart(.voiceMode) { return pick(Self.voiceModes) }
            if isPart(.trigger) { return pick(Self.triggers) }
            if isPart(.mono) { return pick(Self.onOff) }
            if isPart(.output) { return pick(Self.outputs) }
            if isPart(.inputSource) { return pick(Self.inputSources) }
            if isPart(.mpeMode) { return pick(Self.mpeModes) }
            if isPart(.partFxInput) { return pick(Self.partFxInputs) }
            if isPart(.midiChannel) { return i == 0 ? "Omni" : "\(i)" }
            if isPart(.transpose) { return String(format: "%+d st", i) }
            if isPart(.bendRange) || isPart(.mpeBendRange) { return "+/-\(i) st" }
            if [.ccA, .ccB, .ccC, .ccD].contains(where: isPart) { return "CC \(i)" }
            if isPart(.baseNote) || isPart(.keyLow) || isPart(.keyHigh) {
                let note = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"][i % 12]
                return "\(note)\(i / 12 - 1)"
            }
        }
        if d.isStepped { return "\(i)" }
        if d.minValue < 0 {
            let amount = Int((abs(value) * 100).rounded())
            if (d.id >> 8) & 0xFF == UInt32(EG13_SLOT_MATRIX.rawValue) { return amount == 0 ? "0%" : "\(value < 0 ? "-" : "+")\(amount)%" }
            return amount == 0 ? "C" : (value < 0 ? "L\(amount)" : "R\(amount)")
        }
        return "\(Int((value * 100).rounded()))%"
    }

    func engineCount(slot: UInt32) -> Int { Self.engines(Int(slot)).count }
    func engineName(slot: UInt32, engine: Int) -> String { Self.engines(Int(slot))[engine].name }
    func engineParamCount(slot: UInt32, engine: Int) -> Int { Self.engines(Int(slot))[engine].params.count }
    func currentEngine(slot: UInt32, part: Int) -> Int { current[Int(slot)][part] }
    func beginEdit(_ id: UInt32) {}
    func edit(_ id: UInt32, value: Double) {
        values[indexByID[id]!] = value
        if let (kind, part, p) = slotOf(id), p == 0 {
            let count = Self.engines(kind).count
            let engine = Int(value.rounded())
            current[kind][part] = engine >= 0 && engine < count ? engine : 0
            generation += 1
        }
    }
    func endEdit(_ id: UInt32) { for synth in demoSynths { synth.tick() } }
    func displayFrame() -> EG13DisplayFrame {
        frameCount += 1
        let time = Double(frameCount) / Self.displayRate
        var modOutMin = [[[Float]]](repeating: [[Float]](repeating: [Float](repeating: .infinity, count: 8), count: 4), count: 2)
        var modOutMax = [[[Float]]](repeating: [[Float]](repeating: [Float](repeating: -.infinity, count: 8), count: 4), count: 2)
        for part in 0..<EG13ParamID.parts {
            let now = time + Self.partOffset * Double(part)
            for slot in 0..<4 {
                let engine = Self.engines(3 + slot)[current[3 + slot][part]].name
                for output in 0..<EG13MatrixNames.outputCount(engine: engine) {
                    // The range over the frame just ended: four samples across 1/30 s.
                    var low = Float.infinity, high = -Float.infinity
                    for sample in 0..<4 {
                        let at = now - (3 - Double(sample)) / (4 * Self.displayRate)
                        if let value = Self.modOutput(engine: engine, slot: slot, output: output, at: at) {
                            low = min(low, Float(value))
                            high = max(high, Float(value))
                        }
                    }
                    modOutMin[part][slot][output] = low
                    modOutMax[part][slot][output] = high
                }
            }
        }
        return EG13DisplayFrame(values: values, partPeaks: [[0.4, 0.35], [0.2, 0.25]],
                                activeVoices: heldChord ? [4, 4] : [0, 0], engineGeneration: generation,
                                modValues: modValues(time: time), modOutMin: modOutMin, modOutMax: modOutMax)
    }

    // MARK: Modulation

    /// The preview's display rate: its 30 Hz tick, and so the unit of its animation clock.
    static let displayRate = 30.0
    /// Part B runs this far behind Part A, so the two parts' traces differ.
    static let partOffset = 0.35
    /// Display frames asked for so far; time is this over the display rate, so the animation
    /// follows the tick (a snapshot of N ticks is the same every run).
    private var frameCount = 0
    /// A chord held down: voices sounding in both parts.
    var heldChord = false

    /// A mod engine's output at time `at`, or nil when it produces nothing (an idle envelope). An
    /// envelope retriggers every 2 s (attack 40 ms, decay to a sustain of 0.45, release from 1.3 s
    /// to 1.8 s, then idle); an LFO is a bipolar sine near 1 Hz, its second output the cosine; the
    /// others are slow unipolar waves, or steps.
    static func modOutput(engine: String, slot: Int, output: Int, at time: Double) -> Double? {
        switch engine {
        case "Envelope", "Function Generator":
            let cycle = (time + 0.5 * Double(slot) + 0.3 * Double(output)).truncatingRemainder(dividingBy: 2)
            switch cycle {
            case ..<0.04: return cycle / 0.04
            case ..<0.5: return 1 - 0.55 * (cycle - 0.04) / 0.46
            case ..<1.3: return 0.45
            case ..<1.8: return 0.45 * (1 - (cycle - 1.3) / 0.5)
            default: return nil
            }
        case "LFO":
            let phase = 2 * Double.pi * (1.1 * time + 0.2 * Double(slot))
            return output == 0 ? sin(phase) : cos(phase)
        case "Random / S&H":
            let step = (time * 3).rounded(.down) + Double(output) * 7.3
            return abs(sin(step * 12.9898)).truncatingRemainder(dividingBy: 1)
        default:
            return 0.5 + 0.5 * sin(2 * Double.pi * 0.4 * time + Double(output))
        }
    }

    /// A source's level at `time`: a mod output (nil when idle reads 0), or a fixed source.
    private func level(part: Int, source: UInt16, time: Double) -> Double {
        switch source {
        case 0: return 1
        case 1: return 0.8   // velocity
        case 9: return 0.5   // mod wheel
        case 32..<64:
            let slot = Int(source - 32) / 8, output = Int(source - 32) % 8
            let engine = Self.engines(3 + slot)[current[3 + slot][part]].name
            return Self.modOutput(engine: engine, slot: slot, output: output,
                                  at: time + Self.partOffset * Double(part)) ?? 0
        default: return 0.5
        }
    }

    /// The final value at each destination a route targets: the parameter's value plus the sum of
    /// its routes' amounts times the source levels (times the via's), as the plugin reports it. NaN
    /// elsewhere.
    private func modValues(time: Double) -> [[Float]] {
        var result = [[Float]](repeating: [Float](repeating: .nan, count: 144), count: EG13ParamID.parts)
        for part in 0..<EG13ParamID.parts {
            var sums: [Int: Double] = [:]
            for (index, route) in routes[part].enumerated() where route.source != 0 && route.destination != 0 {
                guard sourceName(part: part, code: route.source) != nil,
                      destinationName(part: part, code: route.destination) != nil else { continue }
                let amount = values[indexByID[EG13ParamID.routeAmount(part, index)]!]
                let via = route.via == 0 ? 1 : level(part: part, source: route.via, time: time)
                sums[Int(route.destination), default: 0] += amount * via * level(part: part, source: route.source, time: time)
            }
            for (destination, sum) in sums {
                result[part][destination] = Float(min(max(base(part: part, destination: destination) + sum, 0), 1))
            }
        }
        return result
    }

    /// The set value of a destination, 0...1 (Pitch sits at the centre, Amp at 1).
    private func base(part: Int, destination: Int) -> Double {
        switch destination {
        case 1: return 0.5
        case 2: return 1
        case 4: return values[indexByID[EG13ParamID.part(part, .level)]!]
        default:
            let kinds: [(base: Int, kind: Int)] = [(16, 0), (32, 1), (48, 2), (64, 7), (80, 3), (96, 4), (112, 5), (128, 6)]
            for entry in kinds where destination >= entry.base && destination < entry.base + 16 {
                let id = EG13ParamID.slot(part, EG13SlotKind(rawValue: UInt32(entry.kind)), destination - entry.base + 1)
                return indexByID[id].map { values[$0] } ?? 0
            }
            return 0
        }
    }

    /// Puts a route in the matrix with its amount (the preview's stand-in for "Modulate from…").
    func addRoute(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16 = 0, amount: Double) {
        routes[part][index] = (source, destination, via)
        values[indexByID[EG13ParamID.routeAmount(part, index)]!] = amount
        generation += 1
    }

    /// The init patch with a chord held, for the modulation visuals: more routes, so several cards
    /// show a Motion button and several knobs a mark.
    static func modulationDemo() -> EG13PreviewCore {
        let core = standard()
        core.heldChord = true
        for part in 0..<EG13ParamID.parts {
            core.addRoute(part: part, index: 1, source: 40, destination: 32, amount: 0.45)   // Mod 2 -> Cutoff
            core.addRoute(part: part, index: 3, source: 32, destination: 33, amount: 0.25)   // Mod 1 -> Resonance
            core.addRoute(part: part, index: 4, source: 48, destination: 17, amount: -0.3)   // Mod 3 -> Harmonics
            core.addRoute(part: part, index: 5, source: 40, destination: 19, amount: 0.35)   // Mod 2 -> Morph
            core.addRoute(part: part, index: 6, source: 1, destination: 18, amount: 0.2)     // Velocity -> Timbre
            core.addRoute(part: part, index: 7, source: 49, destination: 64, amount: 0.3)    // Mod 3 Out 2 -> Blend
            // Three routes into Mod 1's own knobs: its header strip has three routes in one Motion list.
            core.addRoute(part: part, index: 8, source: 49, destination: 81, amount: 0.2)
            core.addRoute(part: part, index: 9, source: 40, destination: 82, amount: -0.2)
            core.addRoute(part: part, index: 10, source: 1, destination: 83, amount: 0.15)
        }
        return core
    }

    /// The `--mod-load` patch: the init patch, a chord held, three modulated knobs (Cutoff,
    /// Resonance, Harmonics) and three traces (the three mod slots with an engine).
    static func modulationLoad() -> EG13PreviewCore {
        let core = standard()
        core.heldChord = true
        for part in 0..<EG13ParamID.parts {
            core.addRoute(part: part, index: 3, source: 32, destination: 33, amount: 0.25)
            core.addRoute(part: part, index: 4, source: 48, destination: 17, amount: -0.3)
        }
        return core
    }
    func requestResize(width: UInt32, height: UInt32) {}

    // MARK: Matrix

    /// The init patch's routes (spec §6): Mod 1 to Amp via Velocity, Mod 2 to Filter P1, Mod 3 to
    /// Pitch via the mod wheel; the rest are free.
    private var routes: [[(source: UInt16, destination: UInt16, via: UInt16)]] = (0..<EG13ParamID.parts).map { _ in
        [(32, 2, 1), (40, 32, 0), (48, 1, 9)] + Array(repeating: (0, 0, 0), count: EG13ParamID.routeCount - 3)
    }

    private func modEngineNames(part: Int) -> [String] {
        (3...6).map { Self.engines($0)[current[$0][part]].name }
    }

    func route(part: Int, index: Int) -> EG13ModRoute {
        guard part >= 0, part < EG13ParamID.parts, index >= 0, index < EG13ParamID.routeCount else { return EG13ModRoute() }
        let r = routes[part][index]
        let amount = values[indexByID[EG13ParamID.routeAmount(part, index)]!]
        return makeRoute(part: part, source: r.source, destination: r.destination, via: r.via, amount: amount)
    }

    func setRoute(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16) -> Bool {
        guard part >= 0, part < EG13ParamID.parts, index >= 0, index < EG13ParamID.routeCount,
              source == 0 || sourceName(part: part, code: source) != nil,
              via == 0 || sourceName(part: part, code: via) != nil,
              destination == 0 || destinationName(part: part, code: destination) != nil else { return false }
        routes[part][index] = (source, destination, via)
        return true
    }

    func sourceName(part: Int, code: UInt16) -> String? {
        EG13MatrixNames.sourceName(code: code, modEngines: modEngineNames(part: part))
    }

    func destinationName(part: Int, code: UInt16) -> String? {
        EG13MatrixNames.destinationName(code: code) { kind in
            Self.engines(kind)[self.current[kind][part]].params
        }
    }

    func destination(forParam id: UInt32) -> UInt16 {
        if id >> 16 < UInt32(EG13ParamID.parts), (id >> 8) & 0xFF == 0, id & 0xFF == 1 { return 4 }  // Part Level
        guard let (kind, _, p) = slotOf(id), p >= 1, p <= EG13ParamID.slotParamCount(EG13SlotKind(rawValue: UInt32(kind))) else { return 0 }
        let bases: [Int: Int] = [0: 16, 1: 32, 2: 48, 7: 64, 3: 80, 4: 96, 5: 112, 6: 128]
        return UInt16(bases[kind]! + p - 1)
    }

    func version() -> String { "preview" }
}
