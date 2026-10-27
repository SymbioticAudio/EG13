import AppKit
import SwiftUI
import SymbioticTokens

public enum EG13SettingsTab: Int, CaseIterable, Sendable {
    case settings, about

    var title: String { self == .settings ? "Settings" : "About" }
}

/// The open or closed state of the Settings overlay, and what it means for the editor behind it:
/// while open, that editor takes no input (mouse, right-click, scroll, keys) and is hidden from
/// VoiceOver, and Escape closes the overlay.
struct EG13SettingsOverlayState: Equatable {
    var tab: EG13SettingsTab?

    var isOpen: Bool { tab != nil }
    /// False while open. Applied as `.allowsHitTesting` and as `eg13InteractiveControls`, because the
    /// knobs are AppKit surfaces that SwiftUI's hit testing does not reliably cover.
    var contentIsInteractive: Bool { !isOpen }
    var contentIsHiddenFromAccessibility: Bool { isOpen }

    mutating func open() { tab = tab ?? .settings }
    mutating func close() { tab = nil }
    /// Escape: closes the overlay if open; returns whether it was handled.
    @discardableResult
    mutating func handleEscape() -> Bool {
        guard isOpen else { return false }
        close()
        return true
    }
}

/// The Settings and About sheet (spec §8, M6b decision 9). It is an in-editor overlay, not a
/// SwiftUI `.sheet`: a sheet needs the host's window, is not scaled with the editor's zoom, and
/// cannot be rendered to an image. The overlay dims the editor, takes the editor's zoom, and a click
/// on the dimmed area (or Close) dismisses it.
struct EG13SettingsPanel: View {
    let synth: EG13Synth
    @Binding var tab: EG13SettingsTab
    let close: () -> Void
    @Environment(\.symbioticTheme) private var theme

    static let width: CGFloat = 520

    var body: some View {
        ZStack {
            // Escape closes the panel, whatever has focus (a hidden cancel button).
            Button("Close settings", action: close)
                .keyboardShortcut(.cancelAction)
                .opacity(0)
                .frame(width: 0, height: 0)
                .accessibilityHidden(true)
            Color.black.opacity(0.45)
                .contentShape(Rectangle())
                .onTapGesture(perform: close)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: theme.spacing.l) {
                HStack {
                    EG13LCDSegments(labels: EG13SettingsTab.allCases.map(\.title), selected: tab.rawValue,
                                    accessibilityPrefix: "Show") { tab = EG13SettingsTab(rawValue: $0) ?? .settings }
                    Spacer(minLength: 0)
                    EG13PanelButton(title: "CLOSE", accessibilityLabel: "Close settings", action: close)
                }
                switch tab {
                case .settings: EG13SettingsPane(synth: synth)
                case .about: EG13AboutPane(synth: synth)
                }
            }
            .foregroundStyle(theme.text.primary.color)
            .padding(theme.spacing.l)
            .frame(width: Self.width, alignment: .topLeading)
            .background(RoundedRectangle(cornerRadius: theme.radius.m).fill(theme.surface.panel.color))
            .shadow(color: .black.opacity(0.35), radius: 16, y: 6)
            .accessibilityElement(children: .contain)
            .accessibilityAddTraits(.isModal)
            .accessibilityLabel("Settings and About")
        }
        .onExitCommand(perform: close)
    }
}

/// The Settings tab: the two app-wide settings, then the current editor's size.
struct EG13SettingsPane: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        let store = synth.settings
        VStack(alignment: .leading, spacing: theme.spacing.l) {
            group("DEFAULT WINDOW SIZE FOR NEW EDITORS",
                  note: "Applies to editors opened from now on. Open editors keep their size.") {
                EG13LCDSegments(labels: EG13Settings.windowPercents.map { "\($0)%" },
                                selected: EG13Settings.windowPercents.firstIndex(of: store.settings.defaultWindowPercent) ?? 1,
                                accessibilityPrefix: "Default window size") { index in
                    store.update { $0.defaultWindowPercent = EG13Settings.windowPercents[index] }
                }
            }
            group("MODULATION DISPLAY",
                  note: "Off hides the wave marks, the MOTION buttons and the traces, and stops their 15 Hz refresh. Sound and the matrix are unchanged.") {
                EG13LCDSegments(labels: ["On", "Off"], selected: store.settings.modulationDisplay ? 0 : 1,
                                accessibilityPrefix: "Modulation display") { index in
                    store.update { $0.modulationDisplay = index == 0 }
                }
            }
            Divider()
            group("WINDOW SIZE", note: "Resizes this editor now. Not saved.") {
                HStack(spacing: theme.spacing.xs) {
                    ForEach(EG13Settings.windowPercents, id: \.self) { percent in
                        EG13PanelButton(title: "\(percent)%", accessibilityLabel: "Resize this editor to \(percent) percent") {
                            synth.requestZoom(Double(percent) / 100)
                        }
                    }
                }
            }
            Text("Settings are saved for every EG13 on this Mac, not in your project.")
                .font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
        }
    }

    private func group<Content: View>(_ title: String, note: String,
                                      @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: theme.spacing.xs) {
            Text(title).font(theme.type.caption.font.weight(.bold))
                .accessibilityAddTraits(.isHeader)
            content()
            Text(note).font(theme.type.caption.font)
                .foregroundStyle(theme.text.secondary.color)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}

/// The About tab: product, version, credit, licence, repository.
struct EG13AboutPane: View {
    let synth: EG13Synth
    @Environment(\.symbioticTheme) private var theme

    static let credit = "DSP based on the open-source Eurorack firmware by Émilie Gillet."
    static let licence = "GPL-3.0"
    static let repository = "github.com/SymbioticAudio/EG13"

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.m) {
            Text(synth.kind == .effect ? "EG13 FX" : "EG13")
                .font(theme.type.title.font.weight(.semibold))
                .accessibilityAddTraits(.isHeader)
            Text("Version \(synth.versionText())")
            Text(Self.credit).fixedSize(horizontal: false, vertical: true)
            Text(Self.licence)
            // Text, not Link: a Link cannot be rendered to an image.
            Text(Self.repository)
                .underline()
                .onTapGesture {
                    if let url = URL(string: "https://" + Self.repository) { NSWorkspace.shared.open(url) }
                }
                .accessibilityAddTraits(.isLink)
        }
        .font(theme.type.body.font)
    }
}

/// A push button in the LCD style (a plain SwiftUI Button cannot be rendered to an image).
struct EG13PanelButton: View {
    let title: String
    let accessibilityLabel: String
    let action: () -> Void
    @Environment(\.symbioticTheme) private var theme

    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 11, weight: .bold, design: .monospaced))
                .foregroundStyle(EG13Palette.lcdSegment.color)
                .padding(.horizontal, theme.spacing.s)
                .frame(height: 24)
                .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(EG13Palette.lcdBackground.color))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(accessibilityLabel)
    }
}
