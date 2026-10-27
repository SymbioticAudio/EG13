import SwiftUI
import SymbioticTokens

/// The Save As, Rename and Delete dialogs of the preset bar (a small popover each). The state and
/// what Submit does live here so they can be tested; `EG13PresetDialogView` only draws it.
@MainActor
struct EG13PresetDialog: Equatable {
    enum Mode: Equatable {
        case saveAs
        case rename(EG13PresetEntry)
        case delete(EG13PresetEntry)
    }

    var mode: Mode
    var name: String
    /// Save As only: Whole instrument (a Performance), Part A or Part B (a Patch).
    var scope: EG13PresetTarget = .whole
    /// The name is taken: the dialog asks before replacing.
    var clash = false

    var title: String {
        switch mode {
        case .saveAs: "SAVE AS"
        case .rename: "RENAME"
        case .delete: "DELETE"
        }
    }

    /// The text of the primary button.
    var action: String {
        switch mode {
        case .delete: "Delete"
        default: clash ? "Replace" : mode == .saveAs ? "Save" : "Rename"
        }
    }

    /// What the dialog says above its buttons, if anything.
    var prompt: String? {
        switch mode {
        case .delete(let entry): "Delete \u{201C}\(entry.name)\u{201D}? The file is removed."
        default: clash ? "\u{201C}\(trimmedName)\u{201D} already exists. Replace it?" : nil
        }
    }

    var trimmedName: String { name.trimmingCharacters(in: .whitespacesAndNewlines) }
    var hasField: Bool { if case .delete = mode { false } else { true } }
    var canSubmit: Bool { !hasField || !trimmedName.isEmpty }

    /// Runs the action. true: the dialog is done and closes (a refusal shows as the bar's message);
    /// false: a clash, the dialog stays and asks about replacing.
    mutating func submit(_ browser: EG13PresetBrowser) -> Bool {
        let outcome: EG13SaveOutcome
        switch mode {
        case .saveAs: outcome = browser.saveAs(name: name, scope: scope, replace: clash)
        case .rename(let entry): outcome = browser.rename(entry, to: name, replace: clash)
        case .delete(let entry):
            browser.delete(entry)
            return true
        }
        if outcome == .clash {
            clash = true
            return false
        }
        return true
    }

    static func saveAs(_ browser: EG13PresetBrowser, shownPart: Int) -> EG13PresetDialog {
        let current = browser.currentEntry
        // A Patch is saved back as a Patch of the shown part; anything else as the whole instrument.
        let scope: EG13PresetTarget = current?.kind == .patch ? .part(shownPart) : .whole
        return EG13PresetDialog(mode: .saveAs, name: current?.name ?? "", scope: scope)
    }
}

