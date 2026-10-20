import Foundation

/// One host parameter as the editor sees it.
public struct EG13ParamDescriptor: Equatable, Sendable {
    public var id: UInt32
    public var isStepped: Bool
    public var isHidden: Bool
    public var minValue: Double
    public var maxValue: Double
    public var defaultValue: Double

    public init(id: UInt32, isStepped: Bool, isHidden: Bool = false,
                minValue: Double, maxValue: Double, defaultValue: Double) {
        self.id = id
        self.isStepped = isStepped
        self.isHidden = isHidden
        self.minValue = minValue
        self.maxValue = maxValue
        self.defaultValue = defaultValue
    }
}

/// What the editor reads at display rate.
public struct EG13DisplayFrame: Equatable, Sendable {
    /// Parameter values by index.
    public var values: [Double]
    /// Largest absolute sample per part since the previous frame: [part][left, right].
    public var partPeaks: [[Float]]

    public init(values: [Double], partPeaks: [[Float]]) {
        self.values = values
        self.partPeaks = partPeaks
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
    /// The engine's stable id (EngineInfo.id), e.g. "plaits".
    func engineID(slot: UInt32, engine: Int) -> String
    func beginEdit(_ id: UInt32)
    func edit(_ id: UInt32, value: Double)
    func endEdit(_ id: UInt32)
    func noteOn(channel: Int, key: Int, velocity: Double)
    func noteOff(channel: Int, key: Int)
    func displayFrame() -> EG13DisplayFrame
    func requestZoom(_ zoom: Double)
}
