import CoreGraphics
import Testing
@testable import EG13UI

@Suite("Source grouping and the vertical spread")
struct EG13SourceGroupingTests {
    /// Every code the core can name: fixed 1...16, and 4 mod slots of up to 4 outputs.
    private func allSources() -> [(code: UInt16, name: String, item: UInt16)] {
        var list: [(code: UInt16, name: String, item: UInt16)] = []
        for code in UInt16(1)...16 { list.append((code, "Fixed \(code)", code)) }
        let engines = ["Envelope", "LFO", "Random / S&H", "Function Generator"]
        let outputs = [1, 2, 4, 2]
        for slot in 0..<4 {
            for out in 0..<outputs[slot] {
                let code = UInt16(32 + slot * 8 + out)
                let name = outputs[slot] == 1 ? "Mod \(slot + 1) \(engines[slot])"
                                               : "Mod \(slot + 1) \(engines[slot]) Out \(out + 1)"
                list.append((code, name, code))
            }
        }
        return list
    }

    private func flatten(_ nodes: [EG13SourceNode<UInt16>]) -> [UInt16] {
        nodes.flatMap { $0.items + flatten($0.children) }
    }

    @Test("Every source lands in exactly one group, and the groups are in order")
    func everySourceOnce() {
        let sources = allSources()
        let groups = EG13SourceGrouping.groups(sources)
        #expect(groups.map(\.title) == ["Keys", "Controllers", "Mods"])
        #expect(groups[0].items == Array(1...8))
        #expect(groups[1].items == Array(9...16))
        #expect(groups[2].children.map(\.title) == ["Mod 1 Envelope", "Mod 2 LFO", "Mod 3 Random / S&H",
                                                    "Mod 4 Function Generator"])
        #expect(groups[2].children[2].items == [48, 49, 50, 51])
        let flat = flatten(groups)
        #expect(flat.sorted() == sources.map(\.item).sorted(), "none lost, none twice")
        #expect(Set(flat).count == flat.count)
        #expect(flat == sources.map(\.item), "codes ascend: the grouped order is the code order")
    }

    @Test("Empty groups and Off mod slots are left out")
    func emptyGroups() {
        let some = allSources().filter { $0.code >= 9 && $0.code < 40 }
        #expect(EG13SourceGrouping.groups(some).map(\.title) == ["Controllers", "Mods"])
        #expect(EG13SourceGrouping.groups(allSources().filter { $0.code < 9 }).map(\.title) == ["Keys"])
    }

    @Test("Cards spread over the column: equal gaps, never below the minimum")
    func spreadGap() {
        let gap = EG13Layout.gap
        #expect(EG13Layout.spreadGap(heights: [100, 100, 100, 100], columnHeight: 700) == 100)  // (700-400)/3
        #expect(EG13Layout.spreadGap(heights: [100, 100, 100, 100], columnHeight: 424) == gap)  // exactly the minimum
        #expect(EG13Layout.spreadGap(heights: [200, 200, 200, 200], columnHeight: 700) == gap, "too tall: minimum, scrolls")
        #expect(EG13Layout.spreadGap(heights: [100], columnHeight: 700) == 0)
        #expect(EG13Layout.spreadGap(heights: [], columnHeight: 700) == 0)
        // The cards plus gaps fill the column exactly when there is room.
        let h: [CGFloat] = [120, 90, 150, 60]
        let g = EG13Layout.spreadGap(heights: h, columnHeight: EG13Layout.stripHeight)
        #expect(abs(h.reduce(0, +) + 3 * g - EG13Layout.stripHeight) < 0.001)
    }
}
