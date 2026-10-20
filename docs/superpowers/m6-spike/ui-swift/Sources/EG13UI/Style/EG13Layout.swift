import CoreGraphics

/// All window geometry in one place (spec §8 layout, owner 2026-10-20: columns + motion
/// chips). Views read sizes from here; view models never do, so the arrangement can
/// change without touching them. Spacing comes from the theme's spacing tokens.
public enum EG13Layout {
    /// The layout is drawn at this size and scaled 75–200% (plugin/src/gui_size.h).
    public static let baseSize = CGSize(width: 1200, height: 760)

    public static let headerHeight: CGFloat = 52
    public static let bottomBarHeight: CGFloat = 82
    public static let keyboardToggleHeight: CGFloat = 24
    public static let keyboardKeysHeight: CGFloat = 72
    public static let outerPadding: CGFloat = 16
    public static let gap: CGFloat = 12

    /// Column strip: four module cards, then the MODS column. Knob diameters are the
    /// skirt diameters (owner, 2026-10-20: hero skirt radius 34, small 17).
    public static let modsColumnWidth: CGFloat = 248
    public static let cardHeaderHeight: CGFloat = 40
    public static let heroKnobDiameter: CGFloat = 68
    public static let detailKnobDiameter: CGFloat = 34
    public static let barKnobDiameter: CGFloat = 34
    public static let detailColumns = 3
    public static let motionChipHeight: CGFloat = 14
    public static let iconBandHeight: CGFloat = 36
    public static let stepperWidth: CGFloat = 64
    public static let heroReadoutWidth: CGFloat = 120
    public static let meterWidth: CGFloat = 8
    public static let meterHeight: CGFloat = 46

    /// The on-screen keyboard: C1 to C7.
    public static let keyboardRange: ClosedRange<Int> = 24...96

    public static func keyboardHeight(visible: Bool) -> CGFloat {
        keyboardToggleHeight + (visible ? keyboardKeysHeight : 0)
    }

    /// Height left for the column strip; grows when the keyboard is collapsed.
    public static func columnStripHeight(keyboardVisible: Bool) -> CGFloat {
        baseSize.height - headerHeight - bottomBarHeight - gap - keyboardHeight(visible: keyboardVisible)
    }

    public static func cardWidth() -> CGFloat {
        let inner = baseSize.width - 2 * outerPadding - modsColumnWidth - 4 * gap
        return (inner / 4).rounded(.down)
    }
}
