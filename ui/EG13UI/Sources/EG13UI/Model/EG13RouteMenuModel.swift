import EG13CBridge
import Observation

/// Which field of a route a menu edits.
public enum EG13RouteField: String, CaseIterable, Sendable {
    case source, via, destination
}

/// One entry of a route menu: the code, its full name (menu item, tooltip, VoiceOver) and the short
/// text the closed LCD well shows when it is chosen.
public struct EG13RouteChoice: Equatable, Identifiable, Sendable {
    public var code: UInt16
    public var name: String
    public var short: String
    public var id: UInt16 { code }
}

/// A submenu of a route menu: the card a destination belongs to, or the family of a source.
public struct EG13RouteGroup: Equatable, Identifiable, Sendable {
    public var title: String
    public var choices: [EG13RouteChoice]
    /// Nested submenus (the Mods group holds one per mod slot).
    public var subgroups: [EG13RouteGroup] = []
    public var id: String { title }
}

/// The menu of one field of one route, as the editor draws and the tests check it.
public struct EG13RouteMenuModel: Equatable, Identifiable, Sendable {
    public var part: Int
    public var route: Int
    public var field: EG13RouteField
    /// The route's code in this field (0 = none).
    public var selected: UInt16
    /// The full name of the choice ("None" for 0, "(hidden)" when the part's engines hide it).
    public var fullText: String
    /// The text of the closed well.
    public var closedText: String
    public var isHidden: Bool
    /// "None" is always the first item and is not in the groups.
    public var groups: [EG13RouteGroup]
    public var id: String { "\(part).\(route).\(field.rawValue)" }
}

/// What a closed route well reads, and the short forms of source and destination names. One place,
/// like EG13ParamLabels; EG13MatrixEditorTests measures every name the engines can produce against
/// the well widths.
public enum EG13RouteLabels {
    public static let none = "None"
    public static let hidden = "(hidden)"

    static let fixedSources: [String: String] = [
        "Velocity": "VEL", "Release Velocity": "REL VEL", "Note": "NOTE", "Gate": "GATE", "Random": "RANDOM",
        "Pressure": "PRESS", "Slide": "SLIDE", "Note Bend": "N.BEND", "Mod Wheel": "MODWHL",
        "Pitch Bend": "P.BEND", "Channel Pressure": "CH PRS", "CC A": "CC A", "CC B": "CC B", "CC C": "CC C",
        "CC D": "CC D", "Transport Phase": "T.PHASE",
    ]
    static let modEngines: [String: String] = [
        "Envelope": "ENV", "LFO": "LFO", "Random / S&H": "RND", "Function Generator": "FUNC",
    ]
    static let cardPrefixes: [String: String] = [
        "Source": "SRC", "Filter": "FLT", "Shaper": "SHP", "Part FX": "FX",
        "Mod 1": "M1", "Mod 2": "M2", "Mod 3": "M3", "Mod 4": "M4",
    ]
    static let fixedDestinations: [String: String] = [
        "Pitch": "PITCH", "Amp": "AMP", "Pan": "PAN", "Part Level": "PART LVL",
    ]

    /// "Mod 2 LFO Out 2" is "M2 LFO 2"; "Release Velocity" is "REL VEL".
    public static func source(_ name: String) -> String {
        if let short = fixedSources[name] { return short }
        guard name.hasPrefix("Mod "), name.count > 6 else { return name.uppercased() }
        let number = name[name.index(name.startIndex, offsetBy: 4)]
        var rest = String(name.dropFirst(6))
        var output = ""
        if let range = rest.range(of: " Out ") {
            output = " " + rest[range.upperBound...]
            rest = String(rest[..<range.lowerBound])
        }
        return "M\(number) " + (modEngines[rest] ?? rest.uppercased()) + output
    }

    /// "Filter: Cutoff" is "FLT CUTOFF"; "Mod 1: Release Shape" is "M1 REL SHP".
    public static func destination(_ name: String) -> String {
        if let short = fixedDestinations[name] { return short }
        guard let range = name.range(of: ": ") else { return name.uppercased() }
        let card = String(name[..<range.lowerBound]), p = String(name[range.upperBound...])
        return (cardPrefixes[card] ?? card.uppercased()) + " " + EG13ParamLabels.short(p)
    }
}

extension EG13Synth {
    /// The three menus of every route of a part, in route order (source, via, destination).
    public func routeMenus(part: Int) -> [EG13RouteMenuModel] {
        (0..<EG13ParamID.routeCount).flatMap { index in
            EG13RouteField.allCases.map { routeMenu(part: part, route: index, field: $0) }
        }
    }

