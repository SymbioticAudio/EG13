import EG13CBridge

/// One control of the editor: which parameter it edits and how it is drawn. The views render
/// from these specs and nothing else, and the reachability test walks the same list, so a
/// parameter with no spec has no control on screen.
public struct EG13ControlSpec: Equatable, Identifiable, Sendable {
    public enum Kind: Equatable, Sendable {
        /// A knob in one of the three sizes.
        case knob(EG13KnobSize)
        /// A minus / LCD / plus stepper.
        case stepped
        /// The same with a wider well, for "Per voice", "Other part", "Part mix".
        case wideStepped
        /// A mod card's Scope: a wide stepper with no label under it.
        case scope
        /// A slot's engine menu (the selector parameter).
        case enginePicker(EG13SlotKind)
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
    /// The master settings in the header: Mode, Latency Mode, Split Note, Voice Gain, Soft Clip.
    public var header: [EG13ControlSpec]
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
        header + cards.flatMap(\.all) + mods.flatMap(\.all) + matrix + partBar + masterBar
            + partSettings
    }
}

extension EG13Synth {
    /// What the engine slot of the card or mod slot shows. Names a core marks unused are hidden.
    func slotControls(_ kind: EG13SlotKind, part: Int) -> EG13SlotControls {
        let slot = slot(kind, part: part)
        let isMod = EG13ParamID.isModSlot(kind)
        let picker = slot.engineNames.count < 2 || param(slot.selectorID) == nil
            ? nil : EG13ControlSpec(id: slot.selectorID, kind: .enginePicker(kind))
        let params = slot.descriptors.enumerated().compactMap { index, descriptor -> EG13ControlSpec? in
            guard param(descriptor.id) != nil, name(descriptor.id) != EG13Slot.unusedName else { return nil }
            let knob = EG13PanelLayout.knob(kind: kind, engine: slot.engineName, p: index + 1)
            return EG13ControlSpec(id: descriptor.id, kind: .knob(knob.size),
                                   emphasis: knob.emphasised ? .accent : .plain)
        }
        let scopeID = EG13ParamID.modScope(part, kind)
        let scope = isMod && param(scopeID) != nil ? EG13ControlSpec(id: scopeID, kind: .scope) : nil
        return EG13SlotControls(kind: kind, engine: picker, params: params, scope: scope)
    }

    /// Every control the editor shows for one part (and the master). Rebuilt on demand; cheap.
    public func editorControls(part shown: Int) -> EG13EditorControls {
        func spec(_ id: UInt32, _ kind: EG13ControlSpec.Kind, _ emphasis: EG13ParamEmphasis = .plain,
                  label: String? = nil) -> EG13ControlSpec? {
            param(id) == nil ? nil : EG13ControlSpec(id: id, kind: kind, emphasis: emphasis, label: label)
        }
        func steppers(_ params: [EG13PartParam]) -> [EG13ControlSpec] {
            params.compactMap { spec(EG13ParamID.part(shown, $0), .wideStepped) }
        }
        let cardKinds: [EG13SlotKind] = [EG13_SLOT_KIND_SOURCE, EG13_SLOT_KIND_FILTER, EG13_SLOT_KIND_SHAPER,
                                         EG13_SLOT_KIND_PART_FX]
        let modKinds: [EG13SlotKind] = [EG13_SLOT_KIND_MOD1, EG13_SLOT_KIND_MOD2, EG13_SLOT_KIND_MOD3,
                                        EG13_SLOT_KIND_MOD4]
        // Part Level is the bar's main control (accent, in the shown part's colour); Dry and Out are
        // secondary (black).
        let partBar: [EG13ControlSpec] = [
            spec(EG13ParamID.part(shown, .level), .knob(.medium), .accent),
            spec(EG13ParamID.part(shown, .pan), .knob(.medium)),
            spec(EG13ParamID.part(shown, .glide), .knob(.medium)),
            spec(EG13ParamID.part(shown, .voiceMode), .wideStepped),
            spec(EG13ParamID.part(shown, .voiceCount), .wideStepped),
        ].compactMap { $0 }
        return EG13EditorControls(
            part: shown,
            header: [
                spec(EG13ParamID.master(.mode), .segmented),
                spec(EG13ParamID.master(.latencyMode), .stepped, label: "Latency"),
                spec(EG13ParamID.master(.splitNote), .stepped, label: "Split"),
                spec(EG13ParamID.master(.voiceGain), .stepped, label: "Gain"),
                spec(EG13ParamID.master(.softClip), .stepped, label: "Clip"),
            ].compactMap { $0 },
            cards: cardKinds.map { slotControls($0, part: shown) },
            mods: modKinds.map { slotControls($0, part: shown) },
            matrix: (0..<EG13ParamID.routeCount).compactMap {
                spec(EG13ParamID.routeAmount(shown, $0), .knob(.small), .black, label: "Route \($0 + 1)")
            },
            partBar: partBar,
            masterBar: [
                spec(EG13ParamID.master(.dry), .knob(.medium), .black),
                spec(EG13ParamID.master(.level), .knob(.medium), .black, label: "Out"),
            ].compactMap { $0 },
            partSettings: steppers([.transpose, .baseNote, .midiChannel, .keyLow, .keyHigh, .trigger,
                                    .bendRange, .mono, .output, .inputSource, .partFxInput,
                                    .mpeMode, .mpeBendRange, .ccA, .ccB, .ccC, .ccD]))
    }
}
