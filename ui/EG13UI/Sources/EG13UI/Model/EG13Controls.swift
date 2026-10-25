import EG13CBridge

/// One control of the editor: which parameter it edits and how it is drawn. The views render
/// from these specs and nothing else, and the reachability test walks the same list, so a
/// parameter with no spec has no control on screen.
public struct EG13ControlSpec: Equatable, Identifiable, Sendable {
    public enum Kind: Equatable, Sendable {
        /// A knob. Slot P's are `.detail`; the bottom bar and the matrix use `.bar`.
        case knob(EG13KnobSize)
        /// The compact knob of a mod card: smaller, and no value text under it.
        case compactKnob
        /// A minus / LCD / plus stepper.
        case stepped
        /// A slot's engine menu (the selector parameter).
        case enginePicker
        /// The master Mode segmented control.
        case segmented
    }

    public var id: UInt32
    public var kind: Kind
    public var emphasis: EG13ParamEmphasis = .plain
    /// A label to show instead of the parameter's name ("Out" for the master Level).
    public var label: String?
}

/// A slot's controls: its engine menu, the P's the engine uses, and a mod slot's Scope.
public struct EG13SlotControls: Equatable, Sendable {
    public var kind: EG13SlotKind
    public var engine: EG13ControlSpec?
    public var params: [EG13ControlSpec]
    public var scope: EG13ControlSpec?

    public var all: [EG13ControlSpec] { (engine.map { [$0] } ?? []) + params + (scope.map { [$0] } ?? []) }
}

/// Every control of the editor for the shown part, by region.
public struct EG13EditorControls: Equatable, Sendable {
    public var part: Int
    /// The master Mode control in the header, and the master settings popover.
    public var header: [EG13ControlSpec]
    public var masterSettings: [EG13ControlSpec]
    /// Source, Filter, Shaper and Part FX, in column order.
    public var cards: [EG13SlotControls]
    /// The four mod slots.
    public var mods: [EG13SlotControls]
    /// The 12 route amounts.
    public var matrix: [EG13ControlSpec]
    /// Part Level, Pan, Glide, Voice Mode, Voices.
    public var partBar: [EG13ControlSpec]
    /// Master Dry and Out.
    public var masterBar: [EG13ControlSpec]
    /// The rest of the part's settings, in the Part settings popover.
    public var partSettings: [EG13ControlSpec]

    public var all: [EG13ControlSpec] {
        header + masterSettings + cards.flatMap(\.all) + mods.flatMap(\.all) + matrix + partBar + masterBar
            + partSettings
    }
}

extension EG13Synth {
    /// What the engine slot of the card or mod slot shows. Names a core marks unused are hidden.
    func slotControls(_ kind: EG13SlotKind, part: Int) -> EG13SlotControls {
        let slot = slot(kind, part: part)
        let isMod = EG13ParamID.isModSlot(kind)
        let picker = slot.engineNames.isEmpty || param(slot.selectorID) == nil
            ? nil : EG13ControlSpec(id: slot.selectorID, kind: .enginePicker)
        let params = slot.descriptors.enumerated().compactMap { index, descriptor -> EG13ControlSpec? in
            guard param(descriptor.id) != nil, name(descriptor.id) != EG13Slot.unusedName else { return nil }
            return EG13ControlSpec(
                id: descriptor.id, kind: isMod ? .compactKnob : .knob(.detail),
                emphasis: EG13Emphasis.slotParam(kind: kind, engine: slot.engine, p: index + 1))
        }
        let scopeID = EG13ParamID.modScope(part, kind)
        let scope = isMod && param(scopeID) != nil ? EG13ControlSpec(id: scopeID, kind: .stepped) : nil
        return EG13SlotControls(kind: kind, engine: picker, params: params, scope: scope)
    }

    /// Every control the editor shows for one part (and the master). Rebuilt on demand; cheap.
    public func editorControls(part shown: Int) -> EG13EditorControls {
        func spec(_ id: UInt32, _ kind: EG13ControlSpec.Kind, _ emphasis: EG13ParamEmphasis = .plain,
                  label: String? = nil) -> EG13ControlSpec? {
            param(id) == nil ? nil : EG13ControlSpec(id: id, kind: kind, emphasis: emphasis, label: label)
        }
        func steppers(_ params: [EG13PartParam]) -> [EG13ControlSpec] {
            params.compactMap { spec(EG13ParamID.part(shown, $0), .stepped) }
        }
        let cardKinds: [EG13SlotKind] = [EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER,
                                         EG13_SLOT_KIND_PART_FX]
        let modKinds: [EG13SlotKind] = [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3,
                                        EG13_SLOT_KIND_MOD4]
        // Part Level is the bar's main control (accent); Dry and Out are secondary (black).
        let partBar: [EG13ControlSpec] = [
            spec(EG13ParamID.part(shown, .level), .knob(.bar), .teal),
            spec(EG13ParamID.part(shown, .pan), .knob(.bar)),
            spec(EG13ParamID.part(shown, .glide), .knob(.bar)),
            spec(EG13ParamID.part(shown, .voiceMode), .stepped),
            spec(EG13ParamID.part(shown, .voiceCount), .stepped),
        ].compactMap { $0 }
        return EG13EditorControls(
            part: shown,
            header: [spec(EG13ParamID.master(.mode), .segmented)].compactMap { $0 },
            masterSettings: [
                spec(EG13ParamID.master(.splitNote), .stepped),
                spec(EG13ParamID.master(.voiceGain), .stepped),
                spec(EG13ParamID.master(.softClip), .stepped),
                spec(EG13ParamID.master(.latencyMode), .stepped),
            ].compactMap { $0 },
            cards: cardKinds.map { slotControls($0, part: shown) },
            mods: modKinds.map { slotControls($0, part: shown) },
            matrix: (0..<EG13ParamID.routeCount).compactMap {
                spec(EG13ParamID.routeAmount(shown, $0), .knob(.bar), .black, label: "Route \($0 + 1)")
            },
            partBar: partBar,
            masterBar: [
                spec(EG13ParamID.master(.dry), .knob(.bar), .black),
                spec(EG13ParamID.master(.level), .knob(.bar), .black, label: "Out"),
            ].compactMap { $0 },
            partSettings: steppers([.transpose, .baseNote, .midiChannel, .keyLow, .keyHigh, .trigger,
                                    .bendRange, .mono, .output, .inputSource, .partFxInput,
                                    .mpeMode, .mpeBendRange, .ccA, .ccB, .ccC, .ccD]))
    }
}
