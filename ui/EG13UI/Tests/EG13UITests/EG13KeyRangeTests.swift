import Testing
@testable import EG13UI

/// The key-range graphic: the x/key mapping, the ordered clamped drags, which marks show in
/// which mode, and one gesture per drag.
@MainActor
struct EG13KeyRangeTests {
    let core: EG13FakeCore
    let synth: EG13Synth
    let editor: EG13KeyRangeEditor

    init() {
        core = EG13FakeCore.full()
        var ids: [UInt32] = [EG13ParamID.master(.splitNote)]
        for part in 0..<EG13ParamID.parts { ids += [EG13ParamID.part(part, .keyLow), EG13ParamID.part(part, .keyHigh)] }
        for id in ids {
            let i = core.index(of: id)
            core.descriptors[i] = EG13ParamDescriptor(id: id, isStepped: true, minValue: 0, maxValue: 127, defaultValue: 0)
        }
        core.values[core.index(of: EG13ParamID.part(0, .keyHigh))] = 127
        core.values[core.index(of: EG13ParamID.part(1, .keyHigh))] = 127
        core.values[core.index(of: EG13ParamID.master(.splitNote))] = 60
        core.calls = []
        synth = EG13Synth(core: core, kind: .instrument)
        editor = EG13KeyRangeEditor(synth: synth)
    }

    private func set(_ id: UInt32, _ value: Double) {
        core.values[core.index(of: id)] = value
        core.generation += 1
        synth.tick()
    }

    @Test("x maps to key and back for A0–C8")
    func xMapsToKeyAndBack() {
        #expect(EG13KeyMap.keyCount == 88)
        #expect(EG13KeyMap.noteName(EG13KeyMap.lowestKey) == "A0")
        #expect(EG13KeyMap.noteName(EG13KeyMap.highestKey) == "C8")
        for width in [300.0, 360.0, 1000.0] {
            for key in EG13KeyMap.lowestKey...EG13KeyMap.highestKey {
                let left = EG13KeyMap.x(forKey: key, width: width)
                let right = EG13KeyMap.trailingX(forKey: key, width: width)
                #expect(EG13KeyMap.key(forX: left + 0.001, width: width) == key)
                #expect(EG13KeyMap.key(forX: (left + right) / 2, width: width) == key)
                #expect(EG13KeyMap.key(forX: right - 0.001, width: width) == key)
            }
        }
        #expect(EG13KeyMap.key(forX: -50, width: 360) == EG13KeyMap.lowestKey)
        #expect(EG13KeyMap.key(forX: 9999, width: 360) == EG13KeyMap.highestKey)
        #expect(EG13KeyMap.x(forKey: EG13KeyMap.highestKey + 1, width: 360) == EG13KeyMap.x(forKey: EG13KeyMap.highestKey, width: 360))
    }

    @Test("Key range drags clamp and stay ordered")
    func dragsClampAndStayOrdered() {
        set(EG13ParamID.part(0, .keyLow), 40)
        set(EG13ParamID.part(0, .keyHigh), 72)
        let low = EG13KeyMap.Handle.low(part: 0), high = EG13KeyMap.Handle.high(part: 0)
        editor.begin(low)
        editor.move(low, toKey: 90)  // past High: stops at High
        #expect(editor.key(low) == 72)
        editor.move(low, toKey: 5)  // before the strip: A0
        #expect(editor.key(low) == EG13KeyMap.lowestKey)
        editor.end(low)
        editor.begin(high)
        editor.move(high, toKey: 10)  // below Low: stops at Low
        #expect(editor.key(high) == EG13KeyMap.lowestKey)
        editor.move(high, toKey: 120)  // past the strip: C8
        #expect(editor.key(high) == EG13KeyMap.highestKey)
        editor.end(high)
        editor.begin(.split)
        editor.move(.split, toKey: 200)
        #expect(editor.key(.split) == EG13KeyMap.highestKey)
        editor.end(.split)
        #expect(EG13KeyMap.dragged(.low(part: 1), to: 60, low: 30, high: 50) == 50)
        #expect(EG13KeyMap.dragged(.high(part: 1), to: 20, low: 30, high: 50) == 30)
    }

    @Test("Split marker shows only in Split")
    func marksByMode() {
        for mode in EG13Mode.allCases {
            #expect(EG13KeyMap.showsSplitMarker(mode: mode) == (mode == .split))
            #expect(EG13KeyMap.showsBand(part: 0, mode: mode))
            #expect(EG13KeyMap.showsBand(part: 1, mode: mode) == (mode != .single))
        }
        set(EG13ParamID.master(.mode), Double(EG13Mode.split.rawValue))
        #expect(editor.mode == .split)
    }

    @Test("each drag is one begin…end gesture")
    func oneGesturePerDrag() {
        let low = EG13KeyMap.Handle.low(part: 0)
        let id = EG13ParamID.part(0, .keyLow)
        editor.begin(low)
        editor.drag(low, toX: 50, width: 360)
        editor.drag(low, toX: 120, width: 360)
        editor.drag(low, toX: 120, width: 360)
        editor.end(low)
        let calls = core.calls
        #expect(calls.first == .begin(id))
        #expect(calls.last == .end(id))
        #expect(calls.filter { $0 == .begin(id) }.count == 1)
        #expect(calls.filter { $0 == .end(id) }.count == 1)
        #expect(calls.dropFirst().dropLast().allSatisfy { if case .edit(id, _) = $0 { return true } else { return false } })
        #expect(!synth.isEditing(id))

        core.calls = []
        editor.step(.split, by: 1)  // VoiceOver: also one gesture
        let split = EG13ParamID.master(.splitNote)
        #expect(core.calls == [.begin(split), .edit(split, 61), .end(split)])
    }

    @Test("Handle labels name the part and the note")
    func labels() {
        #expect(EG13KeyMap.accessibilityLabel(.low(part: 0), key: 36) == "Part A lowest key, C2")
        #expect(EG13KeyMap.accessibilityLabel(.high(part: 1), key: 108) == "Part B highest key, C8")
        #expect(EG13KeyMap.accessibilityLabel(.split, key: 60) == "Split note, C4")
    }
}
