import SwiftUI
import SymbioticTokens

/// What the key-range graphic edits and how: each drag of a handle is one balanced
/// begin, edit..., end gesture on that handle's parameter. A model so the tests can drive it
/// without a view.
@MainActor
struct EG13KeyRangeEditor {
    let synth: EG13Synth

    func param(_ handle: EG13KeyMap.Handle) -> EG13Param? {
        switch handle {
        case .low(let part): return synth.partParam(part, .keyLow)
        case .high(let part): return synth.partParam(part, .keyHigh)
        case .split: return synth.masterParam(.splitNote)
        }
    }

    func key(_ handle: EG13KeyMap.Handle) -> Int { Int(param(handle)?.value ?? 0) }

    /// Part `part`'s range as shown: ordered, clamped to the strip.
    func range(part: Int) -> ClosedRange<Int> {
        let low = EG13KeyMap.clampedKey(key(.low(part: part)))
        let high = EG13KeyMap.clampedKey(key(.high(part: part)))
        return min(low, high)...max(low, high)
    }

    var mode: EG13Mode { synth.mode }

    /// The keys Part `part` plays (EG13KeyMap.playedRange): in Split, its range on its side of the
    /// split note. Nil when it plays none of the strip.
    func playedRange(part: Int) -> ClosedRange<Int>? {
        EG13KeyMap.playedRange(part: part, low: key(.low(part: part)), high: key(.high(part: part)),
                               mode: mode, split: key(.split))
    }

    /// The value a drag of `handle` to `key` sets: the strip's ends are MIDI 0 and 127, and the
    /// other end's own value (not clamped to the strip) bounds it.
    func target(_ handle: EG13KeyMap.Handle, key: Int) -> Int {
        let part: Int
        switch handle {
        case .low(let p), .high(let p): part = p
        case .split: part = 0
        }
        let low = self.key(.low(part: part)), high = self.key(.high(part: part))
        return EG13KeyMap.dragged(handle, to: key, low: min(low, high), high: max(low, high))
    }

    func begin(_ handle: EG13KeyMap.Handle) {
        guard let param = param(handle), !param.isEditing else { return }
        param.beginEdit()
    }

    func drag(_ handle: EG13KeyMap.Handle, toX x: Double, width: Double) {
        move(handle, toKey: EG13KeyMap.key(forX: x, width: width))
    }

    func move(_ handle: EG13KeyMap.Handle, toKey key: Int) {
        guard let param = param(handle) else { return }
        param.setValue(Double(target(handle, key: key)))
    }

    func end(_ handle: EG13KeyMap.Handle) { param(handle)?.endEdit() }

    /// The handle a press at (x, y) grabs, from the current ranges and mode.
    func hitTarget(x: Double, y: Double, width: Double, laneHeight: Double, hitWidth: Double) -> EG13KeyMap.Handle? {
        let ranges = (0..<EG13ParamID.parts).map { range(part: $0) }
        return EG13KeyMap.hitTarget(x: x, y: y, width: width, laneHeight: laneHeight, hitWidth: hitWidth,
                                    mode: mode, ranges: ranges, split: EG13KeyMap.clampedKey(key(.split)))
    }

    /// VoiceOver's increment and decrement: one semitone of the value itself (0 to 127, as the
    /// stepper; the split note within the strip), as one gesture.
    func step(_ handle: EG13KeyMap.Handle, by delta: Int) {
        guard let param = param(handle) else { return }
        let value: Int
        switch handle {
        case .low(let part):
            value = min(max(key(handle) + delta, EG13KeyMap.lowestNote), key(.high(part: part)))
        case .high(let part):
            value = max(min(key(handle) + delta, EG13KeyMap.highestNote), key(.low(part: part)))
        case .split:
            value = EG13KeyMap.clampedKey(key(handle) + delta)
        }
        begin(handle)
        param.setValue(Double(value))
        end(handle)
    }
}

/// One press-drag-release on a handle. It sends nothing until the pointer has moved by a key and
/// that move changes the value (the parameter's own, which may lie off the strip: 0 to 127), so a
/// plain click is no edit; then it is one begin, edit..., end. The handle follows the pointer by the
/// distance moved from where it is drawn, not to where the pointer is, so it does not jump on grab.
struct EG13KeyRangeDrag {
    let handle: EG13KeyMap.Handle
    private let startX: Double
    /// Where the handle is drawn (on the strip): the drag moves from here.
    private let startKey: Int
    /// The parameter's value at the press, unclamped: a drag that would leave it there is no edit.
    private let startValue: Int
    private(set) var began = false

    @MainActor
    init(handle: EG13KeyMap.Handle, startX: Double, editor: EG13KeyRangeEditor) {
        self.handle = handle
        self.startX = startX
        startValue = editor.key(handle)
        startKey = EG13KeyMap.clampedKey(startValue)
    }

