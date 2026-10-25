import SwiftUI
import EG13UI
import SymbioticTokens

/// The Controls page (Task 7a): every EG13KnobStyle option side by side, each labelled with
/// its option, for the owner to choose from. Both part accents, the engine picker and the
/// stepped controls. Parameters are made once, so dragging one sample moves only that one.
@MainActor
final class EG13ControlsDemo {
    let core: EG13PreviewCore
    private var params: [String: EG13Param] = [:]

    init(core: EG13PreviewCore) { self.core = core }

    func param(_ key: String, _ name: String, value: Double = 0.62, min: Double = 0, max: Double = 1,
               stepped: Bool = false) -> EG13Param {
        if let existing = params[key] { return existing }
        let made = core.demoParam(name, value: value, min: min, max: max, stepped: stepped)
        params[key] = made
        return made
    }
}

struct EG13ControlsPage: View {
    let demo: EG13ControlsDemo
    @Environment(\.symbioticTheme) private var theme

    static let size = CGSize(width: 1240, height: 640)

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            HStack(alignment: .firstTextBaseline) {
                Text("EG13 controls, round 3 (approved)")
                    .font(.system(size: 22, weight: .light))
                Text("the approved controls, sizes, engine picker and steppers, in each part accent.")
                    .font(theme.type.caption.font)
                Spacer()
            }
            .foregroundStyle(theme.text.primary.color)
            HStack(alignment: .top, spacing: theme.spacing.m) {
                EG13PartGallery(demo: demo, part: 0)
                EG13PartGallery(demo: demo, part: 1)
            }
        }
        .padding(theme.spacing.l)
        .frame(width: Self.size.width, height: Self.size.height, alignment: .topLeading)
        .background(theme.surface.window.color)
    }
}

private struct EG13PartGallery: View {
    let demo: EG13ControlsDemo
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    private var base: EG13KnobStyle { EG13KnobStyle.default.accented(part: part) }
    private func with(_ change: (inout EG13KnobStyle) -> Void) -> EG13KnobStyle {
        var style = base
        change(&style)
        return style
    }

    var body: some View {
        let letter = EG13PartName.letter(part)
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            HStack(spacing: theme.spacing.s) {
                Text(letter)
                    .font(theme.type.title.font.weight(.heavy))
                    .foregroundStyle(theme.text.inverse.color)
                    .frame(width: 26, height: 26)
                    .background(RoundedRectangle(cornerRadius: 3).fill(EG13Palette.part(part).ink.color))
                Text(part == 0 ? "PART A, teal" : "PART B, pink")
                    .font(theme.type.caption.font.weight(.bold))
                Spacer()
            }
            row("APPROVED CONTROLS: arc in the skirt band, no ticks", [
                ("default: white cap, plain label", base),
                ("emphasised: accent cap + chip", with { $0.cap = .accent; $0.label = .chip }),
                ("secondary: black cap, part arc", with { $0.cap = .black }),
                ("default, arc at minimum", base),
            ], values: [0.45, 0.6, 0.75, 0.0])
            sizes
            controls
        }
        .foregroundStyle(theme.text.primary.color)
        .padding(theme.spacing.m)
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(theme.surface.panel.color))
    }

    private func row(_ title: String, _ items: [(String, EG13KnobStyle)], values: [Double]) -> some View {
        VStack(alignment: .leading, spacing: theme.spacing.xs) {
            Text(title).font(theme.type.caption.font.weight(.bold)).foregroundStyle(theme.text.secondary.color)
            HStack(alignment: .top, spacing: 0) {
                ForEach(items.indices, id: \.self) { i in
                    VStack(spacing: theme.spacing.xxs) {
                        EG13Knob(param: demo.param("\(part).\(title).\(i)", "Harmonics", value: values[i]),
                                 style: items[i].1, size: .hero)
                        Text(items[i].0).font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(theme.text.secondary.color)
                            .multilineTextAlignment(.center)
                            .frame(width: 125)
                    }
                    .frame(maxWidth: .infinity)
                }
            }
        }
    }

    private var sizes: some View {
        VStack(alignment: .leading, spacing: theme.spacing.xs) {
            Text("SIZES: hero, detail, bar; bipolar; stepped (default style)")
                .font(theme.type.caption.font.weight(.bold)).foregroundStyle(theme.text.secondary.color)
            HStack(alignment: .top, spacing: theme.spacing.xl) {
                EG13Knob(param: demo.param("\(part).size.0", "Harmonics"), style: base, size: .hero)
                EG13Knob(param: demo.param("\(part).size.1", "Timbre"), style: base, size: .detail)
                EG13Knob(param: demo.param("\(part).size.2", "Pan", value: 0.3, min: -1, max: 1), style: base, size: .bar)
                EG13Knob(param: demo.param("\(part).size.3", "Voices", value: 5, min: 1, max: 8, stepped: true),
                         style: base, size: .detail)
                Spacer(minLength: 0)
            }
            Text("hero, detail, bar (pan, bipolar: arc from centre), detail stepped (voices)")
                .font(.system(size: 10, design: .monospaced)).foregroundStyle(theme.text.secondary.color)
        }
    }

    private var controls: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text("ENGINE PICKER AND STEPPED CONTROLS")
                .font(theme.type.caption.font.weight(.bold)).foregroundStyle(theme.text.secondary.color)
            let selector = demo.param("\(part).engine", "Engine", value: Double(part * 2), max: 7, stepped: true)
            HStack(spacing: theme.spacing.m) {
                EG13EnginePicker(names: EG13PreviewCore.engineNames, selector: selector,
                                 selected: Int(selector.value), accessibilityName: "Part \(letter) Source engine")
                EG13Stepped(param: demo.param("\(part).voices", "Voices", value: 8, min: 1, max: 8, stepped: true))
                EG13Stepped(param: demo.param("\(part).mode", "Voice mode", value: 1, max: 3, stepped: true))
                EG13Stepped(param: demo.param("\(part).chan", "Channel", value: 3, max: 16, stepped: true))
                Spacer(minLength: 0)
            }
        }
    }

    private var letter: String { EG13PartName.letter(part) }
}
