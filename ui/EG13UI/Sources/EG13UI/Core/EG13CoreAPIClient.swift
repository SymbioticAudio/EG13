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
            minValue: info.min, maxValue: info.max, defaultValue: info.def)
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

    public func engineParamCount(slot: UInt32, engine: Int) -> Int {
        Int(api.pointee.engine_param_count(context, slot, UInt32(engine)))
    }

    public func currentEngine(slot: UInt32, part: Int) -> Int {
        Int(api.pointee.current_engine(context, slot, UInt32(part)))
    }

    public func beginEdit(_ id: UInt32) { api.pointee.begin_edit(context, id) }
    public func edit(_ id: UInt32, value: Double) { api.pointee.edit(context, id, value) }
    public func endEdit(_ id: UInt32) { api.pointee.end_edit(context, id) }

    public func displayFrame() -> EG13DisplayFrame {
        var snapshot = EG13Snapshot()
        api.pointee.snapshot(context, &snapshot)
        let count = min(Int(snapshot.param_count), Int(EG13_MAX_PARAMS))
        let values = withUnsafeBytes(of: snapshot.values) { Array($0.bindMemory(to: Double.self).prefix(count)) }
        let peaks = snapshot.part_peak
        let voices = snapshot.active_voices
        let parts = Int(EG13_NUM_PARTS), destinations = Int(EG13_NUM_DESTINATIONS)
        let slots = Int(EG13_NUM_MOD_SLOTS), outputs = Int(EG13_MAX_MOD_OUTPUTS)
        let modValues: [[Float]] = withUnsafeBytes(of: snapshot.mod_value) { raw in
            let all = raw.bindMemory(to: Float.self)
            return (0..<parts).map { Array(all[($0 * destinations)..<(($0 + 1) * destinations)]) }
        }
        func outputs3(_ raw: UnsafeRawBufferPointer) -> [[[Float]]] {
            let all = raw.bindMemory(to: Float.self)
            return (0..<parts).map { part in
                (0..<slots).map { slot in
                    let start = (part * slots + slot) * outputs
                    return Array(all[start..<(start + outputs)])
                }
            }
        }
        let outMin = withUnsafeBytes(of: snapshot.mod_out_min, outputs3)
        let outMax = withUnsafeBytes(of: snapshot.mod_out_max, outputs3)
        return EG13DisplayFrame(values: values,
                                partPeaks: [[peaks.0.0, peaks.0.1], [peaks.1.0, peaks.1.1]],
                                activeVoices: [Int(voices.0), Int(voices.1)],
                                engineGeneration: snapshot.engine_generation,
                                modValues: modValues, modOutMin: outMin, modOutMax: outMax)
    }

    public func route(part: Int, index: Int) -> EG13ModRoute {
        var raw = EG13CBridge.EG13Route()
        guard part >= 0, index >= 0,
              api.pointee.route_get(context, UInt32(part), UInt32(index), &raw) else { return EG13ModRoute() }
        return makeRoute(part: part, source: raw.source, destination: raw.destination, via: raw.via,
                         amount: raw.amount)
    }

    public func setRoute(part: Int, index: Int, source: UInt16, destination: UInt16, via: UInt16) -> Bool {
        guard part >= 0, index >= 0 else { return false }
        return api.pointee.route_set(context, UInt32(part), UInt32(index), source, destination, via)
    }

    public func sourceName(part: Int, code: UInt16) -> String? {
        guard part >= 0 else { return nil }
        var found = false
        let text = string { found = api.pointee.source_name(context, UInt32(part), code, $0, $1); return found }
        return found ? text : nil
    }

    public func destinationName(part: Int, code: UInt16) -> String? {
        guard part >= 0 else { return nil }
        var found = false
        let text = string { found = api.pointee.destination_name(context, UInt32(part), code, $0, $1); return found }
        return found ? text : nil
    }

    public func destination(forParam id: UInt32) -> UInt16 { api.pointee.destination_for_param(context, id) }

    public func version() -> String { string { api.pointee.version(context, $0, $1) } }

    public func requestResize(width: UInt32, height: UInt32) {
        api.pointee.request_resize(context, width, height)
    }

    // MARK: Presets

    private func text(_ pointer: UnsafePointer<CChar>?) -> String {
        pointer.map { String(cString: $0) } ?? ""
    }

    public func factoryPresets() -> [EG13PresetEntry] {
        (0..<Int(api.pointee.factory_preset_count(context))).compactMap { index in
            var raw = EG13FactoryPreset()
            guard api.pointee.factory_preset(context, UInt32(index), &raw) else { return nil }
            return EG13PresetEntry(bank: text(raw.bank), name: text(raw.name),
                                   kind: raw.kind == 0 ? .performance : .patch,
                                   note: text(raw.note), credit: text(raw.credit), licence: text(raw.licence),
                                   source: .factory(index), json: text(raw.json))
        }
    }

    private func result(_ call: (UnsafeMutablePointer<CChar>, UInt32) -> Int32) -> EG13PresetApplyResult {
        var buffer = [CChar](repeating: 0, count: Int(EG13_TEXT_SIZE) * 2)
        let status = buffer.withUnsafeMutableBufferPointer { call($0.baseAddress!, UInt32($0.count)) }
        let message = String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
        switch status {
        case Int32(EG13_PRESET_OK): return EG13PresetApplyResult(status: .ok, message: message)
        case Int32(EG13_PRESET_WARNINGS): return EG13PresetApplyResult(status: .warnings, message: message)
        default: return EG13PresetApplyResult(status: .invalid, message: message)
        }
    }

    public func applyPreset(json: String, target: EG13PresetTarget) -> EG13PresetApplyResult {
        result { api.pointee.preset_apply(context, json, target.cPart, $0, $1) }
    }

    public func applyInit() -> EG13PresetApplyResult {
        result { api.pointee.preset_init(context, $0, $1) }
    }

    public func savePreset(part: Int?, name: String) -> String {
        let cPart = Int32(part ?? -1)
        let needed = Int(api.pointee.preset_save(context, cPart, name, nil, 0))
        guard needed > 0 else { return "" }
        var buffer = [CChar](repeating: 0, count: needed)
        let written = buffer.withUnsafeMutableBufferPointer {
            api.pointee.preset_save(context, cPart, name, $0.baseAddress!, UInt32($0.count))
        }
        guard Int(written) == needed else { return "" }
        return String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
    }

    public func browserState() -> String {
        var size = 1024
        while size <= 1 << 22 {
            var buffer = [CChar](repeating: 0, count: size)
            let ok = buffer.withUnsafeMutableBufferPointer {
                api.pointee.browser_state_get(context, $0.baseAddress!, UInt32($0.count))
            }
            if ok { return String(decoding: buffer.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self) }
            size *= 4
        }
        return ""
    }

    public func setBrowserState(_ state: String) { api.pointee.browser_state_set(context, state) }

    public func isSetupParam(_ id: UInt32) -> Bool { api.pointee.param_is_setup(context, id) }
}
