#!/usr/bin/env bash
# Builds the EG13UI Swift package (ui/EG13UI) as a static library, one build per architecture,
# and merges the results with lipo into <out-dir>/libEG13UI.a.
#   scripts/build-ui.sh <config> <archs> <out-dir>
# config: Debug, Release or the swift names debug, release. archs: CMAKE_OSX_ARCHITECTURES
# ("arm64;x86_64" or "arm64 x86_64"); empty means the host architecture.
# The static library carries its package dependencies (symbiotic-design), so one file is all
# the plugin links.
# With EG13_NO_BRAND set (release builds), the library leaves out the all-rights-reserved
# SymbioticBrand artwork; it then builds in its own SwiftPM directory, so it never shares a cache
# with the branded build, and fails if any SymbioticBrand symbol is left.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$ROOT/ui/EG13UI"

if [ $# -ne 3 ]; then
  echo "usage: build-ui.sh <config> <archs> <out-dir>" >&2
  exit 2
fi
CONFIG="$(echo "$1" | tr '[:upper:]' '[:lower:]')"
ARCHS="${2//;/ }"
OUT="$3"
case "$CONFIG" in debug | release) ;; *) echo "build-ui.sh: unknown config '$1'" >&2; exit 2 ;; esac
[ -n "$ARCHS" ] || ARCHS="$(uname -m)"

# Global constraint: no SF Symbols in the editor (icons come from symbiotic-design or are drawn).
if grep -rnE 'systemName:|systemImage:|NSImage\(systemSymbolName' "$PKG/Sources" --include='*.swift'; then
  echo "build-ui.sh: SF Symbols are not allowed in the editor (see above)" >&2
  exit 1
fi

mkdir -p "$OUT"
TMP="$(mktemp -d "$OUT/lipo.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

SWIFT_ARGS=(--package-path "$PKG" -c "$CONFIG")
if [ -n "${EG13_NO_BRAND+set}" ]; then
  SWIFT_ARGS+=(--scratch-path "$OUT/swiftpm-nobrand")
fi

PARTS=()
for arch in $ARCHS; do
  swift build "${SWIFT_ARGS[@]}" --arch "$arch"
  bin="$(swift build "${SWIFT_ARGS[@]}" --arch "$arch" --show-bin-path)"
  # SwiftPM reuses one products directory for every --arch, so copy each result out at once.
  cp "$bin/libEG13UI.a" "$TMP/libEG13UI-$arch.a"
  # The preview's and the tests' engine tables (EG13UIFixtures) come with a resource bundle; they
  # must never reach the plugin's static library.
  # nm output goes into a variable first: under pipefail, `nm | grep -q` dies of SIGPIPE when grep
  # exits early and the guard silently never fires.
  syms="$(nm "$TMP/libEG13UI-$arch.a")" || { echo "build-ui.sh: nm failed on libEG13UI-$arch.a" >&2; exit 1; }
  # Positive control: the module's own symbols must be found, or the grep below proves nothing.
  if ! grep -qE "EG13UI" <<<"$syms"; then
    echo "build-ui.sh: self-check failed: no EG13UI symbol found in nm output" >&2
    exit 1
  fi
  if grep -qE "EG13UIFixtures|resource_bundle_accessor" <<<"$syms"; then
    echo "build-ui.sh: libEG13UI.a contains EG13UIFixtures or a Swift resource bundle accessor" >&2
    exit 1
  fi
  if [ -n "${EG13_NO_BRAND+set}" ] && grep -qE "SymbioticBrand" <<<"$syms"; then
    echo "build-ui.sh: EG13_NO_BRAND is set but libEG13UI-$arch.a contains SymbioticBrand symbols" >&2
    exit 1
  fi
  # The test-only path overrides (EG13_SETTINGS_PATH, EG13_PRESETS_PATH) are compiled out of release
  # builds. Control: a debug library must still contain them.
  if [ "$CONFIG" = release ] && grep -aqE "EG13_SETTINGS_PATH|EG13_PRESETS_PATH" "$TMP/libEG13UI-$arch.a"; then
    echo "build-ui.sh: a release libEG13UI.a still contains an EG13_*_PATH environment override" >&2
    exit 1
  fi
  PARTS+=("$TMP/libEG13UI-$arch.a")
done

if [ ${#PARTS[@]} -eq 1 ]; then
  cp "${PARTS[0]}" "$OUT/libEG13UI.a.new"
else
  lipo -create "${PARTS[@]}" -output "$OUT/libEG13UI.a.new"
fi
mv "$OUT/libEG13UI.a.new" "$OUT/libEG13UI.a"
