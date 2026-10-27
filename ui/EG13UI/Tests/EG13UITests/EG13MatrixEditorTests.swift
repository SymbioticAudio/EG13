import AppKit
import EG13CBridge
import EG13UIFixtures
import Testing
@testable import EG13UI

/// The matrix editor and "Modulate from": the route menus, their short texts, the context menu's
/// content, opening a route from a Motion row, clearing, and the hidden-field rule.
@MainActor
struct EG13MatrixEditorTests {
    static let velocity: UInt16 = 1
    static let modWheel: UInt16 = 9
    static let filterP1: UInt16 = 32
    static let filterP4: UInt16 = 35

    let core: EG13FakeCore
    let synth: EG13Synth

    /// The full fake with the Analog Filter (3 P's) and a Mod 1 Envelope in both parts.
    init() {
        core = EG13FakeCore.fullCatalog()
        for part in 0..<2 {
            core.select(EG13_SLOT_KIND_FILTER, engine: 1, part: part)
            core.select(EG13_SLOT_KIND_MOD1, engine: 1, part: part)
        }
        core.calls = []
        synth = EG13Synth(core: core, kind: .instrument)
    }

    private func reload() {
        core.generation += 1
        synth.tick()
    }

    private func filterP(_ n: Int, part: Int = 0) -> UInt32 { EG13ParamID.slot(part, EG13_SLOT_KIND_FILTER, n) }

    // MARK: Reachability

    @Test("Every route has its source, via and destination menu and its amount control, once")
    func routeMenusAreReachable() {
        for part in 0..<EG13ParamID.parts {
            let menus = synth.routeMenus(part: part)
            #expect(menus.count == 3 * EG13ParamID.routeCount)
            #expect(Set(menus.map(\.id)).count == menus.count, "no menu twice")
            for route in 0..<EG13ParamID.routeCount {
                #expect(menus.filter { $0.route == route }.map(\.field) == [.source, .via, .destination])
            }
            #expect(menus.allSatisfy { $0.part == part })
            let amounts = synth.editorControls(part: part).matrix.map(\.id)
            #expect(amounts == (0..<EG13ParamID.routeCount).map { EG13ParamID.routeAmount(part, $0) })
        }
    }

