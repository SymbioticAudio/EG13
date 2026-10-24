import Observation

/// One host parameter in the editor. Edits are CLAP gestures: beginEdit, any number of
/// setValue calls, endEdit (spec §8). While a gesture is open, host values from the
/// display frame are ignored so the control does not jump; after it ends they are ignored
/// for a few frames until the host has caught up with the last edit.
@MainActor
@Observable
public final class EG13Param {
    /// Display frames to wait for the host to echo an edit before following it again.
    public static let holdFramesAfterEdit = 6

    public let index: Int
    public private(set) var descriptor: EG13ParamDescriptor
    public private(set) var name: String
    public private(set) var value: Double
    public private(set) var text: String
    public private(set) var isEditing = false

    @ObservationIgnored private unowned let core: any EG13Core
    @ObservationIgnored private var holdFrames = 0

    public init(core: any EG13Core, index: Int, descriptor: EG13ParamDescriptor, value: Double) {
        self.core = core
        self.index = index
        self.descriptor = descriptor
        self.name = core.paramName(at: index)
        self.value = value
        self.text = core.paramText(at: index, value: value)
    }

    public var id: UInt32 { descriptor.id }

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

    public func text(for value: Double) -> String { core.paramText(at: index, value: value) }

    public func beginEdit() {
        guard !isEditing else { return }
        isEditing = true
        core.beginEdit(id)
    }

    /// Inside a gesture: sends the edit. Outside one: wraps it in its own gesture.
    public func setValue(_ newValue: Double) {
        let v = constrain(newValue)
        guard v != value else { return }
        perform {
            value = v
            text = core.paramText(at: index, value: v)
            core.edit(id, value: v)
        }
    }

    public func setNormalized(_ normalized: Double) { setValue(value(forNormalized: normalized)) }

    public func endEdit() {
        guard isEditing else { return }
        isEditing = false
        holdFrames = Self.holdFramesAfterEdit
        core.endEdit(id)
    }

    public func resetToDefault() { setValue(descriptor.defaultValue) }

    /// Runs change inside a gesture, opening and closing one if none is open.
    public func perform(_ change: () -> Void) {
        let opened = !isEditing
        if opened { beginEdit() }
        change()
        if opened { endEdit() }
    }

    /// A host value from the display frame.
    func apply(hostValue: Double) {
        if isEditing { return }
        if holdFrames > 0 {
            holdFrames -= 1
            if hostValue != value { return }
            holdFrames = 0
        }
        guard hostValue != value else { return }
        value = hostValue
        text = core.paramText(at: index, value: hostValue)
    }

    /// New name and descriptor (e.g. after an engine switch).
    func reload(descriptor newDescriptor: EG13ParamDescriptor) {
        descriptor = newDescriptor
        name = core.paramName(at: index)
        text = core.paramText(at: index, value: value)
    }
}
