import CoreGraphics

/// Which way the Mods are laid out (layout round 2: the owner picks one, then the other goes).
public enum EG13EditorVariant: String, CaseIterable, Sendable {
    /// A: Source, Filter, Shaper and Part FX cards on the left; the four Mods as a column of
    /// horizontal mini-cards on the right.
    case modsColumn = "A"
    /// B: the slot cards use the full width; a VOICE / MODS switch in the header swaps them for
    /// the four Mods cards, also full width.
    case modsPage = "B"
}

/// All window geometry in one place (spec §8 layout, owner 2026-10-20 and 2026-10-25). Views read
/// sizes from here; view models never do, so the arrangement can change without touching them.
///
/// The root is drawn at `baseSize` and scaled 75-200 % (plugin/src/gui_size.h). Top to bottom:
/// header, the card strip (horizontal module cards, one under another), the matrix row, the
/// bottom bar.
public enum EG13Layout {
    public static let baseSize = CGSize(width: 1200, height: 760)

    public static let headerHeight: CGFloat = 56
    public static let bottomBarHeight: CGFloat = 78
    public static let matrixHeight: CGFloat = 76
    public static let outerPadding: CGFloat = 12
    public static let gap: CGFloat = 8

    /// Knob diameters are the skirt diameters: three sizes, taken from the real panels'
    /// large, medium and small knobs (owner, 2026-10-25).
    public static let largeKnobDiameter: CGFloat = 48
    public static let mediumKnobDiameter: CGFloat = 38
    public static let smallKnobDiameter: CGFloat = 28

    public static func knobDiameter(_ size: EG13KnobSize) -> CGFloat {
        switch size {
        case .large: return largeKnobDiameter
        case .medium: return mediumKnobDiameter
        case .small: return smallKnobDiameter
        }
    }

    /// A card's header strip: category, engine menu (and a mod's Scope).
    public static let cardHeaderHeight: CGFloat = 28
    public static let cardPadding: CGFloat = 6
    public static let rowGap: CGFloat = 4
    /// The engine menu's well: wide enough for the longest engine name ("State Variable Filter").
    public static let enginePickerWidth: CGFloat = 190
    public static let motionChipHeight: CGFloat = 14
    public static let stepperWidth: CGFloat = 64
    /// The wider stepper well for "Per voice", "Other part", "Part mix" and the like.
    public static let wideStepperWidth: CGFloat = 88
    public static let meterWidth: CGFloat = 8
    public static let meterHeight: CGFloat = 46

    /// Narrowest knob cell of a slot card and of a mod card. A label's text has the cell's width
    /// less `labelInset` (a 2 pt margin and the chip's 3 pt padding on each side).
    public static let slotMinCell: CGFloat = 64
    public static let modMinCell: CGFloat = 50
    public static let labelInset: CGFloat = 8
    /// A cell is never wider than this: a card with few knobs packs them to the left.
    public static let maxCell: CGFloat = 96

    /// The strip is the full inner width; variant A gives the right-hand part to the Mods.
    public static var stripWidth: CGFloat { baseSize.width - 2 * outerPadding }
    public static let modsColumnWidth: CGFloat = 470

    public static func slotCardWidth(_ variant: EG13EditorVariant) -> CGFloat {
        variant == .modsColumn ? stripWidth - modsColumnWidth - gap : stripWidth
    }

    public static func modCardWidth(_ variant: EG13EditorVariant) -> CGFloat {
        variant == .modsColumn ? modsColumnWidth : stripWidth
    }

    /// Height left for the card strip.
    public static var stripHeight: CGFloat {
        baseSize.height - headerHeight - bottomBarHeight - matrixHeight - 3 * gap - gap
    }

    /// Knob rows of `count` knobs in `width` points, rows balanced: 16 knobs in a width that
    /// holds 10 are two rows of 8, never 10 and 6. The cell is as wide as the row allows.
    public static func gridShape(count: Int, width: CGFloat, minCell: CGFloat) -> (columns: Int, cell: CGFloat) {
        guard count > 0 else { return (1, width) }
        let most = max(1, Int((width / minCell).rounded(.down)))
        let rows = (count + most - 1) / most
        let columns = (count + rows - 1) / rows
        return (columns, min((width / CGFloat(columns)).rounded(.down), maxCell))
    }

    /// The width a knob label's text may use in a cell.
    public static func labelWidth(cell: CGFloat) -> CGFloat { cell - labelInset }
}
