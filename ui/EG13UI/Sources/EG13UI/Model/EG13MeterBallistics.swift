import Foundation

/// Peak meter ballistics: instant attack, falls at fallRate dB per second.
public struct EG13MeterBallistics: Equatable, Sendable {
    public static let floorDB: Double = -60
    public static let fallRate: Double = 24

    public private(set) var levelDB: Double = floorDB

    public init() {}

    public mutating func update(peak: Float, elapsed: Double) {
        let peakDB = peak > 0 ? max(Self.floorDB, 20 * log10(Double(peak))) : Self.floorDB
        let fallen = max(Self.floorDB, levelDB - Self.fallRate * max(0, elapsed))
        levelDB = max(peakDB, fallen)
    }

    /// 0...1 for drawing: floorDB maps to 0, 0 dBFS to 1 (above 0 dBFS stays 1).
    public var fraction: Double { min(1, max(0, (levelDB - Self.floorDB) / -Self.floorDB)) }
    public var isClipping: Bool { levelDB >= 0 }
}
