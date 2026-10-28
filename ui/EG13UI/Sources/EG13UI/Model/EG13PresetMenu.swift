import Foundation

/// What the header's preset dropdown shows and which entries are enabled (M7 design §4.3). The menu
/// view and the snapshot replica both draw this, so they cannot differ.
struct EG13PresetMenu: Equatable {
    struct Row: Identifiable, Equatable {
        var entry: EG13PresetEntry
        /// "A+B" for a Performance (both parts), "A|B" for a Patch (one part, loaded into A or B).
        var mark: String
        /// The shown part's sound came from this entry.
        var ticked: Bool
        var id: String { entry.id }
        /// The row's help (tooltip): the credit and licence of a credited preset, else nil.
        var help: String? { entry.attribution }
    }

    struct Group: Identifiable, Equatable {
        var title: String
        var rows: [Row]
        var id: String { title }
    }

    /// Bank 1, Bank 2 (or FX), then User; User is always listed, empty until the first save.
    var groups: [Group]
    /// Save acts on one loaded preset (a factory one saves as a User copy).
    var canSave: Bool
    /// Save As is always there: Init and a name per part have nothing else to save.
    var canSaveAs = true
    /// Rename and Delete apply to a User preset only: the one the shown part came from.
    var canRename: Bool
    var canDelete: Bool

    static func mark(for kind: EG13PresetEntry.Kind) -> String { kind == .performance ? "A+B" : "A|B" }
}

extension EG13PresetBrowser {
    var menu: EG13PresetMenu {
        let current = currentEntry
        var groups: [EG13PresetMenu.Group] = []
        for entry in entries {
            let row = EG13PresetMenu.Row(entry: entry, mark: EG13PresetMenu.mark(for: entry.kind),
                                         ticked: entry.id == current?.id)
            if let index = groups.firstIndex(where: { $0.title == entry.bank }) {
                groups[index].rows.append(row)
            } else {
                groups.append(EG13PresetMenu.Group(title: entry.bank, rows: [row]))
            }
        }
        if !groups.contains(where: { $0.title == "User" }) { groups.append(.init(title: "User", rows: [])) }
        let user = current.map { !$0.isFactory } ?? false
        return EG13PresetMenu(groups: groups, canSave: canSave, canRename: user, canDelete: user)
    }
}
