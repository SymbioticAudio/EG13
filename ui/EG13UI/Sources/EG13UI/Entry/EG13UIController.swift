import AppKit
import EG13CBridge
import os
import QuartzCore
import SwiftUI
import SymbioticIcons
#if canImport(SymbioticBrand)
import SymbioticBrand
#endif

/// How many editor objects (controllers and plugin views) are alive. Tests read it through
/// eg13_ui_live_count to prove create/destroy leaks nothing, a running timer included (a timer
/// keeps its controller alive).
enum EG13LiveObjects {
    private static let count = OSAllocatedUnfairLock(initialState: 0)
    static var current: Int { count.withLock { $0 } }
    static func born() { count.withLock { $0 += 1 } }
    static func died() { count.withLock { $0 -= 1 } }
}

/// The container NSView the plugin embeds in the host's view. Holds the SwiftUI root.
final class EG13PluginView: NSView {
    private let hosting: NSHostingView<EG13ScaledRootView>
    private let synth: EG13Synth

    init(synth: EG13Synth, zoom: CGFloat) {
        self.synth = synth
        hosting = NSHostingView(rootView: EG13ScaledRootView(synth: synth, zoom: zoom))
        let size = NSSize(width: EG13Layout.baseSize.width * zoom, height: EG13Layout.baseSize.height * zoom)
        super.init(frame: NSRect(origin: .zero, size: size))
        hosting.frame = bounds
        hosting.autoresizingMask = [.width, .height]
        // The fixed identity ignores the system appearance (spec §8).
        appearance = EG13Theme.appearance
        hosting.appearance = EG13Theme.appearance
        addSubview(hosting)
        EG13LiveObjects.born()
    }

    deinit { EG13LiveObjects.died() }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { true }

    /// Logical points; the layout scales to fit inside, keeping its aspect ratio.
    func setSize(width: CGFloat, height: CGFloat) {
        let zoom = min(width / EG13Layout.baseSize.width, height / EG13Layout.baseSize.height)
        setFrameSize(NSSize(width: width, height: height))
        hosting.rootView = EG13ScaledRootView(synth: synth, zoom: zoom)
        hosting.frame = bounds
    }

    var hostingView: NSView { hosting }
}

/// One open editor: its model, view and display-rate timer. Main thread only.
@MainActor
final class EG13UIController: NSObject {
    static let framesPerSecond: Double = 30

    let client: EG13CoreAPIClient
    let synth: EG13Synth
    let view: EG13PluginView
    private var timer: Timer?

    init(api: UnsafePointer<EG13CoreAPI>, kind: EG13Synth.Kind, zoom: Double) {
        EG13UIController.registerIconsOnce()
        client = EG13CoreAPIClient(api: api)
        synth = EG13Synth(core: client, kind: kind)
        view = EG13PluginView(synth: synth, zoom: CGFloat(zoom))
        super.init()
        EG13LiveObjects.born()
    }

    deinit { EG13LiveObjects.died() }

    func attach(to parent: NSView) -> Bool {
        view.removeFromSuperview()
        view.setFrameOrigin(.zero)
        parent.addSubview(view)
        // AU hosts (clap-wrapper's AUv2 view) never call show: attaching makes it visible.
        setVisible(true)
        return true
    }

    func setSize(width: UInt32, height: UInt32) {
        view.setSize(width: CGFloat(width), height: CGFloat(height))
    }

    func setVisible(_ visible: Bool) {
        view.isHidden = !visible
        if visible { startTimer() } else { stopTimer() }
    }

    /// Closes every gesture and note, stops the timer and detaches. The controller is
    /// released right after.
    func teardown() {
        stopTimer()
        synth.shutdown()
        view.removeFromSuperview()
    }

    var isTimerRunning: Bool { timer != nil }

    private func startTimer() {
        guard timer == nil else { return }
        // The closure holds the controller weakly, so a forgotten timer cannot keep it alive;
        // teardown invalidates it anyway.
        let timer = Timer(timeInterval: 1 / Self.framesPerSecond, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.synth.tick() }
        }
        RunLoop.main.add(timer, forMode: .common)  // keeps meters moving during drags and menus
        self.timer = timer
    }

    private func stopTimer() {
        timer?.invalidate()
        timer = nil
    }

    private static var iconsRegistered = false

    private static func registerIconsOnce() {
        guard !iconsRegistered else { return }
        iconsRegistered = true
        #if canImport(SymbioticBrand)
        Icons.register(SymbioticBrand.provider)
        #endif
    }
}