    @MainActor
    mutating func update(x: Double, width: Double, editor: EG13KeyRangeEditor) {
        guard width > 0 else { return }
        let moved = Int(((x - startX) / width * Double(EG13KeyMap.keyCount)).rounded())
        let key = startKey + moved
        if !began {
            guard moved != 0, editor.target(handle, key: key) != startValue else { return }
            began = true
            editor.begin(handle)
        }
        editor.move(handle, toKey: key)
    }

    /// Ends the gesture if one was begun. Safe to call twice.
    @MainActor
    mutating func end(editor: EG13KeyRangeEditor) {
        guard began else { return }
        began = false
        editor.end(handle)
    }
}

/// A non-playing 88-key strip, A0 to C8 (the owner's decision: EG13 has no keyboard to play). Part
/// A's key range is a teal band and Part B's a pink one, bright where the part plays (in Split, its
/// side of the split note) and faint beyond; the split note is a marker, in Split mode only; Part B's band is there only when Part B can sound. Dragging a band end or the marker edits
/// Key Low, Key High or Split Note.
///
/// One gesture serves the whole strip and picks its handle at mouse-down (EG13KeyMap.hitTarget): a
/// part's handles are grabbed in its own lane and the split marker on the keys row, so the marker
/// never covers the handles that sit on the split (Part A's High and Part B's Low).
struct EG13KeyRangeView: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme
    @State private var drag: EG13KeyRangeDrag?
    /// A press that grabbed nothing: its moves are ignored.
    @State private var ignoring = false
    /// Where the current press started. SwiftUI does not call onEnded for a cancelled gesture, so a
    /// press from anywhere else is a new one: the old drag is ended first.
    @State private var pressStart: CGPoint?

    static let laneHeight: CGFloat = 12
    static let keyHeight: CGFloat = 26
    static let handleWidth: CGFloat = 8
    static let hitWidth: CGFloat = 16
    private static let space = "EG13KeyStrip"

    private var editor: EG13KeyRangeEditor { EG13KeyRangeEditor(synth: synth) }

    var body: some View {
        GeometryReader { proxy in
            let width = Double(proxy.size.width)
            let editor = editor
            let mode = editor.mode
            let visibleParts = (0..<EG13ParamID.parts).filter { EG13KeyMap.showsBand(part: $0, mode: mode) }
            ZStack(alignment: .topLeading) {
                keys(width: proxy.size.width)
                    .offset(y: Self.laneHeight * CGFloat(EG13ParamID.parts))
                ForEach(visibleParts, id: \.self) { part in
                    band(part: part, width: width, editor: editor)
                }
                if EG13KeyMap.showsSplitMarker(mode: mode) {
                    splitMarker(width: width, editor: editor)
                }
            }
            .frame(width: proxy.size.width, height: proxy.size.height, alignment: .topLeading)
            .contentShape(Rectangle())
            .coordinateSpace(name: Self.space)
            .gesture(dragGesture(width: width, editor: editor))
        }
        .frame(height: Self.laneHeight * CGFloat(EG13ParamID.parts) + Self.keyHeight)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Key range")
        .onDisappear { finish() }
        .onChange(of: synth.mode) { _, _ in finish() }
    }

    /// Ends an open drag (the popover closed or the mode removed the handle under it).
    private func finish() {
        if var open = drag { open.end(editor: editor) }
        drag = nil
        ignoring = false
        pressStart = nil
    }

    private func keys(width: CGFloat) -> some View {
        let whites = EG13KeyMap.lowestKey...EG13KeyMap.highestKey
        let lineColor = EG13Palette.legend.color.opacity(0.35)
        return Canvas { context, size in
            let w = Double(size.width)
            context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(EG13Palette.white.color))
            for key in whites where EG13KeyMap.isWhite(key) {
                let x = CGFloat(EG13KeyMap.x(forKey: key, width: w))
                var edge = Path()
                edge.move(to: CGPoint(x: x, y: 0))
                edge.addLine(to: CGPoint(x: x, y: size.height))
                context.stroke(edge, with: .color(lineColor), lineWidth: 0.5)
            }
            for key in whites where !EG13KeyMap.isWhite(key) {
                let x0 = CGFloat(EG13KeyMap.x(forKey: key, width: w))
                let x1 = CGFloat(EG13KeyMap.trailingX(forKey: key, width: w))
                let black = CGRect(x: x0, y: 0, width: x1 - x0, height: size.height * 0.62)
                context.fill(Path(black), with: .color(EG13Palette.black.color))
            }
            context.stroke(Path(CGRect(origin: .zero, size: size)), with: .color(lineColor), lineWidth: 0.5)
        }
        .frame(width: width, height: Self.keyHeight)
        .accessibilityHidden(true)
        .allowsHitTesting(false)
    }

    /// The part's Key Low to Key High range is a faint band with the drag handles at its ends; the
    /// keys the part actually plays (in Split, only its side of the split note) are the bright band
    /// and the tint over the keys. Outside Split the two are the same.
    private func band(part: Int, width: Double, editor: EG13KeyRangeEditor) -> some View {
        let range = editor.range(part: part)
        let x0 = CGFloat(EG13KeyMap.x(forKey: range.lowerBound, width: width))
        let x1 = CGFloat(EG13KeyMap.trailingX(forKey: range.upperBound, width: width))
        let played = editor.playedRange(part: part)
        let p0 = played.map { CGFloat(EG13KeyMap.x(forKey: $0.lowerBound, width: width)) } ?? 0
        let p1 = played.map { CGFloat(EG13KeyMap.trailingX(forKey: $0.upperBound, width: width)) } ?? 0
        let colors = EG13Palette.part(part)
        let y = Self.laneHeight * CGFloat(part)
        return ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 2).fill(colors.bright.color.opacity(0.25))
                .frame(width: max(x1 - x0, 1), height: Self.laneHeight - 2)
                .offset(x: x0, y: y + 1)
                .accessibilityHidden(true)
            if played != nil {
                RoundedRectangle(cornerRadius: 2).fill(colors.bright.color.opacity(0.85))
                    .frame(width: max(p1 - p0, 1), height: Self.laneHeight - 2)
                    .offset(x: p0, y: y + 1)
                    .accessibilityHidden(true)
                // The played keys get a tint too, so which keys the part plays reads at once.
                Rectangle().fill(colors.bright.color.opacity(0.22))
                    .frame(width: max(p1 - p0, 1), height: Self.keyHeight)
                    .offset(x: p0, y: Self.laneHeight * CGFloat(EG13ParamID.parts))
                    .accessibilityHidden(true)
            }
            handle(.low(part: part), at: x0, y: y, color: colors.ink.color, editor: editor)
            handle(.high(part: part), at: x1, y: y, color: colors.ink.color, editor: editor)
        }
        .allowsHitTesting(false)
    }

    private func handle(_ handle: EG13KeyMap.Handle, at x: CGFloat, y: CGFloat, color: Color,
                        editor: EG13KeyRangeEditor) -> some View {
        let key = editor.key(handle)  // VoiceOver names the value itself, off the strip too
        return RoundedRectangle(cornerRadius: 2).fill(color)
            .frame(width: Self.handleWidth, height: Self.laneHeight)
            .overlay(RoundedRectangle(cornerRadius: 2).stroke(EG13Palette.white.color, lineWidth: 1))
            .position(x: x, y: y + Self.laneHeight / 2)
            .modifier(EG13HandleAccessibility(handle: handle, key: key, editor: editor))
    }

    private func splitMarker(width: Double, editor: EG13KeyRangeEditor) -> some View {
        let handle = EG13KeyMap.Handle.split
        let key = EG13KeyMap.clampedKey(editor.key(handle))
        // On the boundary: the split note is the first key of the upper part.
        let x = CGFloat(EG13KeyMap.x(forKey: key, width: width))
        let height = Self.laneHeight * CGFloat(EG13ParamID.parts) + Self.keyHeight
        return ZStack {
            Rectangle().fill(EG13Palette.legend.color).frame(width: 2, height: height)
            Rectangle().fill(EG13Palette.white.color).frame(width: 0.5, height: height)
        }
        .frame(width: 2, height: height)
        .position(x: x, y: height / 2)
        .modifier(EG13HandleAccessibility(handle: handle, key: key, editor: editor))
        .allowsHitTesting(false)
    }

    private func dragGesture(width: Double, editor: EG13KeyRangeEditor) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named(Self.space))
            .onChanged { value in
                if let start = pressStart, start != value.startLocation { finish() }  // the last one was cancelled
                pressStart = value.startLocation
                if drag == nil && !ignoring {
                    let start = value.startLocation
                    if let grabbed = editor.hitTarget(x: Double(start.x), y: Double(start.y), width: width,
                                                      laneHeight: Double(Self.laneHeight),
                                                      hitWidth: Double(Self.hitWidth)) {
                        drag = EG13KeyRangeDrag(handle: grabbed, startX: Double(start.x), editor: editor)
                    } else {
                        ignoring = true
                    }
                }
                guard var open = drag else { return }
                open.update(x: Double(value.location.x), width: width, editor: editor)
                drag = open
            }
            .onEnded { _ in finish() }
    }
}

/// VoiceOver for one handle: "Part A lowest key", valued with the note, adjustable by a semitone.
private struct EG13HandleAccessibility: ViewModifier {
    let handle: EG13KeyMap.Handle
    let key: Int
    let editor: EG13KeyRangeEditor

    func body(content: Content) -> some View {
        content
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(EG13KeyMap.accessibilityLabel(handle, key: key))
            .accessibilityValue(EG13KeyMap.noteName(key))
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: editor.step(handle, by: 1)
                case .decrement: editor.step(handle, by: -1)
                @unknown default: break
                }
            }
    }
}
