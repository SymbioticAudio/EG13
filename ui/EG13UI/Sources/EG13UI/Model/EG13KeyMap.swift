import Foundation

/// The key-range graphic's geometry and rules (spec §8, M6b decision 7): a non-playing 88-key
/// strip, A0 to C8, that shows each part's key range and the split note, and whose handles edit
/// Key Low, Key High and Split Note. Pure functions, so the view stays drawing and gestures.
public enum EG13KeyMap {
    public static let lowestKey = 21  // A0
    public static let highestKey = 108  // C8
    public static let keyCount = highestKey - lowestKey + 1

    /// What a drag can move.
    public enum Handle: Hashable, Sendable {
        case low(part: Int)
        case high(part: Int)
        case split
    }

    /// The left edge of a key, in a strip `width` wide; a key past either end is clamped to it.
    public static func x(forKey key: Int, width: Double) -> Double {
        let index = clampedKey(key) - lowestKey
        return width * Double(index) / Double(keyCount)
    }

    /// The right edge of a key.
    public static func trailingX(forKey key: Int, width: Double) -> Double {
        let index = clampedKey(key) - lowestKey + 1
        return width * Double(index) / Double(keyCount)
    }

    /// The key under an x position; outside the strip it is the nearest end key.
    public static func key(forX x: Double, width: Double) -> Int {
        guard width > 0, !x.isNaN else { return lowestKey }
        let index = Int((x / width * Double(keyCount)).rounded(.down))
        return clampedKey(lowestKey + index)
    }

    public static func clampedKey(_ key: Int) -> Int { min(max(key, lowestKey), highestKey) }

    /// The key a handle moves to when dragged to `key`: clamped to the strip, and a part's
    /// Low never above its High nor High below its Low.
    public static func dragged(_ handle: Handle, to key: Int, low: Int, high: Int) -> Int {
        let strip = clampedKey(key)
        switch handle {
        case .low: return min(strip, max(clampedKey(high), lowestKey))
        case .high: return max(strip, min(clampedKey(low), highestKey))
        case .split: return strip
        }
    }

    public static func isWhite(_ key: Int) -> Bool {
        switch ((key % 12) + 12) % 12 {
        case 1, 3, 6, 8, 10: return false
        default: return true
        }
    }

    /// "C4" is MIDI note 60, as the core writes it.
    public static func noteName(_ key: Int) -> String {
        let names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        let note = max(key, 0)
        return "\(names[note % 12])\(note / 12 - 1)"
    }

    /// Part B's band shows only when Part B can sound: not in Single.
    public static func showsBand(part: Int, mode: EG13Mode) -> Bool { part == 0 || mode != .single }

    /// The split marker shows only in Split mode.
    public static func showsSplitMarker(mode: EG13Mode) -> Bool { mode == .split }

    /// The handle's VoiceOver label, "Part A lowest key, C2".
    public static func accessibilityLabel(_ handle: Handle, key: Int) -> String {
        let name = noteName(key)
        switch handle {
        case .low(let part): return "\(EG13PartName.name(part)) lowest key, \(name)"
        case .high(let part): return "\(EG13PartName.name(part)) highest key, \(name)"
        case .split: return "Split note, \(name)"
        }
    }
}
