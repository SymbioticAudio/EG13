import EG13CBridge
import Testing
@testable import EG13UI

@MainActor
struct EG13ParamTests {
    let core = EG13FakeCore.standard()

    func param(_ id: UInt32) -> EG13Param {
        let i = core.index(of: id)
        return EG13Param(core: core, index: i, descriptor: core.descriptors[i], value: core.values[i])
    }

    @Test func idsMirrorTheCAPI() {
        #expect(EG13PartParam.level.rawValue == Int(EG13_PART_LEVEL))
        #expect(EG13PartParam.voiceCount.rawValue == Int(EG13_PART_VOICE_COUNT))
        #expect(EG13PartParam.output.rawValue == Int(EG13_PART_OUTPUT))
        #expect(EG13MasterParam.splitNote.rawValue == Int(EG13_MASTER_SPLIT_NOTE))
        #expect(EG13ParamID.master(.level) == 0xFF0000)
        #expect(EG13ParamID.part(1, .level) == 0x10001)
        #expect(EG13ParamID.source(0, 1) == 257)
        #expect(EG13ParamID.partIndex(of: 0x10001) == 1)
        #expect(EG13ParamID.partIndex(of: 0xFF0000) == nil)
    }

    @Test func readsNameTextAndRange() {
        let level = param(EG13ParamID.part(0, .level))
        #expect(level.name == "Level")
        #expect(level.text == "100%")
        #expect(level.normalized == 1)
        #expect(level.accessibilityLabel == "Part A Level")
        #expect(param(EG13ParamID.master(.level)).accessibilityLabel == "Master Level")
        #expect(param(EG13ParamID.part(0, .pan)).isBipolar)
        #expect(param(EG13ParamID.part(0, .voiceCount)).stepCount == 8)
    }

    @Test func aValueOutsideAGestureIsWrappedInOne() {
        let level = param(EG13ParamID.part(0, .level))
        level.setValue(0.5)
        let id = level.id
        #expect(core.calls == [.begin(id), .edit(id, 0.5), .end(id)])
        #expect(level.text == "50%")
        #expect(!level.isEditing)
    }

    @Test func valuesInsideAGestureAreOnlyEdits() {
        let level = param(EG13ParamID.part(0, .level))
        level.beginEdit()
        level.setValue(0.4)
        level.setValue(0.3)
        level.setValue(0.3)  // unchanged: no event
        level.endEdit()
        let id = level.id
        #expect(core.calls == [.begin(id), .edit(id, 0.4), .edit(id, 0.3), .end(id)])
    }

    @Test func steppedValuesRoundAndEveryValueClamps() {
        let voices = param(EG13ParamID.part(0, .voiceCount))
        voices.setValue(3.4)
        #expect(voices.value == 3)
        voices.setValue(42)
        #expect(voices.value == 8)
        voices.setValue(.nan)
        #expect(voices.value == 1)
    }

    @Test func hostValuesAreIgnoredDuringAGestureAndBrieflyAfterIt() {
        let level = param(EG13ParamID.part(0, .level))
        level.beginEdit()
        level.setValue(0.2)
        level.apply(hostValue: 0.9)  // automation while dragging: the knob does not jump
        #expect(level.value == 0.2)
        level.endEdit()
        level.apply(hostValue: 1.0)  // the host has not seen the edit yet
        #expect(level.value == 0.2)
        level.apply(hostValue: 0.2)  // echoed: following the host again
        level.apply(hostValue: 0.6)
        #expect(level.value == 0.6)
        #expect(level.text == "60%")
    }

    @Test func aHostThatNeverEchoesIsFollowedAfterTheHoldFrames() {
        let level = param(EG13ParamID.part(0, .level))
        level.setValue(0.2)
        for _ in 0..<EG13Param.holdFramesAfterEdit { level.apply(hostValue: 0.7) }
        #expect(level.value == 0.2)
        level.apply(hostValue: 0.7)
        #expect(level.value == 0.7)
    }

    @Test func resetToDefaultIsOneGesture() {
        let level = param(EG13ParamID.part(0, .level))
        level.setValue(0.5)
        core.calls.removeAll()
        level.resetToDefault()
        #expect(core.calls == [.begin(level.id), .edit(level.id, 1), .end(level.id)])
    }
}