    /// One field's menu: its choices grouped, and the route's current choice.
    public func routeMenu(part: Int, route index: Int, field: EG13RouteField) -> EG13RouteMenuModel {
        let routes = routes(part: part)
        let route = routes.indices.contains(index) ? routes[index] : EG13ModRoute()
        let code: UInt16, name: String?
        switch field {
        case .source: (code, name) = (route.source, route.sourceName)
        case .via: (code, name) = (route.via, route.viaName)
        case .destination: (code, name) = (route.destination, route.destinationName)
        }
        let isDestination = field == .destination
        let hidden = code != 0 && name == nil
        let full = code == 0 ? EG13RouteLabels.none : (name ?? EG13RouteLabels.hidden)
        let closed: String
        if code == 0 { closed = EG13RouteLabels.none } else if let name {
            closed = isDestination ? EG13RouteLabels.destination(name) : EG13RouteLabels.source(name)
        } else {
            closed = EG13RouteLabels.hidden
        }
        return EG13RouteMenuModel(part: part, route: index, field: field, selected: code, fullText: full,
                                  closedText: closed, isHidden: hidden,
                                  groups: isDestination ? destinationGroups(part: part) : sourceGroups(part: part))
    }

    /// Sources by family (EG13SourceGrouping): Keys, Controllers, Mods > one submenu per mod slot.
    func sourceGroups(part: Int) -> [EG13RouteGroup] {
        let list = sources(part: part).map {
            (code: $0.code, name: $0.name,
             item: EG13RouteChoice(code: $0.code, name: $0.name, short: EG13RouteLabels.source($0.name)))
        }
        func convert(_ node: EG13SourceNode<EG13RouteChoice>) -> EG13RouteGroup {
            EG13RouteGroup(title: node.title, choices: node.items, subgroups: node.children.map(convert))
        }
        return EG13SourceGrouping.groups(list).map(convert)
    }

    /// Destinations by card, in the card order: Source, Filter, Shaper, Part FX, Mod 1...4, Voice.
    /// Only the P's the engines use. An item's name is the P's name; the group says which card.
    func destinationGroups(part: Int) -> [EG13RouteGroup] {
        let all = destinations(part: part)
        let order: [(String, Range<Int>)] = [
            ("Source", 16..<32), ("Filter", 32..<48), ("Shaper", 48..<64), ("Part FX", 64..<80),
            ("Mod 1", 80..<96), ("Mod 2", 96..<112), ("Mod 3", 112..<128), ("Mod 4", 128..<144),
            ("Voice", 1..<16),
        ]
        return order.compactMap { title, range in
            let choices = all.filter { range.contains(Int($0.code)) }.map {
                EG13RouteChoice(code: $0.code, name: EG13Motion.destinationText(name: $0.name),
                                short: EG13RouteLabels.destination($0.name))
            }
            return choices.isEmpty ? nil : EG13RouteGroup(title: title, choices: choices)
        }
    }

    /// Sets one field of a route from its menu; a hidden field of the same route is sent as 0.
    @discardableResult
    public func chooseRoute(part: Int, route: Int, field: EG13RouteField, code: UInt16) -> Bool {
        switch field {
        case .source: return setRoute(part: part, index: route, source: code)
        case .via: return setRoute(part: part, index: route, via: code)
        case .destination: return setRoute(part: part, index: route, destination: code)
        }
    }

    /// Clears a route: source, destination and via None, amount 0 (through a begin/edit/end gesture).
    public func clearRoute(part: Int, route: Int) { removeRoute(part: part, index: route) }
}

/// What the right-hand column shows, and the route the matrix scrolls to and highlights after a
/// Motion row or the knob popover chooses it.
@MainActor @Observable
public final class EG13RightColumnState {
    public enum Pane: Int, CaseIterable, Sendable { case mods, matrix
        public var title: String { self == .mods ? "Mods" : "Matrix" }
    }
    public var pane: Pane = .mods
    public private(set) var highlightedRoute: Int?
    /// Bumps on every `open`, so choosing the same route twice scrolls again.
    public private(set) var openCount = 0

    public init() {}

    /// A Motion row was chosen: show the matrix and highlight that route's row.
    public func open(route: Int) {
        pane = .matrix
        highlightedRoute = route
        openCount += 1
    }

    public func clearHighlight() { highlightedRoute = nil }
}
