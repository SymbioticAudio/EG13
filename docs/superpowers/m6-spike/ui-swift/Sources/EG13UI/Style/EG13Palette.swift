import SwiftUI
import SymbioticTokens

/// EG13's own colour palette (owner, 2026-10-20), sampled from Mutable-style panels:
/// light panels with near-black legends, black skirts, white caps, two accents (teal and
/// pink-red), and an LCD for readouts. EG13 owns its colours; symbiotic-design supplies
/// icons, typography, spacing and control behaviour. Module categories are not
/// colour-coded. This is the only file in EG13UI allowed to contain colour literals
/// (scripts/check-ui-tokens.sh enforces it).
public enum EG13Palette {
    /// Bright (caps, part tabs) and ink (arcs, chips with white text at 4.5:1 or better).
    public struct Pair: Sendable, Equatable {
        public let bright: ThemeColor
        public let ink: ThemeColor
    }

    public enum AccentRole: Sendable, Equatable { case teal, pink }

    public static let teal = Pair(bright: ThemeColor(hex: 0x00A1A7FF), ink: ThemeColor(hex: 0x007F8AFF))
    public static let pink = Pair(bright: ThemeColor(hex: 0xDA4061FF), ink: ThemeColor(hex: 0xC42C4EFF))

    /// Mid-grey window (#2A2A2A rack dark is the alternative under consideration).
    public static let window = ThemeColor(hex: 0x8C8C89FF)
    public static let panel = ThemeColor(hex: 0xE6E6E6FF)
    public static let panelHeader = ThemeColor(hex: 0xD5D5D4FF)
    public static let placeholderPanel = ThemeColor(hex: 0xCFCFCEFF)
    /// Header and keyboard bar.
    public static let rack = ThemeColor(hex: 0x333333FF)
    public static let groupChip = ThemeColor(hex: 0x6E6E6EFF)
    public static let black = ThemeColor(hex: 0x000000FF)
    public static let white = ThemeColor(hex: 0xFFFFFFFF)
    public static let legend = ThemeColor(hex: 0x232020FF)
    public static let lcdBackground = ThemeColor(hex: 0x383838FF)
    public static let lcdSegment = ThemeColor(hex: 0xAFD22CFF)
    /// Spare.
    public static let peach = ThemeColor(hex: 0xDFC0ACFF)

    public static func accent(_ role: AccentRole) -> Pair { role == .teal ? teal : pink }

    /// Part A teal, Part B pink.
    public static func part(_ part: Int) -> Pair { part == 0 ? teal : pink }

    /// The colours EG13's controls are drawn in (EG13ControlStyle.flat).
    public static var controls: EG13ControlColors {
        EG13ControlColors(
            track: white.color.opacity(0.14),
            tick: legend.color.opacity(0.6),
            skirt: black.color,
            innerRing: white.color.opacity(0.2),
            plainCap: white.color,
            darkCap: black.color,
            indicator: black.color,
            indicatorOnDark: white.color,
            label: legend.color,
            chipText: white.color,
            lcdBackground: lcdBackground.color,
            lcdText: lcdSegment.color,
            focus: teal.ink.color)
    }

    /// A knob's colours from its emphasis (owner, 2026-10-20): an accent cap has an arc in
    /// the accent's bright colour and an ink chip; a plain white cap has an arc in the
    /// part's colour; a dark cap (master knobs, small black knobs) has a light grey arc.
    public static func look(for emphasis: EG13ParamEmphasis, part: Int) -> EG13KnobLook {
        switch emphasis.cap {
        case .plain:
            return EG13KnobLook(cap: .plain, arc: self.part(part).bright.color)
        case .accent(let role):
            let pair = accent(role)
            return EG13KnobLook(cap: .filled(pair.bright.color), arc: pair.bright.color, chip: pair.ink.color)
        case .black:
            return EG13KnobLook(cap: .dark, arc: panel.color)
        }
    }

    /// Master Dry and Out: dark caps, light grey arcs.
    public static var masterLook: EG13KnobLook { EG13KnobLook(cap: .dark, arc: panel.color) }
}
