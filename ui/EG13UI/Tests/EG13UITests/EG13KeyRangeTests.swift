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
        editor.move(low, toKey: 30)
        #expect(editor.key(low) == 30)
        editor.move(low, toKey: 5)  // past the left end: open, MIDI 0
        #expect(editor.key(low) == 0)
        editor.move(low, toKey: EG13KeyMap.lowestKey)  // on the leftmost key: A0
        #expect(editor.key(low) == EG13KeyMap.lowestKey)
        editor.move(low, toKey: EG13KeyMap.lowestKey - 1)
        #expect(editor.key(low) == 0)
        editor.end(low)
        editor.begin(high)
        editor.move(high, toKey: 10)  // below the strip, and Low is 0: the lowest key of the strip
        #expect(editor.key(high) == EG13KeyMap.lowestKey)
        editor.move(high, toKey: 120)  // past the right end: open, MIDI 127
        #expect(editor.key(high) == 127)
        editor.move(high, toKey: EG13KeyMap.highestKey)  // on the rightmost key: C8
        #expect(editor.key(high) == EG13KeyMap.highestKey)
        editor.move(high, toKey: EG13KeyMap.highestKey + 1)
        #expect(editor.key(high) == 127)
        editor.end(high)
        editor.begin(.split)
        editor.move(.split, toKey: 200)
        #expect(editor.key(.split) == EG13KeyMap.highestKey)
        editor.end(.split)
        #expect(EG13KeyMap.dragged(.low(part: 1), to: 60, low: 30, high: 50) == 50)
        #expect(EG13KeyMap.dragged(.high(part: 1), to: 20, low: 30, high: 50) == 30)
    }

    @Test("dragging past the strip end sets 0 or 127, onto the end key sets A0 or C8")
    func dragToEndOpensTheRange() {
        set(EG13ParamID.part(0, .keyLow), 40)
        set(EG13ParamID.part(0, .keyHigh), 72)
        let width = 360.0, key = width / 88
        let low = EG13KeyMap.Handle.low(part: 0), high = EG13KeyMap.Handle.high(part: 0)
        var lowDrag = EG13KeyRangeDrag(handle: low, startX: EG13KeyMap.x(forKey: 40, width: width), editor: editor)
        lowDrag.update(x: -30, width: width, editor: editor)  // past the left end
        #expect(editor.key(low) == 0, "the left end is MIDI 0, not A0")
        lowDrag.update(x: 0.5 * key, width: width, editor: editor)  // on the leftmost key
        #expect(editor.key(low) == EG13KeyMap.lowestKey, "A0 is reachable by dragging")
        lowDrag.end(editor: editor)
        var highDrag = EG13KeyRangeDrag(handle: high, startX: EG13KeyMap.x(forKey: 72, width: width), editor: editor)
        highDrag.update(x: width + 30, width: width, editor: editor)
        #expect(editor.key(high) == 127, "the right end is MIDI 127, not C8")
        highDrag.update(x: width - 0.6 * key, width: width, editor: editor)  // on the rightmost key
        #expect(editor.key(high) == EG13KeyMap.highestKey, "C8 is reachable by dragging")
        highDrag.end(editor: editor)
        #expect(editor.range(part: 0) == EG13KeyMap.lowestKey...EG13KeyMap.highestKey, "drawn pinned to the strip")
    }

    @Test("a default 0–127 range survives a small drag of the other end")
    func defaultRangeSurvivesSmallDrags() {
        let width = 360.0, key = width / 88
        let low = EG13KeyMap.Handle.low(part: 0), high = EG13KeyMap.Handle.high(part: 0)
        #expect(editor.key(low) == 0 && editor.key(high) == 127)
        // Low: one key right, then back fully left. High is never touched.
        var lowDrag = EG13KeyRangeDrag(handle: low, startX: 0, editor: editor)
        lowDrag.update(x: key, width: width, editor: editor)
        #expect(editor.key(low) == EG13KeyMap.lowestKey + 1)
        #expect(editor.key(high) == 127)
        lowDrag.update(x: -key, width: width, editor: editor)  // back past the end
        lowDrag.end(editor: editor)
        #expect(editor.key(low) == 0)
        // High: a small move left is a key; back past the end is 127 again.
        var highDrag = EG13KeyRangeDrag(handle: high, startX: width, editor: editor)
        highDrag.update(x: width - key, width: width, editor: editor)
        #expect(editor.key(high) == EG13KeyMap.highestKey - 1)
        highDrag.update(x: width + key, width: width, editor: editor)
        highDrag.end(editor: editor)
        #expect(editor.key(high) == 127)
        #expect(editor.key(low) == 0)
        // A press on an end handle that does not move sends nothing.
        core.calls = []
        var still = EG13KeyRangeDrag(handle: low, startX: 0, editor: editor)
        still.update(x: 0.4 * key, width: width, editor: editor)
        still.update(x: -20, width: width, editor: editor)
        still.end(editor: editor)
        #expect(core.calls.isEmpty)
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

    @Test("A press with no key change sends no gesture calls")
    func tapIsNotAnEdit() {
        set(EG13ParamID.part(0, .keyLow), 40)
        core.calls = []
        let low = EG13KeyMap.Handle.low(part: 0)
        let width = 360.0
        let startX = EG13KeyMap.x(forKey: 40, width: width)
        var drag = EG13KeyRangeDrag(handle: low, startX: startX, editor: editor)
        drag.update(x: startX, width: width, editor: editor)
        drag.update(x: startX + 1.5, width: width, editor: editor)  // under half a key
        drag.update(x: startX - 1.5, width: width, editor: editor)
        drag.end(editor: editor)
        #expect(core.calls.isEmpty)
        #expect(editor.key(low) == 40)

        // A real move is one begin, edits by the distance moved, one end.
        var moving = EG13KeyRangeDrag(handle: low, startX: startX, editor: editor)
        moving.update(x: startX + 3 * width / 88, width: width, editor: editor)
        moving.update(x: startX + 5 * width / 88, width: width, editor: editor)
        moving.end(editor: editor)
        moving.end(editor: editor)
        let id = EG13ParamID.part(0, .keyLow)
        #expect(core.calls == [.begin(id), .edit(id, 43), .edit(id, 45), .end(id)])
    }

    @Test("At the split, both handles and the marker are each grabbable")
    func hitTargetsAtTheSplit() {
        let width = 360.0, lane = 12.0, hit = 16.0
        let ranges = [30...59, 60...100]  // A High = split - 1, B Low = split
        func target(_ x: Double, _ y: Double, _ mode: EG13Mode = .split) -> EG13KeyMap.Handle? {
            EG13KeyMap.hitTarget(x: x, y: y, width: width, laneHeight: lane, hitWidth: hit,
                                 mode: mode, ranges: ranges, split: 60)
        }
        let splitX = EG13KeyMap.x(forKey: 60, width: width)
        #expect(target(splitX, 6) == .high(part: 0))
        #expect(target(splitX, 18) == .low(part: 1))
        #expect(target(splitX, 24 + 10) == .split)
        #expect(target(splitX, 24 + 10, .layer) == nil)  // no marker outside Split
        #expect(target(splitX + 60, 34) == nil)
        #expect(target(splitX + 60, 6) == nil)
        #expect(target(splitX, 18, .single) == nil)  // Part B's band is hidden in Single
        #expect(target(EG13KeyMap.x(forKey: 30, width: width), 6) == .low(part: 0))
    }
}
