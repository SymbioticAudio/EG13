import Testing
@testable import EG13UI

@MainActor
struct EG13ParamTests {
    let core = EG13FakeCore.standard()
    let synth: EG13Synth

    init() { synth = EG13Synth(core: core, kind: .instrument) }

    func param(_ id: UInt32) -> EG13Param { synth.param(id)! }

    @Test func idsMirrorTheParamTable() {
        #expect(EG13PartParam.level.rawValue == 1)
        #expect(EG13PartParam.voiceCount.rawValue == 10)
        #expect(EG13PartParam.output.rawValue == 14)
        #expect(EG13MasterParam.splitNote.rawValue == 3)
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

    @Test func resetToDefaultIsOneGesture() {
        let level = param(EG13ParamID.part(0, .level))
        level.setValue(0.5)
        core.calls.removeAll()
        level.resetToDefault()
        #expect(core.calls == [.begin(level.id), .edit(level.id, 1), .end(level.id)])
    }
}
