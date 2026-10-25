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

    public init(values: [Double], partPeaks: [[Float]], activeVoices: [Int] = [0, 0],
                engineGeneration: UInt32 = 0) {
        self.values = values
        self.partPeaks = partPeaks
        self.activeVoices = activeVoices
        self.engineGeneration = engineGeneration
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
    func requestResize(width: UInt32, height: UInt32)
}
