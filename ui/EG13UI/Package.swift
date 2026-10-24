// swift-tools-version: 6.0
// EG13UI: the SwiftUI editor of the EG13 plugin (spec §8). Built by CMake through
// scripts/build-ui.sh as a static library and linked into every plugin format.
import PackageDescription

// Forks and licence checks build without the all-rights-reserved SymbioticBrand glyphs:
// EG13_NO_BRAND=1 swift build ...  (the standard MIT icons are used instead).
let withBrand = Context.environment["EG13_NO_BRAND"] == nil

// Modules that define Objective-C-visible classes are renamed with an EG13 prefix, so a
// host that loads another plugin built from the same packages never sees two classes with
// one name (spec §2.2). Sources keep
// importing the original module names; scripts/check-objc-prefix.sh verifies the result.
let aliases = ["SymbioticIcons": "EG13SymbioticIcons", "SymbioticTokens": "EG13SymbioticTokens"]
let icons: Target.Dependency = withBrand
    ? .product(name: "SymbioticBrand", package: "symbiotic-design", moduleAliases: aliases)
    : .product(name: "SymbioticIcons", package: "symbiotic-design", moduleAliases: aliases)

let package = Package(
    name: "EG13UI",
    platforms: [.macOS(.v14)],
    products: [.library(name: "EG13UI", type: .static, targets: ["EG13UI"])],
    dependencies: [
        .package(url: "https://github.com/SymbioticAudio/symbiotic-design.git", exact: "0.1.0"),
    ],
    targets: [
        // The plugin's C API (core-api/include/eg13/core_api.h), imported through a module map.
        .systemLibrary(name: "EG13CBridge"),
        .target(
            name: "EG13UI",
            dependencies: [
                "EG13CBridge",
                .product(name: "SymbioticTokens", package: "symbiotic-design", moduleAliases: aliases),
                icons,
            ]),
        .testTarget(name: "EG13UITests", dependencies: ["EG13UI"]),
    ]
)
