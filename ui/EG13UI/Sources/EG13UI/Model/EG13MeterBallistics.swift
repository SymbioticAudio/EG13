import Foundation

/// Peak meter ballistics for one channel. The meter draws in dB, so it falls in dB: from a
/// new peak's level down to the floor in `decayTime`, a straight line on the meter. Instant
/// attack. The peak hold stays for `holdTime`, then falls at the same rate. Levels are
/// linear amplitude. Time is passed in, so tests need no clock.
public struct EG13MeterBallistics: Equatable, Sendable {
    public static let decayTime: Double = 0.3
    public static let holdTime: Double = 1.5
    public static let floorDB: Double = -60

    public private(set) var level: Float = 0
    public private(set) var hold: Float = 0
    private var levelDB = floorDB
    private var holdDB = floorDB
    private var fallRate: Double = 0  // dB per second, set by the last peak
    private var holdRate: Double = 0
    private var holdAge: Double = 0

    public init() {}

    /// peak: the largest |sample| since the previous call. dt: seconds since the previous call.
    public mutating func update(peak: Float, dt: Double) {
        let step = max(0, dt)
        // Decay first, then attack against the decayed level, so a steady signal below the
        // last peak holds the meter at that signal instead of dipping under it for a frame.
        levelDB = max(Self.floorDB, levelDB - fallRate * step)
        let peakDB = Self.decibels(peak)
        if peakDB > Self.floorDB, peakDB >= levelDB {
            level = peak
            levelDB = peakDB
            fallRate = (levelDB - Self.floorDB) / Self.decayTime
        } else {
            level = levelDB <= Self.floorDB ? 0 : Float(pow(10, levelDB / 20))
        }
        if level >= hold {
            hold = level
            holdDB = levelDB
            holdRate = fallRate
            holdAge = 0
        } else {
            holdAge += step
            if holdAge > Self.holdTime {
                holdDB = max(Self.floorDB, holdDB - holdRate * step)
                let fallen: Float = holdDB <= Self.floorDB ? 0 : Float(pow(10, holdDB / 20))
                hold = max(level, fallen)
            }
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
