import Foundation

/// One host parameter as the editor sees it.
public struct EG13ParamDescriptor: Equatable, Sendable {
    public var id: UInt32
    public var isStepped: Bool
    public var minValue: Double
    public var maxValue: Double
    public var defaultValue: Double

    public init(id: UInt32, isStepped: Bool,
                minValue: Double, maxValue: Double, defaultValue: Double) {
        self.id = id
        self.isStepped = isStepped
        self.minValue = minValue
        self.maxValue = maxValue
        self.defaultValue = defaultValue
    }
}

/// What the editor reads at display rate. The peaks are read-and-reset: one consumer polls.
public struct EG13DisplayFrame: Equatable, Sendable {
    /// Parameter values by index.
    public var values: [Double]
    /// Largest absolute sample per part since the previous frame: [part][left, right].
    public var partPeaks: [[Float]]
    /// Voices sounding per part.
    public var activeVoices: [Int]
    /// Changes on every engine switch, state load or adopt: the cue to re-read descriptors.
    public var engineGeneration: UInt32
    /// The final normalized value at each destination code: [part][destination]. NaN means none.
    /// Pitch is semitones / 48 + 0.5 and pan 0...1; the editor reads the destinations of P's.
    public var modValues: [[Float]]
    /// Min and max of each mod output since the previous frame: [part][slot][output]. +inf (min)
    /// and -inf (max) mean the output produced nothing.
    public var modOutMin: [[[Float]]]
    public var modOutMax: [[[Float]]]

    public init(values: [Double], partPeaks: [[Float]], activeVoices: [Int] = [0, 0],
                engineGeneration: UInt32 = 0, modValues: [[Float]] = [],
                modOutMin: [[[Float]]] = [], modOutMax: [[[Float]]] = []) {
        self.values = values
        self.partPeaks = partPeaks
        self.activeVoices = activeVoices
        self.engineGeneration = engineGeneration
        self.modValues = modValues
        self.modOutMin = modOutMin
        self.modOutMax = modOutMax
    }
}

/// The plugin, seen from the editor (core-api/include/eg13/core_api.h). Main actor only.
/// The real implementation is EG13CoreAPIClient; tests use a fake.
@MainActor
public protocol EG13Core: AnyObject {
    func paramCount() -> Int
    func paramDescriptor(at index: Int) -> EG13ParamDescriptor?
    func paramName(at index: Int) -> String
    func paramText(at index: Int, value: Double) -> String
    func engineCount(slot: UInt32) -> Int
    func engineName(slot: UInt32, engine: Int) -> String
    /// How many of the slot's P parameters the engine uses.
    func engineParamCount(slot: UInt32, engine: Int) -> Int
    /// The engine selected in the part's slot.
    func currentEngine(slot: UInt32, part: Int) -> Int
    func beginEdit(_ id: UInt32)
    func edit(_ id: UInt32, value: Double)
    func endEdit(_ id: UInt32)
    func displayFrame() -> EG13DisplayFrame
    /// Matrix route `index` of the part: codes, names (nil when the part's engines hide the code)
    /// and the amount. A part or index out of range gives an empty route.
    func route(part: Int, index: Int) -> EG13ModRoute
    /// Sets a route's source, destination and via. False, and nothing changes, for a bad part,
    /// route or code, including a code the part's current engines hide. The amount is a host
    /// parameter: edit it through beginEdit, edit and endEdit.
    func setRoute(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16) -> Bool
    /// "Velocity", "Mod 1 Envelope"; nil for 0, reserved codes and codes the engines hide.
    func sourceName(part: Int, code: UInt16) -> String?
    /// "Pitch", "Filter: Cutoff"; nil for 0, reserved codes and codes the engines hide.
    func destinationName(part: Int, code: UInt16) -> String?
    /// The destination code of a P parameter (Filter P1 is 32) or of Part Level (4), 0 for any other parameter.
    func destination(forParam id: UInt32) -> UInt16
    /// "0.6.0 (abc1234)".
    func version() -> String
    func requestResize(width: UInt32, height: UInt32)
    /// This plugin kind's factory presets, in the plugin's list order (Bank 1, Bank 2, FX).
    func factoryPresets() -> [EG13PresetEntry]
    /// Loads a preset file's sound (a Performance into both parts, a Patch into the target part).
    /// The plugin keeps the setup fields. `.invalid` changed nothing.
    func applyPreset(json: String, target: EG13PresetTarget) -> EG13PresetApplyResult
    /// The current sound as a preset file: a Patch of `part`, or a Performance when nil. Empty if
    /// it could not be made.
    func savePreset(part: Int?, name: String) -> String
    /// The browser string the project saved ("" for none).
    func browserState() -> String
    func setBrowserState(_ state: String)
}
