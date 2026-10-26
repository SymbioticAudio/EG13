import AppKit

/// Where a knob's value text breaks. Value text is never cut with "…" and never shrunk: one line if
/// it fits the cell's text width at the knob's value font, otherwise it wraps at its spaces, up to
/// two lines. `fits` is false when even that is not enough (a word too wide); EG13ValueTextTests
/// asserts it is true for every value string of every engine.
public enum EG13ValueFit {
    /// The font the value is set in: the theme's caption, regular, digits of equal width.
    nonisolated(unsafe) public static let font = NSFont.monospacedDigitSystemFont(ofSize: EG13Theme.theme.type.caption.size,
                                                              weight: .regular)
    /// Built once: the theme is fixed, and a body measures several strings.
    nonisolated(unsafe) private static let attributes: [NSAttributedString.Key: Any] = [.font: font]

    public static func width(_ text: String) -> CGFloat {
        ceil((text as NSString).size(withAttributes: attributes).width)
    }

    /// The lines to draw for a value text (its short form, EG13ValueLabels) in `limit` points.
    public static func lines(_ text: String, width limit: CGFloat) -> (lines: [String], fits: Bool) {
        let text = EG13ValueLabels.short(text)
        if self.width(text) <= limit { return ([text], true) }
        let words = text.split(separator: " ").map(String.init)
        guard words.count > 1 else { return ([text], false) }
        // Break at the space that leaves the shorter longest line.
        var best: (lines: [String], widest: CGFloat)?
        for split in 1..<words.count {
            let lines = [words[..<split].joined(separator: " "), words[split...].joined(separator: " ")]
            let widest = lines.map(width).max() ?? 0
            if best == nil || widest < best!.widest { best = (lines, widest) }
        }
        return (best!.lines, best!.widest <= limit)
    }
}
