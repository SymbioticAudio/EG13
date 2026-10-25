import CoreGraphics

/// All window geometry in one place (spec §8 layout, owner 2026-10-20: columns + motion
/// chips). Views read sizes from here; view models never do, so the arrangement can
/// change without touching them. Spacing comes from the theme's spacing tokens.
///
/// The root is drawn at `baseSize` and scaled 75-200 % (plugin/src/gui_size.h). Top to bottom:
/// header, the column strip (four slot cards and the MODS column), the matrix row, the bottom bar.
public enum EG13Layout {
    public static let baseSize = CGSize(width: 1200, height: 760)

    public static let headerHeight: CGFloat = 44
    public static let bottomBarHeight: CGFloat = 78
    public static let matrixHeight: CGFloat = 76
    public static let outerPadding: CGFloat = 12
    public static let gap: CGFloat = 8

    /// Knob diameters are the skirt diameters (owner, 2026-10-20: hero skirt radius 34, small 17).
    public static let cardHeaderHeight: CGFloat = 38
    public static let heroKnobDiameter: CGFloat = 68
    public static let detailKnobDiameter: CGFloat = 34
    public static let barKnobDiameter: CGFloat = 34
    /// A mod card's knobs: smaller, with no value text, so 16 fit in two rows.
    public static let compactKnobDiameter: CGFloat = 28
    public static let slotColumns = 4
    public static let modColumns = 8
    public static let motionChipHeight: CGFloat = 14
    public static let stepperWidth: CGFloat = 88
    public static let heroReadoutWidth: CGFloat = 120
    public static let meterWidth: CGFloat = 8
    public static let meterHeight: CGFloat = 46

    /// The MODS column; the four slot cards share the rest of the width.
    public static let modsColumnWidth: CGFloat = 368

    /// Height left for the column strip.
    public static var columnStripHeight: CGFloat {
        baseSize.height - headerHeight - bottomBarHeight - matrixHeight - 3 * gap - gap
    }

    public static var cardWidth: CGFloat {
        let inner = baseSize.width - 2 * outerPadding - modsColumnWidth - 4 * gap
        return (inner / 4).rounded(.down)
    }

    /// The four mod cards share the strip's height.
    public static var modCardHeight: CGFloat {
        ((columnStripHeight - 3 * 6) / 4).rounded(.down)
    }
}
