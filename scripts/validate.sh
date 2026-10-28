#!/usr/bin/env bash
# Runs clap-validator, auval (instrument + FX), an out-of-process AU render check and pluginval on a
# mac build.
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
# A new EG13 instrument opens on the factory first-load preset (M7 spec goal 1), so its parameters
# are not at their declared defaults after init: the defaults stay the init patch's, which is what
# reset-to-default uses. param-default-values is skipped for the instrument alone; EG13 FX keeps
# every test.
# An ID that matches no plugin runs only the 7 library-level tests and passes, so each run must also
# have run a plugin-level test (the skip above makes this the only guard against a renamed ID).
clap_validate() {
  local id="$1" out
  shift
  out="$(mktemp)"
  if ! "$CLAP_VALIDATOR" validate "$CLAP" --only-failed -p "$id" "$@" 2>&1 | tee "$out"; then
    rm -f "$out"
    return 1
  fi
  if ! grep -q "Test param-fuzz-basic completed" "$out"; then
    rm -f "$out"
    echo "validate.sh: clap-validator ran no plugin tests for '$id': is the plugin ID right?" >&2
    return 1
  fi
  rm -f "$out"
}
clap_validate com.symbioticaudio.eg13.fx
clap_validate com.symbioticaudio.eg13 -x '^param-default-values$'

echo "== AU Info.plist: $COMPONENT/Contents/Info.plist"
# A stale or generic bundle plist makes auval report "didn't find the component", so check first.
PLIST="$COMPONENT/Contents/Info.plist"
for expected in "aumu EG13 Symb" "aumf EGfx Symb"; do
  read -r want_type want_sub want_man <<<"$expected"
  found=0
  n="$(plutil -extract AudioComponents raw -o - "$PLIST" 2>/dev/null || echo 0)"
  for ((i = 0; i < n; i++)); do
    t="$(plutil -extract "AudioComponents.$i.type" raw -o - "$PLIST" 2>/dev/null || true)"
    s="$(plutil -extract "AudioComponents.$i.subtype" raw -o - "$PLIST" 2>/dev/null || true)"
    m="$(plutil -extract "AudioComponents.$i.manufacturer" raw -o - "$PLIST" 2>/dev/null || true)"
    if [ "$t" = "$want_type" ] && [ "$s" = "$want_sub" ] && [ "$m" = "$want_man" ]; then found=1; break; fi
  done
  if [ "$found" -ne 1 ]; then
    echo "validate.sh: $PLIST has no AudioComponents entry for $want_type/$want_sub/$want_man (stale or generic plist?). Rebuild: cmake --build --preset mac --target EG13_all" >&2
    exit 1
  fi
  echo "ok: $want_type/$want_sub/$want_man"
done

echo "== auval: $COMPONENT"
# Bundles without a valid signature get an ad-hoc one; a valid one (a Developer ID signature in the
# release workflow, which validates the signed bundles again) is kept.
codesign --verify --strict "$COMPONENT" 2>/dev/null || codesign --force --deep --sign - "$COMPONENT"
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

echo "== out-of-process AU render check"
# auval hosts in process. Logic Pro 12 hosts AUs out of process, where an unconnected input bus fails
# every render (the instrument once had a sidechain input and was silent there), so render both
# units out of process too: the instrument with no input connected, EG13 FX fed a tone. Prints SKIP
# (and passes) only where out-of-process hosting itself is unavailable.
AU_OOP_CHECK="$BUILD/tools/au_oop_check"
mkdir -p "$(dirname "$AU_OOP_CHECK")"
clang++ -std=c++17 -fobjc-arc -O1 -Wall -Werror -framework AudioToolbox -framework Foundation \
  "$ROOT/tools/au_oop_check/au_oop_check.mm" -o "$AU_OOP_CHECK"
"$AU_OOP_CHECK" aumu EG13 Symb
"$AU_OOP_CHECK" aumf EGfx Symb --tone

echo "== pluginval: $VST3"
codesign --verify --strict "$VST3" 2>/dev/null || codesign --force --deep --sign - "$VST3"
"$PLUGINVAL" --strictness-level 5 --validate-in-process --validate "$VST3"

echo "All validators passed."
