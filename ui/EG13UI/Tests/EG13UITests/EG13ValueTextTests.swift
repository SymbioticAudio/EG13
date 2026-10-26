import AppKit
import Testing
@testable import EG13UI

/// A knob's value text is never cut: measured at the view's font against the narrowest cell the
/// knob's card gives, every value string of every engine fits in at most two lines.
@MainActor
struct EG13ValueTextTests {
    @Test("Every value string of every engine fits its knob cell in at most two lines")
    func valueTextFits() {
        var failures: [String] = []
        for row in EG13ValueStrings.rows {
            let minCell = row.slot == "mod" ? EG13Layout.modMinCell : EG13Layout.slotMinCell
            let width = EG13Layout.labelWidth(cell: minCell)
            for text in row.texts {
                let fit = EG13ValueFit.lines(text, width: width)
                if !fit.fits || fit.lines.count > 2 {
                    failures.append("\(row.engine) \(row.param): \"\(text)\" in \(width) pt")
                }
            }
        }
        #expect(failures.isEmpty, "\(failures.count) too long: \(failures.joined(separator: "; "))")
    }

    @Test("Continuous values of the part bar and matrix fit their cells")
    func barValuesFit() {
        let width = EG13Layout.labelWidth(cell: 64)
        for text in ["100%", "-100%", "+100%", "L100", "R100", "0%", "-12.0 dB", "+6.0 dB", "1.32 s", "20.00 kHz"] {
            #expect(EG13ValueFit.lines(text, width: width).fits, "\(text)")
        }
    }

    @Test("A short form is shorter than its full text and every key is a real string")
    func shortFormsAreReal() {
        let all = Set(EG13ValueStrings.rows.flatMap(\.texts))
        for (full, short) in EG13ValueLabels.table {
            #expect(all.contains(full), "\(full) is not an engine value string")
            #expect(short.count <= full.count, "\(short) is not shorter than \(full)")
        }
    }

    @Test("No two values of one knob share a short form, and every phrase is used")
    func shortFormsAreDistinct() {
        for row in EG13ValueStrings.rows where row.texts.count > 3 {
            let shorts = row.texts.map(EG13ValueLabels.short)
            #expect(Set(shorts).count == Set(row.texts).count, "\(row.engine) \(row.param)")
        }
        let all = EG13ValueStrings.rows.flatMap(\.texts)
        for (full, _) in EG13ValueLabels.phrases {
            #expect(all.contains { $0.contains(full) }, "phrase \"\(full)\" matches no value string")
        }
    }
}
