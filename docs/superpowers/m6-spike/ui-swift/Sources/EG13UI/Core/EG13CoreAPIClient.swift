import EG13CBridge

/// EG13Core over the plugin's C function table. The table outlives the editor.
@MainActor
public final class EG13CoreAPIClient: EG13Core {
    private let api: UnsafePointer<EG13CoreAPI>

    public init(api: UnsafePointer<EG13CoreAPI>) {
        self.api = api
    }

    private var context: UnsafeMutableRawPointer? { api.pointee.context }

    private func string(_ fill: (UnsafeMutablePointer<CChar>, UInt32) -> Bool) -> String {
        var buffer = [CChar](repeating: 0, count: Int(EG13_TEXT_SIZE))
        let ok = buffer.withUnsafeMutableBufferPointer { fill($0.baseAddress!, UInt32($0.count)) }
        guard ok else { return "" }
        return String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }

    public func paramCount() -> Int { Int(api.pointee.param_count(context)) }

    public func paramDescriptor(at index: Int) -> EG13ParamDescriptor? {
        var info = EG13ParamInfo()
        guard api.pointee.param_info(context, UInt32(index), &info) else { return nil }
        return EG13ParamDescriptor(
            id: info.id,
            isStepped: info.flags & UInt32(EG13_PARAM_STEPPED) != 0,
            isHidden: info.flags & UInt32(EG13_PARAM_HIDDEN) != 0,
            minValue: info.min_value, maxValue: info.max_value, defaultValue: info.default_value)
    }

    public func paramName(at index: Int) -> String {
        string { api.pointee.param_name(context, UInt32(index), $0, $1) }
    }

    public func paramText(at index: Int, value: Double) -> String {
        string { api.pointee.param_text(context, UInt32(index), value, $0, $1) }
    }

    public func engineCount(slot: UInt32) -> Int { Int(api.pointee.engine_count(context, slot)) }

    public func engineName(slot: UInt32, engine: Int) -> String {
        string { api.pointee.engine_name(context, slot, UInt32(engine), $0, $1) }
    }

    public func engineID(slot: UInt32, engine: Int) -> String {
        string { api.pointee.engine_id(context, slot, UInt32(engine), $0, $1) }
    }

    public func beginEdit(_ id: UInt32) { api.pointee.begin_edit(context, id) }
    public func edit(_ id: UInt32, value: Double) { api.pointee.edit(context, id, value) }
    public func endEdit(_ id: UInt32) { api.pointee.end_edit(context, id) }

    public func noteOn(channel: Int, key: Int, velocity: Double) {
        api.pointee.note_on(context, Int32(channel), Int32(key), velocity)
    }

    public func noteOff(channel: Int, key: Int) {
        api.pointee.note_off(context, Int32(channel), Int32(key))
    }

    public func displayFrame() -> EG13DisplayFrame {
        var snapshot = EG13Snapshot()
        api.pointee.snapshot(context, &snapshot)
        let count = min(Int(snapshot.param_count), Int(EG13_MAX_PARAMS))
        let values = withUnsafeBytes(of: snapshot.values) { Array($0.bindMemory(to: Double.self).prefix(count)) }
        let peaks = snapshot.part_peak
        return EG13DisplayFrame(values: values,
                                partPeaks: [[peaks.0.0, peaks.0.1], [peaks.1.0, peaks.1.1]])
    }

    public func requestZoom(_ zoom: Double) { api.pointee.request_zoom(context, zoom) }
}
