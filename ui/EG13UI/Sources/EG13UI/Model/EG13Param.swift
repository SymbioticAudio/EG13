import Observation

/// One host parameter as the controls see it: a handle on EG13Synth, which holds the
/// value (the user's during a drag, the snapshot's otherwise). Edits are CLAP gestures:
/// beginEdit, any number of setValue calls, endEdit (spec §8).
@MainActor
public final class EG13Param {
    public let index: Int
    public let id: UInt32
    @ObservationIgnored private unowned let synth: EG13Synth

    init(synth: EG13Synth, index: Int) {
        self.synth = synth
        self.index = index
        id = synth.descriptorAt(index).id
    }

    public var descriptor: EG13ParamDescriptor { synth.descriptor(id) ?? synth.descriptorAt(index) }
    public var name: String { synth.name(id) }
    public var value: Double { synth.value(id) }
    public var text: String { synth.text(id) }
    public var isEditing: Bool { synth.isEditing(id) }

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
}
