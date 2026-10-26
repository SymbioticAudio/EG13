import Foundation

/// The short form of an enumerated value's text, in one place, like EG13ParamLabels for names.
/// The core's full text stays in the tooltip and for VoiceOver. A text not in the tables shows as
/// it is (EG13ValueTextTests measures every engine's value strings against the knob cell, and
/// that no two values of one knob share a short form).
public enum EG13ValueLabels {
    /// Whole texts with a short form of their own.
    public static let table: [String: String] = [
        "Waveshaping": "Waveshp",
        "Saw/Square": "Saw/Sq",
        "Sympathetic": "Sympath.",
        "Sympathetic Q": "Sympath. Q",
        "Vocoder (long release)": "Vocoder long", "Vocoder (frozen)": "Vocoder frozen",
        "Amplitude": "Ampl.", "Frequency": "Freq.", "Pentatonic": "Penta.", "Envelope": "Env.",
        "OB-Xd Xpander Notch+LP 12 dB/6 dB": "Xpd N+LP 12/6", "OB-Xd Xpander Phaser+LP 18 dB/6 dB": "Xpd Ph+LP 18/6",
        "Comb Negative Feedback, Variable Mix": "Comb -FB Var", "Comb Negative Feedback, Mix 100%": "Comb -FB 100%",
        "Comb Negative Feedback, Mix 50%": "Comb -FB 50%", "Comb Positive Feedback, Variable Mix": "Comb +FB Var",
        "Comb Positive Feedback, Mix 100%": "Comb +FB 100%", "Comb Postive Feedback, Mix 50%": "Comb +FB 50%",
        "Comb Bipolar, Variable Mix": "Comb Bip Var",
        "OB-Xd 4-pole Lowpass 24 dB 24 dB (Legacy)": "OB4 LP 24 Legacy",
    ]

    /// Words and phrases of the Classic Filters' 200 model names, replaced in this order. The model
    /// names are the filter library's ("Vember Classic Lowpass 12 dB Standard"); the replaced
    /// words keep a model recognisable and distinct from its neighbours.
    public static let phrases: [(full: String, short: String)] = [
        ("Vember Classic", "Cls"), ("Vember Ladder", "Ldr"), ("OB-Xd 2-pole", "OB2"), ("OB-Xd 4-pole", "OB4"),
        ("OB-Xd Xpander", "Xpd"), ("Diode Ladder", "Diode"), ("Vintage Ladder", "VL"),
        ("Cutoff Warp", "CW"), ("Resonance Warp", "RW"), ("Tri-pole", "Tri"),
        ("Lowpass", "LP"), ("Highpass", "HP"), ("Bandpass", "BP"), ("Allpass", "AP"),
        ("Standard", "Std"), ("Compensated", "Comp"), ("Continuous", "Cont"), ("Moderate", "Mod"),
        (" Stages", "stg"), (" Stage", "stg"), (" dB/", "/"), (" dB", ""), ("Soft Clip", "Soft"),
        ("Runge-Kutta", "RK"), ("Huovilainen", "Huov"), ("Low/Low/Low", "LLL"), ("Low/High/Low", "LHL"), ("High/Low/High", "HLH"), ("High/High/High", "HHH"), 
    ]

    /// The text to draw under a knob for the core's full value text.
    public static func short(_ text: String) -> String {
        if let short = table[text] { return short }
        // The phrases are for model names. A number ("-6 dB", "1234 Hz") keeps its unit.
        if let first = text.unicodeScalars.first(where: { $0 != "-" && $0 != "+" }),
           CharacterSet.decimalDigits.contains(first) { return text }
        var result = text
        for (full, short) in phrases { result = result.replacingOccurrences(of: full, with: short) }
        return result
    }
}
