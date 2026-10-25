import AppKit
import SwiftUI
import EG13UI
import SymbioticTokens

/// Renders the Controls page to PNGs with ImageRenderer: no window, no screen-recording
/// permission. One file per editor scale, as the plugin scales its window (75-200%).
@MainActor
enum EG13Snapshot {
    static let scales: [(name: String, zoom: CGFloat)] = [("75", 0.75), ("100", 1), ("200", 2)]

    static func run(into directory: String) -> Int32 {
        let demo = EG13ControlsDemo(core: .standard())
        do {
            try FileManager.default.createDirectory(atPath: directory, withIntermediateDirectories: true)
        } catch {
            FileHandle.standardError.write(Data("cannot create \(directory): \(error)\n".utf8))
            return 1
        }
        for (name, zoom) in scales {
            let size = EG13ControlsPage.size
            // The page is laid out at 100% and scaled like the plugin's window, so the
            // PNG is 75, 100 or 200% of the base size and vector-sharp at each.
            let view = EG13ControlsPage(demo: demo)
                .scaleEffect(zoom, anchor: .topLeading)
                .frame(width: size.width * zoom, height: size.height * zoom, alignment: .topLeading)
                .symbioticTheme(EG13Theme.theme)
                .environment(\.eg13InteractiveControls, false)
                .environment(\.colorScheme, .light)
            let renderer = ImageRenderer(content: view)
            renderer.scale = 1
            guard let image = renderer.nsImage, let tiff = image.tiffRepresentation,
                  let rep = NSBitmapImageRep(data: tiff), let png = rep.representation(using: .png, properties: [:])
            else {
                FileHandle.standardError.write(Data("render failed at \(name)%\n".utf8))
                return 1
            }
            let path = "\(directory)/controls-\(name).png"
            do { try png.write(to: URL(fileURLWithPath: path)) } catch {
                FileHandle.standardError.write(Data("cannot write \(path): \(error)\n".utf8))
                return 1
            }
            print("wrote \(path) (\(rep.pixelsWide)x\(rep.pixelsHigh))")
        }
        return 0
    }
}
