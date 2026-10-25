/// One engine of a slot, as the core reports it: display name and the names of the P's it uses.
public struct EG13CatalogEngine: Sendable {
    public let name: String
    public let params: [String]
    public init(_ name: String, _ params: [String]) {
        self.name = name
        self.params = params
    }
}

/// The plugin's engine tables, mirrored (preview and tests use it; the plugin reads the live names from the core) so the cards show real engine names and
/// P names (names the core marks "(unused)" stay in the middle of an engine's range, as in the
/// plugin). Copied from slotEngineInfo() in core/synth/param_table.cpp (EngineInfo::displayName
/// and params[].name); update both together when an engine is added.
public enum EG13EngineCatalog {
    /// Mirrors the source engine table (core/engine, EngineInfo: display name and P names).
    public static let source: [EG13CatalogEngine] = [
        .init("Macro Oscillator", ["Model", "Harmonics", "Timbre", "Morph", "Frequency", "Decay", "LPG Colour", "Aux Mix"]),
        .init("Digital Oscillator", ["Shape", "Timbre", "Color", "Frequency", "Signature", "Bit Depth", "Sample Rate"]),
        .init("Modal Synth", ["Model", "Contour", "Bow", "Blow", "Strike", "Flow", "Mallet", "Bow Timbre", "Blow Timbre", "Strike Timbre", "Geometry", "Brightness", "Damping", "Position", "Space", "Ext In"]),
        .init("Resonator", ["Model", "Structure", "Brightness", "Damping", "Position", "Exciter", "Frequency", "Spread", "Polyphony"]),
        .init("Audio Input", ["Gain", "Attack", "Release"]),
    ]

    /// Mirrors the filter engine table (core/engine, EngineInfo: display name and P names).
    public static let filter: [EG13CatalogEngine] = [
        .init("Off", []),
        .init("Analog Filter", ["Frequency", "Resonance", "Mode"]),
        .init("Classic Filters", ["Cutoff", "Resonance", "Model", "Character"]),
        .init("State Variable Filter", ["Cutoff", "Resonance", "Mode"]),
    ]

    /// Mirrors the shaper engine table (core/engine, EngineInfo: display name and P names).
    public static let shaper: [EG13CatalogEngine] = [
        .init("Off", []),
        .init("Ring Mod / Vocoder", ["Algorithm", "Timbre", "Carrier Level", "Mod Level", "Carrier", "Modulator", "Osc Pitch", "Osc Shape", "Mode"]),
        .init("EQ", ["High Freq", "High Gain", "Mid 1 Freq", "Mid 1 Gain", "Mid 1 Q", "Mid 2 Freq", "Mid 2 Gain", "Mid 2 Q", "Low Freq", "Low Gain", "Input Pad", "Output"]),
    ]

    /// Mirrors the mod1 engine table (core/engine, EngineInfo: display name and P names).
    public static let mod: [EG13CatalogEngine] = [
        .init("Off", []),
        .init("Envelope", ["Mode", "Attack", "Attack Shape", "Decay", "Decay Shape", "Sustain", "Sustain Time", "Release", "Release Shape", "Seg 5 A", "Seg 5 B", "Seg 6 A", "Seg 6 B"]),
        .init("LFO", ["Mode", "Sync", "Frequency", "Shape", "Slope", "Shift", "Smoothness", "(unused)", "(unused)", "Output", "Range"]),
        .init("Random / S&H", ["T Model", "T Deja Vu", "T Rate", "T Bias", "Deja Vu", "X Range", "X Spread", "X Bias", "X Steps", "T Range", "T Jitter", "Length", "X Mode", "X Scale", "X Deja Vu", "Sync"]),
        .init("Function Generator", ["Mode", "Attack", "Rate", "Decay", "Wave Shape", "Sustain", "Phase", "Release", "Smoothness", "(unused)", "Waveform"]),
    ]

    /// Mirrors the partFx engine table (core/engine, EngineInfo: display name and P names).
    public static let partFx: [EG13CatalogEngine] = [
        .init("Off", []),
        .init("Granular", ["Blend", "Position", "Size", "Pitch", "Density", "Texture", "Feedback", "Reverb", "Freeze", "Mode", "Quality", "Spread", "In Gain"]),
        .init("Granular Delay", ["Dry/Wet", "Time", "Size", "Pitch", "Density", "Shape", "Feedback", "Reverb", "Freeze", "Quality", "Seed", "(unused)", "Time Random", "Size Random", "Shape Random", "Pitch Random"]),
        .init("Resonator", ["Mix", "Structure", "Brightness", "Damping", "Spread", "Position", "(unused)", "(unused)", "(unused)", "Model", "Polyphony", "(unused)", "Tune"]),
        .init("Reverb", ["Amount", "Time", "Diffusion", "Brightness", "(unused)", "(unused)", "(unused)", "(unused)", "Freeze"]),
    ]

}

extension EG13EngineCatalog {
    /// The engines of each slot kind (EG13SlotKind raw order: Source, Filter, Shaper, Mod 1-4, Part FX).
    public static func engines(kind: Int) -> [EG13CatalogEngine] {
        switch kind {
        case 0: return source
        case 1: return filter
        case 2: return shaper
        case 7: return partFx
        default: return mod
        }
    }
}
