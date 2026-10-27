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

    /// The bounds a drag of `handle` is held to, from the other end's current value.
    private func target(_ handle: EG13KeyMap.Handle, key: Int) -> Double {
        let part: Int
        switch handle {
        case .low(let p), .high(let p): part = p
        case .split: part = 0
        }
        let bounds = range(part: part)
        return Double(EG13KeyMap.dragged(handle, to: key, low: bounds.lowerBound, high: bounds.upperBound))
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
        param.setValue(target(handle, key: key))
    }

    func end(_ handle: EG13KeyMap.Handle) { param(handle)?.endEdit() }

    /// VoiceOver's increment and decrement: one semitone, as one gesture.
    func step(_ handle: EG13KeyMap.Handle, by delta: Int) {
        guard param(handle) != nil else { return }
        begin(handle)
        move(handle, toKey: EG13KeyMap.clampedKey(key(handle)) + delta)
        end(handle)
    }
}

/// A non-playing 88-key strip, A0 to C8 (the owner's decision: EG13 has no keyboard to play). Part
/// A's key range is a teal band and Part B's a pink one; the split note is a marker, in Split mode
/// only; Part B's band is there only when Part B can sound. Dragging a band end or the marker edits
/// Key Low, Key High or Split Note.
struct EG13KeyRangeView: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme
    @State private var dragging: EG13KeyMap.Handle?

    static let laneHeight: CGFloat = 12
    static let keyHeight: CGFloat = 26
    static let handleWidth: CGFloat = 8
    static let hitWidth: CGFloat = 16

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
            .coordinateSpace(name: "EG13KeyStrip")
        }
        .frame(height: Self.laneHeight * CGFloat(EG13ParamID.parts) + Self.keyHeight)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Key range")
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
    }

    private func band(part: Int, width: Double, editor: EG13KeyRangeEditor) -> some View {
        let range = editor.range(part: part)
        let x0 = CGFloat(EG13KeyMap.x(forKey: range.lowerBound, width: width))
        let x1 = CGFloat(EG13KeyMap.trailingX(forKey: range.upperBound, width: width))
        let colors = EG13Palette.part(part)
        let y = Self.laneHeight * CGFloat(part)
        return ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 2).fill(colors.bright.color.opacity(0.85))
                .frame(width: max(x1 - x0, 1), height: Self.laneHeight - 2)
                .offset(x: x0, y: y + 1)
                .accessibilityHidden(true)
            // The band spans the keys too, as a tint, so which keys it covers reads at once.
            Rectangle().fill(colors.bright.color.opacity(0.22))
                .frame(width: max(x1 - x0, 1), height: Self.keyHeight)
                .offset(x: x0, y: Self.laneHeight * CGFloat(EG13ParamID.parts))
                .allowsHitTesting(false)
                .accessibilityHidden(true)
            handle(.low(part: part), at: x0, y: y, color: colors.ink.color, width: width, editor: editor)
            handle(.high(part: part), at: x1, y: y, color: colors.ink.color, width: width, editor: editor)
        }
    }

    private func handle(_ handle: EG13KeyMap.Handle, at x: CGFloat, y: CGFloat, color: Color,
                        width: Double, editor: EG13KeyRangeEditor) -> some View {
        let key = EG13KeyMap.clampedKey(editor.key(handle))
        return RoundedRectangle(cornerRadius: 2).fill(color)
            .frame(width: Self.handleWidth, height: Self.laneHeight)
            .overlay(RoundedRectangle(cornerRadius: 2).stroke(EG13Palette.white.color, lineWidth: 1))
            .frame(width: Self.hitWidth, height: Self.laneHeight)
            .contentShape(Rectangle())
            .position(x: x, y: y + Self.laneHeight / 2)
            .gesture(dragGesture(handle, width: width, editor: editor))
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
        .frame(width: Self.hitWidth, height: height)
        .contentShape(Rectangle())
        .position(x: x, y: height / 2)
        .gesture(dragGesture(handle, width: width, editor: editor))
        .modifier(EG13HandleAccessibility(handle: handle, key: key, editor: editor))
    }

    private func dragGesture(_ handle: EG13KeyMap.Handle, width: Double, editor: EG13KeyRangeEditor) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("EG13KeyStrip"))
            .onChanged { value in
                if dragging != handle {
                    dragging = handle
                    editor.begin(handle)
                }
                editor.drag(handle, toX: Double(value.location.x), width: width)
            }
            .onEnded { _ in
                editor.end(handle)
                dragging = nil
            }
    }
}

/// VoiceOver for one handle: "Part A lowest key, C2", adjustable by a semitone.
private struct EG13HandleAccessibility: ViewModifier {
    let handle: EG13KeyMap.Handle
    let key: Int
    let editor: EG13KeyRangeEditor

    func body(content: Content) -> some View {
        content
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(EG13KeyMap.accessibilityLabel(handle, key: key))
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: editor.step(handle, by: 1)
                case .decrement: editor.step(handle, by: -1)
                @unknown default: break
                }
            }
    }
}