    @Test("A menu shows None, the choice's short text, or (hidden)")
    func closedTexts() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP1, via: 32)
        core.routes[0][1] = .init(source: Self.velocity, destination: Self.filterP4, via: 48)  // P4 and Mod 3 hidden
        reload()
        let first = synth.routeMenus(part: 0).filter { $0.route == 0 }
        #expect(first.map(\.closedText) == ["MODWHL", "M1 ENV", "FLT FREQ"])
        #expect(first.map(\.fullText) == ["Mod Wheel", "Mod 1 Envelope", "Filter: Frequency"])
        let second = synth.routeMenus(part: 0).filter { $0.route == 1 }
        #expect(second.map(\.closedText) == ["VEL", EG13RouteLabels.hidden, EG13RouteLabels.hidden])
        #expect(second.map(\.isHidden) == [false, true, true])
        let free = synth.routeMenus(part: 0).filter { $0.route == 5 }
        #expect(free.map(\.closedText) == ["None", "None", "None"])
        #expect(free.allSatisfy { $0.selected == 0 && !$0.isHidden })
    }

    @Test("The destination menu groups by card, in card order, and lists only visible P's")
    func destinationGroups() {
        let menu = synth.routeMenu(part: 0, route: 0, field: .destination)
        let order = ["Source", "Filter", "Shaper", "Part FX", "Mod 1", "Mod 2", "Mod 3", "Mod 4", "Voice"]
        let positions = menu.groups.map { order.firstIndex(of: $0.title) ?? -1 }
        #expect(!positions.contains(-1) && positions == positions.sorted(), "groups are in card order")
        let filter = menu.groups.first { $0.title == "Filter" }
        #expect(filter?.choices.count == 3)
        #expect(!menu.groups.flatMap(\.choices).contains { $0.code == Self.filterP4 }, "an unused P is not listed")
        #expect(menu.groups.last?.title == "Voice")
        #expect(menu.groups.last?.choices.map(\.name) == ["Pitch", "Amp", "Pan", "Part Level"])
        #expect(menu.groups.first { $0.title == "Mod 2" } == nil, "Mod 2 is Off: no P's")
        // Engine names follow the engine.
        core.select(EG13_SLOT_KIND_FILTER, engine: 2, part: 0)
        reload()
        let other = synth.routeMenu(part: 0, route: 0, field: .destination).groups.first { $0.title == "Filter" }
        #expect(other?.choices.map(\.name) != filter?.choices.map(\.name))
    }

    @Test("The source menu groups Performance and each Mod, and Mod 2 (Off) has none")
    func sourceGroups() {
        let menu = synth.routeMenu(part: 0, route: 0, field: .source)
        #expect(menu.groups.map(\.title) == ["Performance", "Mod 1"])
        #expect(menu.groups[1].choices.map(\.name) == ["Mod 1 Envelope"])
        #expect(synth.routeMenu(part: 0, route: 0, field: .via).groups == menu.groups)
    }

    @Test("Choosing sets that field only; None clears it")
    func choosing() {
        #expect(synth.chooseRoute(part: 0, route: 4, field: .source, code: Self.modWheel))
        #expect(synth.chooseRoute(part: 0, route: 4, field: .destination, code: Self.filterP1))
        #expect(synth.chooseRoute(part: 0, route: 4, field: .via, code: Self.velocity))
        #expect(core.routes[0][4] == .init(source: Self.modWheel, destination: Self.filterP1, via: Self.velocity))
        #expect(synth.chooseRoute(part: 0, route: 4, field: .via, code: 0))
        #expect(core.routes[0][4] == .init(source: Self.modWheel, destination: Self.filterP1, via: 0))
        #expect(core.routes[1][4] == .init(), "the other part is untouched")
    }

    // MARK: Clear

    @Test("Clearing sets source, destination, via and amount to none, the amount in one gesture")
    func clearing() {
        #expect(synth.modulate(id: filterP(1), from: Self.modWheel))
        core.calls = []
        #expect(synth.routes(part: 0)[0].amount == 0.25)
        synth.clearRoute(part: 0, route: 0)
        let id = EG13ParamID.routeAmount(0, 0)
        #expect(core.routes[0][0] == .init())
        #expect(core.calls == [.begin(id), .edit(id, 0), .end(id)])
        #expect(synth.routes(part: 0)[0].isFree && synth.routes(part: 0)[0].amount == 0)
    }

    // MARK: Hidden fields

    @Test("A hidden field shows (hidden), and editing another field sends 0 for each hidden one")
    func hiddenEditSendsZero() {
        core.routes[0][0] = .init(source: Self.modWheel, destination: Self.filterP4, via: 48)  // destination and via hidden
        reload()
        let menus = synth.routeMenus(part: 0).filter { $0.route == 0 }
        #expect(menus.map(\.fullText) == ["Mod Wheel", "(hidden)", "(hidden)"])
        core.setRouteCalls = []
        #expect(synth.chooseRoute(part: 0, route: 0, field: .source, code: Self.velocity))
        #expect(core.setRouteCalls[0].source == Self.velocity)
        #expect(core.setRouteCalls[0].destination == 0 && core.setRouteCalls[0].via == 0)
        #expect(synth.routeMenu(part: 0, route: 0, field: .destination).closedText == "None")
    }

    // MARK: Context menu

    @Test("The knob menu offers Modulate from with every source, and one Remove per route into the knob")
    func contextMenuContent() throws {
        let menu = try #require(synth.contextMenu(for: filterP(1)))
        #expect(menu.modulateTitle == "Modulate from" && menu.modulateEnabled)
        #expect(menu.sources.map(\.name) == synth.sources(part: 0).map(\.name))
        #expect(menu.removals.isEmpty)
        core.routes[0][3] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        core.routes[0][7] = .init(source: 32, destination: Self.filterP1, via: 0)
        core.routes[0][8] = .init(source: Self.velocity, destination: 33, via: 0)  // another knob
        reload()
        let routed = try #require(synth.contextMenu(for: filterP(1)))
        #expect(routed.removals == [.init(route: 3, title: "Remove modulation: Mod Wheel"),
                                    .init(route: 7, title: "Remove modulation: Mod 1 Envelope")])
        #expect(synth.contextMenu(for: EG13ParamID.part(0, .pan)) == nil, "no destination code: no menu")
        #expect(try #require(synth.contextMenu(for: EG13ParamID.part(0, .level))).modulateEnabled, "Part Level can be modulated")
    }

    @Test("With 12 routes used the Modulate item reads Matrix full and is disabled")
    func contextMenuFull() throws {
        for r in 0..<12 { core.routes[0][r] = .init(source: Self.velocity, destination: 33, via: 0) }
        reload()
        let model = try #require(synth.contextMenu(for: filterP(1)))
        #expect(model.modulateTitle == "Matrix full" && !model.modulateEnabled)
        let menu = model.makeMenu(modulate: { _ in }, remove: { _ in })
        #expect(menu.items[0].title == "Matrix full" && !menu.items[0].isEnabled)
        #expect(menu.items[0].submenu == nil)
    }

    @Test("The AppKit menu runs the actions: a source fills the first free route, a Remove clears its route")
    func contextMenuActions() throws {
        core.routes[0][0] = .init(source: Self.velocity, destination: Self.filterP1, via: 0)
        reload()
        let id = filterP(1)
        let model = try #require(synth.contextMenu(for: id))
        let menu = model.makeMenu(modulate: { synth.modulate(id: id, from: $0) },
                                  remove: { synth.clearRoute(part: 0, route: $0) })
        #expect(menu.items[0].title == "Modulate from" && menu.items[0].isEnabled)
        let submenu = try #require(menu.items[0].submenu)
        #expect(submenu.items.map(\.title) == model.sources.map(\.name))
        let wheel = try #require(submenu.items.first { $0.title == "Mod Wheel" })
        _ = wheel.target?.perform(wheel.action, with: wheel)
        #expect(core.routes[0][1] == .init(source: Self.modWheel, destination: Self.filterP1, via: 0), "route 0 was taken")
        let remove = try #require(menu.items.first { $0.title == "Remove modulation: Velocity" })
        _ = remove.target?.perform(remove.action, with: remove)
        #expect(core.routes[0][0] == .init())
        #expect(core.routes[0][1].source == Self.modWheel)
    }

    // MARK: Opening a route

    @Test("Choosing a Motion row shows the matrix and highlights that route")
    func motionRowOpensRoute() {
        core.routes[0][2] = .init(source: Self.modWheel, destination: Self.filterP1, via: 0)
        core.routes[0][6] = .init(source: Self.velocity, destination: 33, via: 0)
        reload()
        let column = EG13RightColumnState()
        #expect(column.pane == .mods && column.highlightedRoute == nil)
        for item in synth.motionItems(kind: EG13_SLOT_KIND_FILTER, part: 0) {
            column.open(route: item.route)
            #expect(column.pane == .matrix && column.highlightedRoute == item.route)
        }
        #expect(column.highlightedRoute == 6)
        let count = column.openCount
        column.open(route: 6)
        #expect(column.openCount == count + 1, "the same row again scrolls again")
        column.pane = .mods
        column.open(route: 2)
        #expect(column.pane == .matrix && column.highlightedRoute == 2)
        column.clearHighlight()
        #expect(column.highlightedRoute == nil && column.pane == .matrix)
    }

    // MARK: Short texts

    @Test("Short forms of sources and destinations")
    func shortForms() {
        #expect(EG13RouteLabels.source("Release Velocity") == "REL VEL")
        #expect(EG13RouteLabels.source("Mod 2 LFO Out 2") == "M2 LFO 2")
        #expect(EG13RouteLabels.source("Mod 4 Random / S&H Out 3") == "M4 RND 3")
        #expect(EG13RouteLabels.source("Mod 3 Function Generator") == "M3 FUNC")
        #expect(EG13RouteLabels.destination("Filter: Cutoff") == "FLT CUTOFF")
        #expect(EG13RouteLabels.destination("Mod 1: Release Shape") == "M1 REL SHP")
        #expect(EG13RouteLabels.destination("Part Level") == "PART LVL")
    }

    /// The closed text of every source and destination the engines can name fits its well, at the
    /// well's font, with its padding and chevron: nothing is cut or scaled.
    @Test("Every source and destination text fits its LCD well")
    func closedTextsFit() {
        let theme = EG13Theme.theme
        let font = NSFont.monospacedSystemFont(ofSize: max(theme.type.caption.size, 11), weight: .bold)
        func width(_ text: String) -> CGFloat { (text.uppercased() as NSString).size(withAttributes: [.font: font]).width }
        let sourceRoom = EG13RouteMenu.textRoom(width: EG13Layout.routeViaWidth, padding: theme.spacing.xs)
        let destinationRoom = EG13RouteMenu.textRoom(width: EG13Layout.routeDestinationWidth, padding: theme.spacing.xs)
        #expect(EG13Layout.routeSourceWidth >= EG13Layout.routeViaWidth)
        var names = Set(EG13MatrixNames.fixedSources.values)
        for engine in EG13EngineCatalog.mod.map(\.name) {
            for slot in 0..<4 {
                for output in 0..<8 {
                    let code = UInt16(32 + 8 * slot + output)
                    if let name = EG13MatrixNames.sourceName(code: code, modEngines: [String](repeating: engine, count: 4)) {
                        names.insert(name)
                    }
                }
            }
        }
        for name in names {
            let short = EG13RouteLabels.source(name)
            #expect(width(short) <= sourceRoom, "\(name) -> \(short): \(width(short)) > \(sourceRoom)")
        }
        #expect(names.count > 30)
        var destinations = Set(EG13MatrixNames.fixedDestinations.values)
        for code in 16..<144 {
            for kind in [0, 1, 2, 3, 4, 5, 6, 7] {
                for engine in EG13EngineCatalog.engines(kind: kind) {
                    if let name = EG13MatrixNames.destinationName(code: UInt16(code), paramNames: { _ in engine.params }) {
                        destinations.insert(name)
                    }
                }
            }
        }
        for name in destinations {
            let short = EG13RouteLabels.destination(name)
            #expect(width(short) <= destinationRoom, "\(name) -> \(short): \(width(short)) > \(destinationRoom)")
        }
        #expect(destinations.count > 100)
    }

    @Test("Every closed text fits the row: the three wells, the knob, its value and the clear button fit the column")
    func rowFitsColumn() {
        let widths = [EG13Layout.routeNumberWidth, EG13Layout.routeSourceWidth, 18, EG13Layout.routeViaWidth,
                      EG13Layout.routeDestinationWidth, EG13Layout.smallKnobDiameter, EG13Layout.routeValueWidth, 20]
        let spacing = EG13Layout.rowGap * CGFloat(widths.count - 1)
        let total = widths.reduce(0, +) + spacing + 2 * EG13Layout.rowGap
        #expect(total <= EG13Layout.modCardWidth - 2 * EG13Layout.cardPadding, "\(total)")
    }
}
