/// The modulation sources grouped for a menu, from the source codes alone (fixed sources 1...16,
/// mod outputs 32 + 8 per mod slot). Nothing is listed by hand: the names come from the core.
/// Keys (1...8), Controllers (9...16 and any other fixed code), then Mods: one subgroup per mod
/// slot, titled with the slot's name ("Mod 1 Envelope"), holding that slot's outputs.
public struct EG13SourceNode<Item: Equatable & Sendable>: Equatable, Sendable {
    public var title: String
    public var items: [Item]
    public var children: [EG13SourceNode<Item>]
    public init(title: String, items: [Item] = [], children: [EG13SourceNode<Item>] = []) {
        self.title = title
        self.items = items
        self.children = children
    }
}

public enum EG13SourceGrouping {
    public static let keysTitle = "Keys"
    public static let controllersTitle = "Controllers"
    public static let modsTitle = "Mods"
    /// Codes 1...8 are the keys; the rest of the fixed codes are controllers.
    public static let keyCodes: Range<Int> = 1..<9
    public static let firstModCode = 32
    public static let outputsPerMod = 8

    /// The mod slot (0...) of a mod output code.
    public static func modSlot(of code: UInt16) -> Int { (Int(code) - firstModCode) / outputsPerMod }

    /// "Mod 1 LFO Out 2" is "Mod 1 LFO": the slot's name.
    public static func slotName(of sourceName: String) -> String {
        sourceName.range(of: " Out ").map { String(sourceName[..<$0.lowerBound]) } ?? sourceName
    }

    /// Groups in menu order; an empty group is left out. Order inside a group is the order given.
    public static func groups<Item: Equatable & Sendable>(
        _ sources: [(code: UInt16, name: String, item: Item)]
    ) -> [EG13SourceNode<Item>] {
        var keys: [Item] = [], controllers: [Item] = []
        var slots: [(slot: Int, node: EG13SourceNode<Item>)] = []
        for source in sources {
            let code = Int(source.code)
            if code >= firstModCode {
                let slot = modSlot(of: source.code)
                if let at = slots.firstIndex(where: { $0.slot == slot }) {
                    slots[at].node.items.append(source.item)
                } else {
                    slots.append((slot, EG13SourceNode(title: slotName(of: source.name), items: [source.item])))
                }
            } else if keyCodes.contains(code) {
                keys.append(source.item)
            } else {
                controllers.append(source.item)
            }
        }
        var result: [EG13SourceNode<Item>] = []
        if !keys.isEmpty { result.append(EG13SourceNode(title: keysTitle, items: keys)) }
        if !controllers.isEmpty { result.append(EG13SourceNode(title: controllersTitle, items: controllers)) }
        let mods = slots.sorted { $0.slot < $1.slot }.map(\.node)
        if !mods.isEmpty { result.append(EG13SourceNode(title: modsTitle, children: mods)) }
        return result
    }
}
