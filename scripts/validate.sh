#!/usr/bin/env bash
# Runs clap-validator, auval (instrument + FX) and pluginval on a mac build.
# Installs the AU component into ~/Library/Audio/Plug-Ins/Components so auval can find it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build/mac}"
PLUGINS="$BUILD/plugins"

"$ROOT/scripts/fetch-validators.sh" >/dev/null
CLAP_VALIDATOR="$(find "$ROOT/build/tools/clap-validator" -type f -name clap-validator 2>/dev/null | head -1 || true)"
PLUGINVAL="$(find "$ROOT/build/tools/pluginval" -type f -path '*MacOS/pluginval' 2>/dev/null | head -1 || true)"

CLAP="$(find "$PLUGINS" -maxdepth 1 -name '*.clap' 2>/dev/null | head -1 || true)"
COMPONENT="$(find "$PLUGINS" -maxdepth 1 -name '*.component' 2>/dev/null | head -1 || true)"
VST3="$(find "$PLUGINS" -maxdepth 1 -name '*.vst3' 2>/dev/null | head -1 || true)"

for v in CLAP_VALIDATOR PLUGINVAL CLAP COMPONENT VST3; do
  if [ -z "${!v}" ] || [ ! -e "${!v}" ]; then
    echo "validate.sh: $v not found (build dir: $BUILD)" >&2
    exit 1
  fi
done

echo "== clap-validator: $CLAP"
"$CLAP_VALIDATOR" validate "$CLAP" --only-failed

echo "== auval: $COMPONENT"
codesign --force --deep --sign - "$COMPONENT"
DEST="$HOME/Library/Audio/Plug-Ins/Components"
mkdir -p "$DEST"
rm -rf "${DEST:?}/$(basename "${COMPONENT:?}")"
cp -R "$COMPONENT" "$DEST/"
# Force AU re-registration of the freshly installed component.
killall -9 AudioComponentRegistrar 2>/dev/null || true
# Informational listing only; auval -a can crash with third-party AUs, the -v calls below are the gates.
auval -a | grep -E "Symb" || true
auval -v aumu EG13 Symb
auval -v aumf EGfx Symb

echo "== pluginval: $VST3"
codesign --force --deep --sign - "$VST3"
"$PLUGINVAL" --strictness-level 5 --validate-in-process --validate "$VST3"

echo "All validators passed."
