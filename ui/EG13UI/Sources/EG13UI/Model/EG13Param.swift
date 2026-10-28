import Observation

/// One host parameter as the controls see it, and the observation unit of the editor: a
/// control that reads this parameter's `value` or `text` is invalidated only when this
/// parameter's shown value changes, never by an edit or a snapshot change to another one.
/// The value is the user's during a drag, the held last value of a gesture that ended until
/// the snapshot catches up, else the snapshot's (EG13Synth decides when, this holds the
/// state). Edits are CLAP gestures: beginEdit, any number of setValue calls, endEdit (spec §8).
@MainActor
@Observable
public final class EG13Param {
    public let index: Int
    public let id: UInt32
    /// The value shown. Written only when it changes.
    public private(set) var value: Double
    /// The core's text for `value`, formatted once per change (a body that reads it three
    /// times makes no core call).
    public private(set) var text: String

    /// Whether at least one visible route targets this parameter: the knob's modulation mark. Written
    /// only when it changes, so a knob that reads it ignores every modulation value update.
    public private(set) var isRouted = false
    @ObservationIgnored private unowned let synth: EG13Synth
    @ObservationIgnored private var snapshot: Double
    /// The user's value while the gesture is open.
    @ObservationIgnored private var override: Double?
    /// The last value of a gesture that ended, held until the snapshot shows it or `age`
    /// reaches EG13Synth.pendingTicks.
    @ObservationIgnored var held: (value: Double, age: Int)?

    init(synth: EG13Synth, index: Int, snapshot: Double) {
        self.synth = synth
        self.index = index
        id = synth.descriptorAt(index).id
        self.snapshot = snapshot
        value = snapshot
        text = synth.text(at: index, value: snapshot)
    }

    public var descriptor: EG13ParamDescriptor { synth.descriptorAt(index) }
    public var name: String { synth.nameAt(index) }
    public var isEditing: Bool { synth.isEditing(id) }

    // MARK: State, driven by EG13Synth

    var snapshotValue: Double { snapshot }

    func setSnapshot(_ new: Double) {
        guard new != snapshot else { return }
        snapshot = new
        refresh()
    }

    func setOverride(_ new: Double?) {
        override = new
        refresh()
    }

    var hasOverride: Bool { override != nil }

    /// The gesture ended: its last value is held (age 0) until the snapshot shows it.
    func endGesture() {
        if let last = override { held = (last, 0) }
        override = nil
    }

    func setHeld(_ new: (value: Double, age: Int)?) {
        held = new
        refresh()
    }

    /// Recomputes the shown value; writes (and so notifies) only if it changed.
    private func refresh() {
        let shown = override ?? held?.value ?? snapshot
        guard shown != value else { return }
        value = shown
        text = synth.text(at: index, value: shown)
    }

    func setRouted(_ new: Bool) {
        if new != isRouted { isRouted = new }
    }

    /// The synth this parameter belongs to (the knob's route popover reads its routes).
    var owner: EG13Synth { synth }

    /// The text depends on the engine as well as the value: reformat after a generation change.
    func refreshText() {
        let fresh = synth.text(at: index, value: value)
        if fresh != text { text = fresh }
    }

    /// 0...1 across the parameter's range.
    public var normalized: Double {
        let span = descriptor.maxValue - descriptor.minValue
        return span > 0 ? (value - descriptor.minValue) / span : 0
    }

    public var defaultNormalized: Double {
        let span = descriptor.maxValue - descriptor.minValue
        return span > 0 ? (descriptor.defaultValue - descriptor.minValue) / span : 0
    }

    /// Bipolar parameters (pan, transpose, ±12 st) draw their arc from the centre.
    public var isBipolar: Bool { descriptor.minValue < 0 && descriptor.maxValue > 0 }

    /// Number of discrete values, nil when continuous.
    public var stepCount: Int? {
        descriptor.isStepped ? Int((descriptor.maxValue - descriptor.minValue).rounded()) + 1 : nil
    }

    /// "Part A Level", "Master Dry": the VoiceOver label.
    public var accessibilityLabel: String {
        guard let part = EG13ParamID.partIndex(of: id) else { return "Master \(name)" }
        return "\(EG13PartName.name(part)) \(name)"
    }

    public func value(forNormalized normalized: Double) -> Double {
        let n = min(max(normalized.isNaN ? 0 : normalized, 0), 1)
        let v = descriptor.minValue + n * (descriptor.maxValue - descriptor.minValue)
        return constrain(v)
    }

    public func constrain(_ v: Double) -> Double {
        let clamped = min(max(v.isNaN ? descriptor.minValue : v, descriptor.minValue), descriptor.maxValue)
        return descriptor.isStepped ? clamped.rounded() : clamped
    }

    public func text(for value: Double) -> String { synth.text(at: index, value: value) }

    public func beginEdit() { synth.begin(id) }

    /// Inside a gesture: sends the edit. Outside one: wraps it in its own gesture.
    public func setValue(_ newValue: Double) {
        let v = constrain(newValue)
        guard v != value else { return }
        synth.edit(id, v)
    }

    public func setNormalized(_ normalized: Double) { setValue(value(forNormalized: normalized)) }

    public func endEdit() { synth.end(id) }

    public func resetToDefault() { setValue(descriptor.defaultValue) }
}

extension EG13Param {
    /// The shared control behaviour over this parameter: gestures go to the host as
    /// beginEdit, setValue and endEdit.
    public func makeInteraction() -> EG13ControlInteraction {
        EG13ControlInteraction(
            minValue: descriptor.minValue, maxValue: descriptor.maxValue, defaultValue: descriptor.defaultValue,
            isStepped: descriptor.isStepped,
            current: { [self] in value }, begin: { [self] in beginEdit() },
            edit: { [self] in setValue($0) }, end: { [self] in endEdit() })
    }

    /// The same over a stepper's offered values: the interaction steps through the options'
    /// indices and sends each option's value. Without choices, the plain interaction.
    public func makeInteraction(choices: EG13StepChoices?) -> EG13ControlInteraction {
        guard let choices else { return makeInteraction() }
        return EG13ControlInteraction(
            minValue: 0, maxValue: Double(choices.options.count - 1),
            defaultValue: Double(choices.index(of: descriptor.defaultValue)), isStepped: true,
            current: { [self] in Double(choices.index(of: value)) }, begin: { [self] in beginEdit() },
            edit: { [self] in setValue(choices.options[min(max(Int($0), 0), choices.options.count - 1)].value) },
            end: { [self] in endEdit() })
    }

    /// The text a stepper shows: the option's name when it offers choices, else the core's.
    public func text(choices: EG13StepChoices?) -> String { choices?.label(for: value) ?? text }
}
