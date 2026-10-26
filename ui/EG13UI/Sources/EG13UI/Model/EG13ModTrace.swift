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

    /// Per output, oldest first, at most `capacity` frames.
    public private(set) var frames: [[EG13ModTraceFrame?]] = Array(repeating: [], count: EG13ModTrace.outputCount)

    public init() {}

    public func frames(output: Int) -> [EG13ModTraceFrame?] {
        frames.indices.contains(output) ? frames[output] : []
    }

    /// Adds one display frame. Writes only when the trace changes (a run of gaps on a full trace
    /// changes nothing), so an idle slot never invalidates its card.
    func push(min mins: [Float], max maxs: [Float]) {
        var next = frames
        for output in 0..<Self.outputCount {
            let low = output < mins.count ? mins[output] : .infinity
            let high = output < maxs.count ? maxs[output] : -.infinity
            let frame: EG13ModTraceFrame? = low.isFinite && high.isFinite && low <= high
                ? EG13ModTraceFrame(min: low, max: high) : nil
            next[output].append(frame)
            if next[output].count > Self.capacity { next[output].removeFirst(next[output].count - Self.capacity) }
        }
        if next != frames { frames = next }
    }
}
