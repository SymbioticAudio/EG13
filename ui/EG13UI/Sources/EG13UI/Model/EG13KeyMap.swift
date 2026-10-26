/// Which parts a key on the on-screen keyboard reaches, mirroring the synth's routing
/// (spec §5.3): mode, split note and each part's key range.
public struct EG13KeyMap: Equatable, Sendable {
    public var mode: EG13Mode
    public var splitNote: Int
    public var keyRanges: [ClosedRange<Int>]  // per part
    /// Multi mode plays the part shown in the editor.
    public var selectedPart: Int

    public init(mode: EG13Mode, splitNote: Int, keyRanges: [ClosedRange<Int>], selectedPart: Int) {
        self.mode = mode
        self.splitNote = splitNote
        self.keyRanges = keyRanges
        self.selectedPart = selectedPart
    }

    public func parts(forKey key: Int) -> [Int] {
        let candidates: [Int]
        switch mode {
        case .single: candidates = [0]
        case .layer: candidates = [0, 1]
        case .split: candidates = [key < splitNote ? 0 : 1]
        case .multi: candidates = [selectedPart]
        }
        return candidates.filter { $0 < keyRanges.count && keyRanges[$0].contains(key) }
    }

    /// Velocity from where the key was pressed: 0 at the top edge, 1 at the bottom.
    public static func velocity(forVerticalPosition y: Double) -> Double {
        let position = min(max(y.isNaN ? 1 : y, 0), 1)
        return 0.2 + 0.8 * position
    }
}
