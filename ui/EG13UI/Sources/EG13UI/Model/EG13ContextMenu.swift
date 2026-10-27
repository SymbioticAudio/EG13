import AppKit

/// The content of a knob's context menu (right-click or ctrl-click): "Modulate from" with the
/// sources, and one "Remove modulation: <source>" per route into the knob. A knob that cannot be
/// modulated has none.
public struct EG13ContextMenuModel: Equatable, Sendable {
    public struct Source: Equatable, Sendable {
        public var code: UInt16
        public var name: String
    }
    public struct Removal: Equatable, Sendable {
        public var route: Int
        public var title: String
    }

    /// "Modulate from", or "Matrix full" (disabled) when the part has no free route.
    public var modulateTitle: String
    public var modulateEnabled: Bool
    public var sources: [Source]
    public var removals: [Removal]

    public static let modulateFrom = "Modulate from"
    public static let matrixFull = "Matrix full"
    public static func removeTitle(source: String) -> String { "Remove modulation: \(source)" }
}

extension EG13Synth {
    /// The context menu of a parameter's knob, or nil when the parameter has no destination.
    public func contextMenu(for id: UInt32) -> EG13ContextMenuModel? {
        guard canModulate(id), let part = EG13ParamID.partIndex(of: id) else { return nil }
        let full = firstFreeRoute(part: part) == nil
        return EG13ContextMenuModel(
            modulateTitle: full ? EG13ContextMenuModel.matrixFull : EG13ContextMenuModel.modulateFrom,
            modulateEnabled: !full,
            sources: sources(part: part).map { .init(code: $0.code, name: $0.name) },
            removals: motionItems(into: id, part: part).map {
                .init(route: $0.route, title: EG13ContextMenuModel.removeTitle(source: Self.sourceText($0.text)))
            })
    }

    /// The source of a Motion row's "Source → Destination" text.
    static func sourceText(_ row: String) -> String {
        row.range(of: " → ").map { String(row[..<$0.lowerBound]) } ?? row
    }
}

/// A menu item's action: an EG13-prefixed target, because the AppKit menu needs an Objective-C one.
final class EG13MenuAction: NSObject {
    let run: () -> Void
    init(_ run: @escaping () -> Void) { self.run = run }
    @objc func fire() { run() }
}

extension EG13ContextMenuModel {
    /// The AppKit menu: items hold their action in `representedObject`, so the menu keeps them alive.
    @MainActor
    func makeMenu(modulate: @escaping (UInt16) -> Void, remove: @escaping (Int) -> Void) -> NSMenu {
        func item(_ title: String, _ run: @escaping () -> Void) -> NSMenuItem {
            let action = EG13MenuAction(run)
            let item = NSMenuItem(title: title, action: #selector(EG13MenuAction.fire), keyEquivalent: "")
            item.target = action
            item.representedObject = action
            return item
        }
        let menu = NSMenu()
        menu.autoenablesItems = false
        let parent = NSMenuItem(title: modulateTitle, action: nil, keyEquivalent: "")
        parent.isEnabled = modulateEnabled
        if modulateEnabled {
            let submenu = NSMenu(title: modulateTitle)
            submenu.autoenablesItems = false
            for source in sources { submenu.addItem(item(source.name) { modulate(source.code) }) }
            parent.submenu = submenu
        }
        menu.addItem(parent)
        if !removals.isEmpty { menu.addItem(.separator()) }
        for removal in removals { menu.addItem(item(removal.title) { remove(removal.route) }) }
        return menu
    }
}
