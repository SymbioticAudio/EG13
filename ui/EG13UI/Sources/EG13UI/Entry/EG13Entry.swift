import AppKit
import EG13CBridge
import SwiftUI
import SymbioticIcons
import SymbioticTokens

// The C entry points declared in core-api/include/eg13/ui_entry.h. The plugin calls them
// on the main thread; pointers cross into the main actor as bit patterns.

@inline(__always)
private func onMain<T: Sendable>(_ body: @MainActor () -> T) -> T {
    MainActor.assumeIsolated(body)
}

@MainActor
private func controller(_ bits: UInt) -> EG13UIController {
    Unmanaged<EG13UIController>.fromOpaque(UnsafeRawPointer(bitPattern: bits)!).takeUnretainedValue()
}

@_cdecl("eg13_ui_link_check")
public func eg13UILinkCheck() -> Int32 {
    onMain {
        let view = NSHostingView(rootView: VStack {
            Icon(.slotSource, size: 16)                                            // SymbioticIcons
            Text("EG13").font(EG13Theme.theme.type.body.font)                       // SymbioticTokens
            EG13KnobShape(normalized: 0.5, style: EG13Palette.knobStyle(for: .teal, part: 0),
                          size: .hero)                                               // EG13UI
                .frame(width: 40, height: 40)
        }.frame(width: 200, height: 160))
        view.layoutSubtreeIfNeeded()
        return view.fittingSize.width > 0 ? 1 : 0
    }
}

@_cdecl("eg13_ui_create")
public func eg13UICreate(_ api: UnsafePointer<EG13CoreAPI>?, _ kind: Int32, _ zoom: Double) -> UnsafeMutableRawPointer? {
    guard let api else { return nil }
    let apiBits = UInt(bitPattern: api)
    let bits: UInt = onMain {
        let controller = EG13UIController(api: UnsafePointer(bitPattern: apiBits)!,
                                          kind: EG13Synth.Kind(rawValue: kind) ?? .instrument, zoom: zoom)
        return UInt(bitPattern: Unmanaged.passRetained(controller).toOpaque())
    }
    return UnsafeMutableRawPointer(bitPattern: bits)
}

@_cdecl("eg13_ui_destroy")
public func eg13UIDestroy(_ handle: UnsafeMutableRawPointer?) {
    guard let handle else { return }
    let bits = UInt(bitPattern: handle)
    onMain {
        let unmanaged = Unmanaged<EG13UIController>.fromOpaque(UnsafeRawPointer(bitPattern: bits)!)
        unmanaged.takeUnretainedValue().teardown()
        unmanaged.release()
    }
}

@_cdecl("eg13_ui_attach")
public func eg13UIAttach(_ handle: UnsafeMutableRawPointer?, _ parent: UnsafeMutableRawPointer?) -> Bool {
    guard let handle, let parent else { return false }
    let bits = UInt(bitPattern: handle), parentBits = UInt(bitPattern: parent)
    return onMain {
        let view = Unmanaged<NSView>.fromOpaque(UnsafeRawPointer(bitPattern: parentBits)!).takeUnretainedValue()
        return controller(bits).attach(to: view)
    }
}

@_cdecl("eg13_ui_set_size")
public func eg13UISetSize(_ handle: UnsafeMutableRawPointer?, _ width: UInt32, _ height: UInt32) {
    guard let handle else { return }
    let bits = UInt(bitPattern: handle)
    onMain { controller(bits).setSize(width: width, height: height) }
}

@_cdecl("eg13_ui_set_visible")
public func eg13UISetVisible(_ handle: UnsafeMutableRawPointer?, _ visible: Bool) {
    guard let handle else { return }
    let bits = UInt(bitPattern: handle)
    onMain { controller(bits).setVisible(visible) }
}

@_cdecl("eg13_ui_live_count")
public func eg13UILiveCount() -> Int32 { Int32(EG13LiveObjects.current) }
