import AppKit
import SwiftUI
import SymbioticTokens

/// EG13's one fixed theme (spec §8): surfaces and text from EG13Palette, everything that
/// is not a colour (typography, spacing, radii) from symbiotic-design's studioDark theme.
/// No theme switching; the macOS appearance is ignored.
public enum EG13Theme {
    public static let theme = make()

    /// Builds the theme with symbiotic-design's public initialisers.
    public static func make() -> Theme {
        Theme(
            id: "eg13",
            appearance: .light,
            isPlaceholder: false,
            surface: Theme.SurfaceColors(
                window: EG13Palette.window,
                panel: EG13Palette.panel,
                raised: EG13Palette.panelHeader,
                inset: EG13Palette.rack,
                separator: Theme.symbiotic.graph.grid,
                selection: EG13Palette.teal.bright,
                focusRing: EG13Palette.teal.ink),
            text: Theme.TextColors(
                primary: EG13Palette.legend,
                secondary: EG13Palette.groupChip,
                disabled: Theme.symbiotic.text.disabled,
                inverse: EG13Palette.white),
            accent: Theme.AccentColors(accent: EG13Palette.teal.ink, onAccent: EG13Palette.white),
            state: Theme.symbiotic.state,
            meter: Theme.studioDark.meter,
            graph: Theme.symbiotic.graph,
            control: Theme.symbiotic.control,
            strip: Theme.symbiotic.strip,
            brand: Theme.symbiotic.brand,
            spacing: Theme.studioDark.spacing,
            radius: Theme.studioDark.radius,
            iconStroke: Theme.studioDark.iconStroke,
            type: Theme.studioDark.type)
    }

    /// The AppKit appearance for the hosting view: always this theme's, never the system's.
    @MainActor
    public static var appearance: NSAppearance? {
        NSAppearance(named: theme.appearance.nsAppearanceName(
            highContrast: NSWorkspace.shared.accessibilityDisplayShouldIncreaseContrast))
    }
}

/// Module categories: a small uppercase label on each card's header strip (not colour-coded).
public enum EG13Category: CaseIterable, Sendable {
    case source, filter, shaper, partFx, mod

    public var title: String {
        switch self {
        case .source: return "SOURCE"
        case .filter: return "FILTER"
        case .shaper: return "SHAPER"
        case .partFx: return "PART FX"
        case .mod: return "MODS"
        }
    }
}

public enum EG13PartName {
    public static func name(_ part: Int) -> String { part == 0 ? "Part A" : "Part B" }
    public static func letter(_ part: Int) -> String { part == 0 ? "A" : "B" }
}
