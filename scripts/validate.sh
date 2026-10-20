#!/usr/bin/env bash
# Runs clap-validator, auval (instrument + FX) and pluginval on a mac build.
# Installs the AU component into ~/Library/Audio/Plug-Ins/Components so auval can find it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build/mac}"
PLUGINS="$BUILD/plugins"

"$ROOT/scripts/fetch-validators.sh" >/dev/null
CLAP_VALIDATOR="$(find "$ROOT/build/tools/clap-validator" -type f -name clap-validator | head -1)"
PLUGINVAL="$(find "$ROOT/build/tools/pluginval" -type f -path '*MacOS/pluginval' | head -1)"

CLAP="$(find "$PLUGINS" -maxdepth 1 -name '*.clap' | head -1)"
COMPONENT="$(find "$PLUGINS" -maxdepth 1 -name '*.component' | head -1)"
VST3="$(find "$PLUGINS" -maxdepth 1 -name '*.vst3' | head -1)"

echo "== clap-validator: $CLAP"
"$CLAP_VALIDATOR" validate "$CLAP" --only-failed

echo "== auval: $COMPONENT"
codesign --force --deep --sign - "$COMPONENT"
DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$DEST"
rm -rf "$DEST/$(basename "$COMPONENT")"
cp -R "$COMPONENT" "$DEST/"
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -a | grep -E "Symb" || true
auval -v aumu EG13 Symb
auval -v aumf EGfx Symb

echo "== pluginval: $VST3"
codesign --force --deep --sign - "$VST3"
"$PLUGINVAL" --strictness-level 5 --validate-in-process --validate "$VST3"

echo "All validators passed."