/// The header's preset bar: `◀ [ name ▾ ] ▶` in the LCD look of the Motion button and the engine
/// menus (green on grey, monospaced). The arrows step through the list; the name opens the
/// dropdown. The "*" after the name marks a preset edited since it was loaded. A message shows
/// as a small line under the name for four seconds.
struct EG13PresetBar: View {
    @Bindable var synth: EG13Synth
    @State private var dialog: EG13PresetDialog?
    @State private var shownMessage: String?
    @State private var messageTask: Task<Void, Never>?
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.eg13InteractiveControls) private var interactive
    @Environment(\.symbioticTheme) private var theme

    static let height: CGFloat = 26
    static let messageSeconds = 4.0

    var body: some View {
        let presets = synth.presets
        HStack(spacing: theme.spacing.xs) {
            arrow("\u{25C0}", label: "Previous preset") { presets.step(-1) }
            nameWell(presets)
                .overlay(alignment: .bottomLeading) { messageLine.offset(y: 15) }
            arrow("\u{25B6}", label: "Next preset") { presets.step(1) }
        }
        .frame(minWidth: 150, idealWidth: 220, maxWidth: 320)
        .onChange(of: presets.message) { _, new in show(new) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Presets")
    }

    // MARK: Pieces

    private var lcdFont: Font { .system(size: max(theme.type.caption.size, 11), weight: .bold, design: .monospaced) }

    private func arrow(_ glyph: String, label: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(glyph)
                .font(lcdFont)
                .foregroundStyle(colors.lcdText)
                .frame(width: Self.height - 2, height: Self.height)
                .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(label)
    }

    private func nameWell(_ presets: EG13PresetBrowser) -> some View {
        // The "*" is its own text, so a long name is cut before it and the edited mark always shows.
        let label = HStack(spacing: 0) {
            Text(presets.displayName)
                .lineLimit(1)
                .truncationMode(.tail)
            if presets.isEdited { Text("*") }
            Spacer(minLength: theme.spacing.s)
            Text("\u{25BE}")
        }
        .font(lcdFont)
        .foregroundStyle(colors.lcdText)
        .padding(.horizontal, theme.spacing.s)
        .frame(height: Self.height)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(colors.lcdBackground))
        .contentShape(Rectangle())
        return Group {
            if interactive {
                Menu { menuContent(presets) } label: { label }
                    .menuStyle(.button)
                    .buttonStyle(.plain)
                    .menuIndicator(.hidden)
                    .onHover { if $0 { presets.reloadUserFolder(keepingMessage: true) } }
                    .popover(isPresented: dialogOpen, arrowEdge: .bottom) { dialogContent }
            } else {
                label  // snapshots: ImageRenderer cannot draw a Menu
            }
        }
        .accessibilityLabel("Preset: \(presets.displayName)\(presets.isEdited ? ", edited" : "")")
        .accessibilityHint("Opens the preset list")
    }

    @ViewBuilder private var messageLine: some View {
        if let text = shownMessage {
            Text(text)
                .font(.system(size: max(theme.type.caption.size - 1, 10), weight: .bold, design: .monospaced))
                .foregroundStyle(colors.lcdText)
                .lineLimit(1)
                .truncationMode(.tail)
                .padding(.horizontal, theme.spacing.xs)
                .frame(height: 13)
                .background(RoundedRectangle(cornerRadius: 2).fill(colors.lcdBackground))
                .frame(maxWidth: 360, alignment: .leading)
                .accessibilityLabel(text)
        }
    }

    // MARK: Menu

    @ViewBuilder private func menuContent(_ presets: EG13PresetBrowser) -> some View {
        let menu = presets.menu
        ForEach(menu.groups) { group in
            Section(group.title) {
                if group.rows.isEmpty { Text("None saved yet") }
                ForEach(group.rows) { row in
                    let title = "\(row.ticked ? "\u{2713}" : "\u{2007}") \(row.mark)  \(row.entry.name)"
                    if row.entry.kind == .patch {
                        Menu(title) {
                            Button("Load into A") { presets.load(row.entry, into: .part(0)) }
                            Button("Load into B") { presets.load(row.entry, into: .part(1)) }
                        }
                    } else {
                        Button(title) { presets.load(row.entry) }
                    }
                }
            }
        }
        Divider()
        Button("Init") { presets.loadInit() }
        Divider()
        Button("Save") { presets.save() }.disabled(!menu.canSave)
        Button("Save As\u{2026}") { dialog = .saveAs(presets, shownPart: synth.selectedPart) }
        Button("Rename\u{2026}") {
            if let entry = presets.currentEntry { dialog = EG13PresetDialog(mode: .rename(entry), name: entry.name) }
        }.disabled(!menu.canRename)
        Button("Delete\u{2026}") {
            if let entry = presets.currentEntry { dialog = EG13PresetDialog(mode: .delete(entry), name: entry.name) }
        }.disabled(!menu.canDelete)
    }

    // MARK: Dialog and message

    private var dialogOpen: Binding<Bool> {
        Binding(get: { dialog != nil }, set: { if !$0 { dialog = nil } })
    }

    @ViewBuilder private var dialogContent: some View {
        if dialog != nil {
            EG13PresetDialogView(
                dialog: Binding(get: { dialog ?? EG13PresetDialog(mode: .saveAs, name: "") }, set: { dialog = $0 }),
                onSubmit: {
                    guard var state = dialog else { return }
                    let done = state.submit(synth.presets)
                    dialog = done ? nil : state
                },
                onCancel: { dialog = nil })
        }
    }

    private func show(_ text: String?) {
        guard let text else { return }
        shownMessage = text
        messageTask?.cancel()  // a new message restarts the four seconds
        messageTask = Task { @MainActor in
            try? await Task.sleep(for: .seconds(Self.messageSeconds))
            guard !Task.isCancelled else { return }
            shownMessage = nil
            if synth.presets.message == text { synth.presets.dismissMessage() }
        }
    }
}

