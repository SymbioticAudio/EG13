import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens

/// The MODS column: four compact mod cards, each with a number chip, the engine menu, the
/// Scope stepper and a grid of the engine's knobs (no value text under them: it is in the
/// tooltip and for VoiceOver). Choosing matrix sources and destinations is M6b.
struct EG13ModsColumn: View {
    let synth: EG13Synth
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    private static let kinds: [EG13SlotKind] = [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3,
                                                EG13_SLOT_KIND_MOD4]

    var body: some View {
        VStack(spacing: 6) {
            ForEach(Self.kinds.indices, id: \.self) { index in
                EG13ModCard(synth: synth, kind: Self.kinds[index], number: index + 1, part: part)
            }
        }
        .frame(width: EG13Layout.modsColumnWidth)
        .frame(maxHeight: .infinity)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Modulation")
    }
}

/// One mod slot.
public struct EG13ModCard: View {
    let synth: EG13Synth
    let kind: EG13SlotKind
    let number: Int
    let part: Int
    @Environment(\.symbioticTheme) private var theme

    public init(synth: EG13Synth, kind: EG13SlotKind, number: Int, part: Int) {
        self.synth = synth
        self.kind = kind
        self.number = number
        self.part = part
    }

    public var body: some View {
        let slot = synth.slot(kind, part: part)
        let controls = synth.slotControls(kind, part: part)
        VStack(spacing: theme.spacing.xs) {
            HStack(spacing: theme.spacing.s) {
                Text("\(number)")
                    .font(theme.type.caption.font.weight(.bold))
                    .foregroundStyle(theme.text.inverse.color)
                    .frame(width: 18, height: 18)
                    .background(RoundedRectangle(cornerRadius: 2).fill(EG13Palette.groupChip.color))
                    .accessibilityHidden(true)
                EG13SlotPicker(synth: synth, slot: slot,
                               accessibilityName: "\(EG13PartName.name(part)) Mod \(number) engine")
                Spacer(minLength: 0)
                if let scope = controls.scope {
                    EG13ControlView(synth: synth, spec: scope, part: part)
                }
            }
            EG13ControlGrid(columns: EG13Layout.modColumns, specs: controls.params, rowSpacing: 4) { spec in
                EG13ControlView(synth: synth, spec: spec, part: part)
            }
            Spacer(minLength: 0)
        }
        .padding(theme.spacing.xs)
        .frame(maxWidth: .infinity)
        .frame(height: EG13Layout.modCardHeight)
        .background(theme.surface.panel.color)
        .clipShape(RoundedRectangle(cornerRadius: theme.radius.s))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Mod \(number): \(slot.engineName)")
    }
}
