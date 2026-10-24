#!/usr/bin/env bash
# Builds the EG13UI Swift package (ui/EG13UI) as a static library, one build per architecture,
# and merges the results with lipo into <out-dir>/libEG13UI.a.
#   scripts/build-ui.sh <config> <archs> <out-dir>
# config: Debug, Release or the swift names debug, release. archs: CMAKE_OSX_ARCHITECTURES
# ("arm64;x86_64" or "arm64 x86_64"); empty means the host architecture.
# The static library carries its package dependencies (symbiotic-design), so one file is all
# the plugin links.
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

mkdir -p "$OUT"
TMP="$(mktemp -d "$OUT/lipo.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

PARTS=()
for arch in $ARCHS; do
  swift build --package-path "$PKG" -c "$CONFIG" --arch "$arch"
  bin="$(swift build --package-path "$PKG" -c "$CONFIG" --arch "$arch" --show-bin-path)"
  # SwiftPM reuses one products directory for every --arch, so copy each result out at once.
  cp "$bin/libEG13UI.a" "$TMP/libEG13UI-$arch.a"
  PARTS+=("$TMP/libEG13UI-$arch.a")
done

if [ ${#PARTS[@]} -eq 1 ]; then
  cp "${PARTS[0]}" "$OUT/libEG13UI.a.new"
else
  lipo -create "${PARTS[@]}" -output "$OUT/libEG13UI.a.new"
fi
mv "$OUT/libEG13UI.a.new" "$OUT/libEG13UI.a"