/// A dialog's content: a title, a text field (not for Delete), the scope choice (Save As), a
/// question when needed, and two buttons. LCD green on grey, like the menus it opens from.
struct EG13PresetDialogView: View {
    @Binding var dialog: EG13PresetDialog
    var onSubmit: () -> Void = {}
    var onCancel: () -> Void = {}
    @Environment(\.eg13ControlColors) private var colors
    @Environment(\.eg13InteractiveControls) private var interactive
    @Environment(\.symbioticTheme) private var theme
    @FocusState private var focused: Bool

    static let width: CGFloat = 300

    private var font: Font { .system(size: max(theme.type.caption.size, 12), weight: .bold, design: .monospaced) }

    var body: some View {
        VStack(alignment: .leading, spacing: theme.spacing.s) {
            Text(dialog.title)
                .font(.system(size: max(theme.type.caption.size, 11), weight: .bold, design: .monospaced))
                .foregroundStyle(colors.lcdText.opacity(0.7))
            if dialog.hasField { field }
            if case .saveAs = dialog.mode { scopeChoice }
            if let prompt = dialog.prompt {
                Text(prompt)
                    .font(font)
                    .foregroundStyle(colors.lcdText)
                    .fixedSize(horizontal: false, vertical: true)
            }
            HStack(spacing: theme.spacing.s) {
                Spacer(minLength: 0)
                button("Cancel", primary: false, action: onCancel)
                button(dialog.action, primary: true, action: onSubmit).disabled(!dialog.canSubmit)
            }
        }
        .padding(theme.spacing.m)
        .frame(width: Self.width)
        .background(colors.lcdBackground)
        .onAppear { focused = true }
    }

