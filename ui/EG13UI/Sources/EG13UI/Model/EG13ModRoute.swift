/// One matrix route as the editor sees it: the patch's source, destination and via codes (0 =
/// none), their names, and the amount (the route's host parameter, -1...1).
///
/// A name is nil when the code is 0 or when the part's current engines hide it (a mod output the
/// mod engine does not have, a P the engine does not use): the route keeps the code, and the
/// editor shows it as hidden rather than guessing a name.
public struct EG13ModRoute: Equatable, Sendable {
    public var source: UInt16
    public var destination: UInt16
    public var via: UInt16
    public var amount: Double
    public var sourceName: String?
    public var destinationName: String?
    public var viaName: String?

    public init(source: UInt16 = 0, destination: UInt16 = 0, via: UInt16 = 0, amount: Double = 0,
                sourceName: String? = nil, destinationName: String? = nil, viaName: String? = nil) {
        self.source = source
        self.destination = destination
        self.via = via
        self.amount = amount
        self.sourceName = sourceName
        self.destinationName = destinationName
        self.viaName = viaName
    }

    /// No source, destination or via: the slot `modulate` fills.
    public var isFree: Bool { source == 0 && destination == 0 && via == 0 }

    /// A source and a destination: the route can modulate.
    public var isActive: Bool { source != 0 && destination != 0 }

    /// A field the part's current engines hide: it has a code but no name.
    public var sourceHidden: Bool { source != 0 && sourceName == nil }
    public var destinationHidden: Bool { destination != 0 && destinationName == nil }
    public var viaHidden: Bool { via != 0 && viaName == nil }
}

extension EG13Core {
    /// A route with its names looked up, for the Core implementations to return.
    public func makeRoute(part: Int, source: UInt16, destination: UInt16, via: UInt16, amount: Double) -> EG13ModRoute {
        EG13ModRoute(source: source, destination: destination, via: via, amount: amount,
                  sourceName: source == 0 ? nil : sourceName(part: part, code: source),
                  destinationName: destination == 0 ? nil : destinationName(part: part, code: destination),
                  viaName: via == 0 ? nil : sourceName(part: part, code: via))
    }
}

/// A source or destination a picker offers.
public struct EG13MatrixCode: Equatable, Sendable {
    public let code: UInt16
    public let name: String
}
