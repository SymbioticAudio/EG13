import Keyboard
import SwiftUI
import SymbioticTokens
import SymbioticIcons

/// Collapsible on-screen keyboard (AudioKit Keyboard, keys drawn in EG13's colours).
/// A strip on each key shows which part it plays, so the split point is visible.
struct EG13KeyboardPanel: View {
    @Bindable var synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: theme.spacing.s) {
                Button { synth.keyboardVisible.toggle() } label: {
                    HStack(spacing: theme.spacing.xs) {
                        Icon(synth.keyboardVisible ? .navDown : .navUp, size: 12)
                        Text("KEYBOARD").font(theme.type.caption.font)
                    }
                }
                .buttonStyle(.plain)
                .accessibilityLabel(synth.keyboardVisible ? "Hide keyboard" : "Show keyboard")
                Text(routingDescription).font(theme.type.caption.font)
                Spacer()
            }
            .foregroundStyle(theme.text.inverse.color)
            .frame(height: EG13Layout.keyboardToggleHeight)
            if synth.keyboardVisible {
                keys
                    .frame(height: EG13Layout.keyboardKeysHeight)
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel("Keyboard")
                    .accessibilityValue(routingDescription)
            }
        }
        .padding(.horizontal, EG13Layout.outerPadding)
        .background(theme.surface.inset.color)
    }

    private var routingDescription: String {
        let split = synth.masterParam(.splitNote)?.text ?? ""
        switch synth.mode {
        case .single: return "Plays Part A"
        case .layer: return "Plays both parts"
        case .split: return "Split at \(split): Part A below, Part B above"
        case .multi: return "Plays \(EG13PartName.name(synth.selectedPart))"
        }
    }

    private var keys: some View {
        let range = EG13Layout.keyboardRange
        let map = synth.keyMap
        return Keyboard(
            layout: .piano(pitchRange: Pitch(Int8(range.lowerBound))...Pitch(Int8(range.upperBound))),
            noteOn: { pitch, point in
                synth.noteOn(key: Int(pitch.midiNoteNumber),
                             velocity: EG13KeyMap.velocity(forVerticalPosition: Double(point.y)))
            },
            noteOff: { pitch in synth.noteOff(key: Int(pitch.midiNoteNumber)) }
        ) { pitch, isActivated in
            let parts = map.parts(forKey: Int(pitch.midiNoteNumber))
            ZStack(alignment: .top) {
                KeyboardKey(pitch: pitch, isActivated: isActivated, text: "",
                            whiteKeyColor: EG13Palette.panel.color,
                            blackKeyColor: EG13Palette.black.color,
                            pressedColor: EG13Palette.part(parts.first ?? 0).ink.color,
                            flatTop: true)
                VStack(spacing: 0) {
                    ForEach(parts, id: \.self) { part in
                        Rectangle().fill(EG13Palette.part(part).bright.color).frame(height: 3)
                    }
                }
            }
        }
    }
}
