// swift-tools-version: 6.0
// EG13UI: the SwiftUI editor of the EG13 plugin (spec §8). Built by CMake through
// scripts/build-ui.sh as a static library and linked into every plugin format.
import PackageDescription

// Forks and licence checks build without the all-rights-reserved SymbioticBrand glyphs:
// EG13_NO_BRAND=1 swift build ...  (the standard MIT icons are used instead).
let withBrand = Context.environment["EG13_NO_BRAND"] == nil

// Modules that define Objective-C-visible classes are renamed with an EG13 prefix, so a
// host that loads another plugin built from the same packages never sees two classes with
// one name (spec §2.2). SymbioticTokens and SymbioticBrand define no classes. Sources keep
// importing the original module names; scripts/check-objc-prefix.sh verifies the result.
let iconsAlias = ["SymbioticIcons": "EG13SymbioticIcons"]
let icons: Target.Dependency = withBrand
    ? .product(name: "SymbioticBrand", package: "symbiotic-design", moduleAliases: iconsAlias)
    : .product(name: "SymbioticIcons", package: "symbiotic-design", moduleAliases: iconsAlias)

let package = Package(
    name: "EG13UI",
    platforms: [.macOS(.v14)],
    products: [.library(name: "EG13UI", type: .static, targets: ["EG13UI"])],
    dependencies: [
        .package(url: "https://github.com/SymbioticAudio/symbiotic-design.git", exact: "0.1.0"),
        .package(url: "https://github.com/AudioKit/Keyboard.git", exact: "1.4.1"),
        // Keyboard's own dependency, pinned exactly here.
        .package(url: "https://github.com/AudioKit/Tonic.git", exact: "2.1.2"),
    ],
    targets: [
        // The plugin's C API (core-api/include/eg13/core_api.h), imported through a module map.
        .systemLibrary(name: "EG13CBridge"),
        .target(
            name: "EG13UI",
            dependencies: [
                "EG13CBridge",
                .product(name: "SymbioticTokens", package: "symbiotic-design"),
                icons,
                .product(name: "Keyboard", package: "Keyboard",
                         moduleAliases: ["Keyboard": "EG13Keyboard", "Tonic": "EG13Tonic"]),
            ]),
        .testTarget(name: "EG13UITests", dependencies: ["EG13UI"]),
    ]
)
