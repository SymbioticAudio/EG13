import EG13CBridge
import Observation

/// The range one mod output covered during one display frame.
public struct EG13ModTraceFrame: Equatable, Sendable {
    public let min: Float
    public let max: Float
}

/// The last 60 display frames (2 s at 30 Hz) of one mod slot's outputs, for the trace a card
/// draws. A frame in which an output produced nothing (the core reports +inf / -inf) is a gap
/// (nil), not a value, so a stopped envelope or an unused output draws as a break.
@MainActor
@Observable
public final class EG13ModTrace {
    public static let capacity = 60
    public static let outputCount = Int(EG13_MAX_MOD_OUTPUTS)

    /// Ring storage: per output, `capacity` slots; `head` is the slot of the oldest frame.
    private var ring: [[EG13ModTraceFrame?]] = Array(
        repeating: Array(repeating: nil, count: EG13ModTrace.capacity), count: EG13ModTrace.outputCount)
    private var head = 0
    private var count = 0
    /// How many stored frames, over all outputs, are values (not gaps).
    private var liveFrames = 0
    /// Bumped whenever the trace changes; reading it (or `frames`, `frame`, `latest`) observes the trace.
    public private(set) var revision = 0

    public init() {}

    /// Per output, oldest first, at most `capacity` frames.
    public var frames: [[EG13ModTraceFrame?]] {
        _ = revision
        return (0..<Self.outputCount).map { frames(output: $0) }
    }

    /// How many frames are stored (at most `capacity`).
    public var frameCount: Int {
        _ = revision
        return count
    }

    /// Frame `index` of an output, 0 the oldest; nil for a gap or an index out of range. Reads in
    /// place: no array is made (the trace view draws with it).
    public func frame(output: Int, at index: Int) -> EG13ModTraceFrame? {
        _ = revision
        guard ring.indices.contains(output), index >= 0, index < count else { return nil }
        return ring[output][(head + index) % Self.capacity]
    }

    /// The newest frame of an output; nil when there is none or it is a gap.
    public func latest(output: Int) -> EG13ModTraceFrame? { frame(output: output, at: count - 1) }

    public func frames(output: Int) -> [EG13ModTraceFrame?] {
        _ = revision
        guard ring.indices.contains(output) else { return [] }
        var out: [EG13ModTraceFrame?] = []
        out.reserveCapacity(count)
        for i in 0..<count { out.append(ring[output][(head + i) % Self.capacity]) }
        return out
    }

    /// Adds one display frame in place (no copies of the storage). Writes nothing when the trace
    /// does not change (a run of gaps on a full trace), so an idle slot never invalidates its card.
    func push(min mins: [Float], max maxs: [Float]) {
        var incoming = [EG13ModTraceFrame?](repeating: nil, count: Self.outputCount)
        var incomingLive = 0
        for output in 0..<Self.outputCount {
            let low = output < mins.count ? mins[output] : .infinity
            let high = output < maxs.count ? maxs[output] : -.infinity
            if low.isFinite && high.isFinite && low <= high {
                incoming[output] = EG13ModTraceFrame(min: low, max: high)
                incomingLive += 1
            }
        }
        let full = count == Self.capacity
        if full && incomingLive == 0 && liveFrames == 0 { return }
        let slot = full ? head : (head + count) % Self.capacity
        for output in 0..<Self.outputCount {
            if full && ring[output][slot] != nil { liveFrames -= 1 }
            ring[output][slot] = incoming[output]
        }
        liveFrames += incomingLive
        if full { head = (head + 1) % Self.capacity } else { count += 1 }
        revision += 1
    }
}