    private var field: some View {
        Group {
            if interactive {
                TextField("Name", text: $dialog.name)
                    .textFieldStyle(.plain)
                    .focused($focused)
                    .onSubmit { if dialog.canSubmit { onSubmit() } }
                    .onChange(of: dialog.name) { _, _ in dialog.clash = false }
            } else {
                Text(dialog.name + "\u{2502}")  // the field and its caret, drawn (ImageRenderer cannot draw a TextField)
            }
        }
        .font(font)
        .foregroundStyle(colors.lcdText)
        .padding(.horizontal, theme.spacing.s)
        .frame(height: 24)
        .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(Color.black.opacity(0.35)))
        .accessibilityLabel("Preset name")
    }

    private var scopeChoice: some View {
        HStack(spacing: theme.spacing.xs) {
            ForEach([(EG13PresetTarget.whole, "Whole instrument"), (.part(0), "Part A"), (.part(1), "Part B")],
                    id: \.1) { scope, title in
                let selected = dialog.scope == scope
                Button { dialog.scope = scope; dialog.clash = false } label: {
                    Text(title)
                        .font(.system(size: max(theme.type.caption.size, 11), weight: .bold, design: .monospaced))
                        .foregroundStyle(selected ? colors.lcdBackground : colors.lcdText)
                        .lineLimit(1)
                        .padding(.horizontal, theme.spacing.s)
                        .frame(height: 22)
                        .background(RoundedRectangle(cornerRadius: theme.radius.s)
                            .fill(selected ? colors.lcdText : Color.clear))
                        .overlay(RoundedRectangle(cornerRadius: theme.radius.s)
                            .stroke(colors.lcdText.opacity(selected ? 0 : 0.6), lineWidth: 1))
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(selected ? [.isSelected, .isButton] : .isButton)
            }
        }
    }

    private func button(_ title: String, primary: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(title)
                .font(font)
                .foregroundStyle(primary ? colors.lcdBackground : colors.lcdText)
                .padding(.horizontal, theme.spacing.m)
                .frame(height: 24)
                .background(RoundedRectangle(cornerRadius: theme.radius.s).fill(primary ? colors.lcdText : Color.clear))
                .overlay(RoundedRectangle(cornerRadius: theme.radius.s)
                    .stroke(colors.lcdText.opacity(primary ? 0 : 0.6), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}

// MARK: Snapshots

/// For the preview's design snapshots (`--snapshot-presets`): ImageRenderer cannot draw a SwiftUI
/// Menu or Popover, so these draw their contents directly.
public enum EG13PresetBarSnapshots {
    /// The dropdown as macOS draws a menu (same rows and enabled states as the real one). A Patch row
    /// named `openPatch` has its "Load into A / B" submenu open beside it.
    @MainActor public static func menu(_ presets: EG13PresetBrowser, openPatch: String? = nil) -> some View {
        EG13PresetMenuReplica(menu: presets.menu, openPatch: openPatch)
    }

    /// Save As, with `clash` the question about replacing a name that is taken.
    @MainActor public static func saveAs(_ presets: EG13PresetBrowser, name: String, scope: EG13PresetTarget,
                                         clash: Bool) -> some View {
        EG13PresetDialogView(dialog: .constant(EG13PresetDialog(mode: .saveAs, name: name, scope: scope, clash: clash)))
            .environment(\.eg13ControlColors, EG13Palette.controls)
    }
}

/// A faithful copy of a macOS menu for the screenshots: 13 pt system text, 22 pt rows, headings in
/// small grey type, a tick column, a disclosure arrow on rows with a submenu.
struct EG13PresetMenuReplica: View {
    let menu: EG13PresetMenu
    var openPatch: String?
    private static let width: CGFloat = 330
    private static let row: CGFloat = 22

    var body: some View {
        HStack(alignment: .top, spacing: 2) {
            panel
            if let open = openPatch, let index = rowIndex(of: open) {
                submenu.offset(y: CGFloat(index) * Self.row)
            }
        }
    }

    /// The vertical position (in rows) of the named Patch row from the top of the panel.
    private func rowIndex(of name: String) -> Int? {
        var line = 0.3  // top padding in rows
        var found: Int?
        for group in menu.groups {
            line += 1  // heading
            for row in group.rows {
                if row.entry.name == name { found = Int(line.rounded()) }
                line += 1
            }
            if group.rows.isEmpty { line += 1 }
        }
        return found
    }

    private var panel: some View {
        VStack(alignment: .leading, spacing: 0) {
            ForEach(menu.groups) { group in
                heading(group.title)
                if group.rows.isEmpty { item("None saved yet", enabled: false) }
                ForEach(group.rows) { row in
                    item("\(row.mark)  \(row.entry.name)", ticked: row.ticked, arrow: row.entry.kind == .patch,
                         highlighted: row.entry.name == openPatch)
                }
            }
            separator
            item("Init")
            separator
            item("Save", enabled: menu.canSave)
            item("Save As\u{2026}")
            item("Rename\u{2026}", enabled: menu.canRename)
            item("Delete\u{2026}", enabled: menu.canDelete)
        }
        .padding(.vertical, 6)
        .frame(width: Self.width, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color(white: 0.96)))
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color.black.opacity(0.18), lineWidth: 0.5))
        .shadow(color: .black.opacity(0.3), radius: 8, y: 3)
    }

    private var submenu: some View {
        VStack(alignment: .leading, spacing: 0) {
            item("Load into A")
            item("Load into B")
        }
        .padding(.vertical, 6)
        .frame(width: 140, alignment: .leading)
        .background(RoundedRectangle(cornerRadius: 8).fill(Color(white: 0.96)))
        .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color.black.opacity(0.18), lineWidth: 0.5))
        .shadow(color: .black.opacity(0.3), radius: 8, y: 3)
    }

    private func heading(_ title: String) -> some View {
        Text(title)
            .font(.system(size: 11, weight: .semibold))
            .foregroundStyle(Color.black.opacity(0.5))
            .padding(.leading, 22)
            .frame(height: Self.row, alignment: .bottom)
            .padding(.bottom, 1)
    }

    private var separator: some View {
        Rectangle().fill(Color.black.opacity(0.15)).frame(height: 1).padding(.vertical, 5).padding(.horizontal, 10)
    }

    private func item(_ title: String, enabled: Bool = true, ticked: Bool = false, arrow: Bool = false,
                      highlighted: Bool = false) -> some View {
        HStack(spacing: 0) {
            Text(ticked ? "\u{2713}" : "").frame(width: 22)
            Text(title).lineLimit(1)
            Spacer(minLength: 0)
            if arrow { Text("\u{203A}").font(.system(size: 15, weight: .semibold)).padding(.trailing, 8) }
        }
        .font(.system(size: 13))
        .foregroundStyle(highlighted ? Color.white : Color.black.opacity(enabled ? 0.9 : 0.3))
        .frame(height: Self.row)
        .background(RoundedRectangle(cornerRadius: 4).fill(highlighted ? Color.accentColor : Color.clear)
            .padding(.horizontal, 5))
    }
}
