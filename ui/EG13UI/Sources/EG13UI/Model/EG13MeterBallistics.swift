import Foundation

/// Peak meter ballistics for one channel, in linear amplitude. Instant attack; the level
/// falls from full scale to silence in `decayTime`; the peak hold stays for `holdTime`,
/// then falls at the same rate. Time is passed in, so tests need no clock.
public struct EG13MeterBallistics: Equatable, Sendable {
    public static let decayTime: Double = 0.3
    public static let holdTime: Double = 1.5
    public static let floorDB: Double = -60

    public private(set) var level: Float = 0
    public private(set) var hold: Float = 0
    private var holdAge: Double = 0

    public init() {}

    /// peak: the largest |sample| since the previous call. dt: seconds since the previous call.
    public mutating func update(peak: Float, dt: Double) {
        let step = Float(max(0, dt) / Self.decayTime)
        level = max(max(0, peak), level - step)
        if level >= hold {
            hold = level
            holdAge = 0
        } else {
            holdAge += max(0, dt)
            if holdAge > Self.holdTime { hold = max(level, hold - step) }
        }
    }

    // MARK: Drawing helpers

    /// dB of a linear level, floored at floorDB.
    public static func decibels(_ linear: Float) -> Double {
        linear > 0 ? max(floorDB, 20 * log10(Double(linear))) : floorDB
    }

    /// 0...1 for drawing: floorDB maps to 0, 0 dBFS to 1 (above stays 1).
    public static func fraction(_ linear: Float) -> Double {
        min(1, max(0, (decibels(linear) - floorDB) / -floorDB))
    }

    public static func isClipping(_ linear: Float) -> Bool { linear >= 1 }
}
