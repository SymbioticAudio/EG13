/// The short label each knob shows, in one place. Abbreviations follow the real module panels
/// where they print one (BRIGHT, STRUCT, RES, LPG COL, S.RATE); the full name stays in the
/// tooltip and for VoiceOver. A label may have a space: it wraps to two lines before it is
/// ever cut or shrunk. A name not in the table shows as it is (EG13LayoutTests measures every
/// engine's labels against the label width).
public enum EG13ParamLabels {
    public static let table: [String: String] = [
        // Source
        "Model": "MODEL", "Harmonics": "HARM", "Timbre": "TIMBRE", "Morph": "MORPH", "Frequency": "FREQ",
        "Decay": "DECAY", "LPG Colour": "LPG COL", "Aux Mix": "AUX MIX",
        "Shape": "SHAPE", "Color": "COLOR", "Signature": "SIG", "Bit Depth": "BITS", "Sample Rate": "S.RATE",
        "Contour": "CONTOUR", "Bow": "BOW", "Blow": "BLOW", "Strike": "STRIKE", "Flow": "FLOW",
        "Mallet": "MALLET", "Bow Timbre": "BOW TIM", "Blow Timbre": "BLOW TIM", "Strike Timbre": "STRK TIM",
        "Geometry": "GEOM", "Brightness": "BRIGHT", "Damping": "DAMP", "Position": "POS", "Space": "SPACE",
        "Ext In": "EXT IN", "Structure": "STRUCT", "Exciter": "EXCITER", "Spread": "SPREAD", "Polyphony": "POLY",
        "Gain": "GAIN", "Attack": "ATK", "Release": "REL",
        // Filter, shaper
        "Cutoff": "CUTOFF", "Resonance": "RES", "Mode": "MODE", "Character": "CHAR",
        "Algorithm": "ALGO", "Carrier Level": "CAR LVL", "Mod Level": "MOD LVL", "Carrier": "CARRIER",
        "Modulator": "MOD", "Osc Pitch": "OSC PCH", "Osc Shape": "OSC SHP",
        "High Freq": "HI FREQ", "High Gain": "HI GAIN", "Mid 1 Freq": "MID1 FRQ", "Mid 1 Gain": "MID1 GN",
        "Mid 1 Q": "MID1 Q", "Mid 2 Freq": "MID2 FRQ", "Mid 2 Gain": "MID2 GN", "Mid 2 Q": "MID2 Q",
        "Low Freq": "LO FREQ", "Low Gain": "LO GAIN", "Input Pad": "PAD", "Output": "OUT",
        // Mods
        "Attack Shape": "ATK SHP", "Decay Shape": "DCY SHP", "Sustain": "SUS", "Sustain Time": "SUS TIME",
        "Release Shape": "REL SHP", "Seg 5 A": "SEG5 A", "Seg 5 B": "SEG5 B", "Seg 6 A": "SEG6 A",
        "Seg 6 B": "SEG6 B", "Sync": "SYNC", "Slope": "SLOPE", "Shift": "SHIFT", "Smoothness": "SMTH",
        "Range": "RANGE", "T Model": "T MODEL", "T Deja Vu": "T DEJA", "T Rate": "T RATE", "T Bias": "T BIAS",
        "Deja Vu": "DEJA VU", "X Range": "X RANGE", "X Spread": "X SPRD", "X Bias": "X BIAS", "X Steps": "X STEPS",
        "T Range": "T RANGE", "T Jitter": "T JITR", "Length": "LEN", "X Mode": "X MODE", "X Scale": "X SCALE",
        "X Deja Vu": "X DEJA", "Rate": "RATE", "Wave Shape": "WAVE SHP", "Phase": "PHASE", "Waveform": "WAVE",
        // Part FX
        "Blend": "BLEND", "Size": "SIZE", "Pitch": "PITCH", "Density": "DENS", "Texture": "TEXTURE",
        "Feedback": "FDBK", "Reverb": "REVERB", "Freeze": "FREEZE", "Quality": "QUAL", "In Gain": "IN GAIN",
        "Dry/Wet": "DRY/WET", "Time": "TIME", "Seed": "SEED", "Time Random": "TIME RND",
        "Size Random": "SIZE RND", "Shape Random": "SHAPE RND", "Pitch Random": "PITCH RND",
        "Mix": "MIX", "Tune": "TUNE", "Amount": "AMOUNT", "Diffusion": "DIFF",
        // Bar and matrix
        "Level": "LEVEL", "Pan": "PAN", "Glide": "GLIDE", "Dry": "DRY",
    ]

    /// The label to draw for a parameter's full name.
    public static func short(_ name: String) -> String { table[name] ?? name.uppercased() }
}
