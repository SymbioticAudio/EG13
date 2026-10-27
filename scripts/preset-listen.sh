#!/bin/bash
# Listening workflow for EG13 preset sources.
#   scripts/preset-listen.sh <bank-dir> [--install [--force]]
# Renders every presets/<bank>/*.json with eg13_preset, writes build/listen/<bank>/index.html (a
# self-contained page with one player per preset), and with --install also compiles each preset
# into the User folder as "Draft – <name>" so it can be played in a DAW. An existing draft is kept
# (you may have edited and saved it in EG13) unless --force is given.
# Bash 3.2 / BSD tools only. EG13_PRESETS_PATH overrides the User folder (as in the editor);
# EG13_PRESET_TOOL overrides the tool path.
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
tool="${EG13_PRESET_TOOL:-$root/build/mac/tools/preset_tool/eg13_preset}"
dir=""
install=0
force=0
for arg in "$@"; do
  case "$arg" in
    --install) install=1 ;;
    --force) force=1 ;;
    -h|--help) sed -n 2,11p "$0"; exit 0 ;;
    *) dir="$arg" ;;
  esac
done
if [ -z "$dir" ] || [ ! -d "$dir" ]; then
  echo "usage: scripts/preset-listen.sh <bank-dir> [--install [--force]]" >&2
  exit 2
fi
[ -x "$tool" ] || { echo "preset-listen: $tool not found; build eg13_preset first" >&2; exit 2; }
if [ "$force" = 1 ] && [ "$install" = 0 ]; then
  echo "preset-listen: --force only makes sense with --install" >&2
  exit 2
fi

# info <file> <key>: one listing field of a source, from the tool.
info() { "$tool" info "$1" | sed -n "s/^$2=//p"; }
html() { sed -e 's/&/\&amp;/g' -e 's/</\&lt;/g' -e 's/>/\&gt;/g'; }

dir="$(cd "$dir" && pwd)"
bank="$(basename "$dir")"
out="$root/build/listen/$bank"
mkdir -p "$out"
rm -f "$out"/*.wav "$out"/index.html

if [ -n "${EG13_PRESETS_PATH:-}" ]; then
  user="$EG13_PRESETS_PATH"
else
  user="$HOME/Library/Application Support/SymbioticAudio/EG13/Presets"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
items="$tmp/items.html"
: > "$items"
count=0
failed=0
installed=0
kept=0
sources=0

# Sorted by the bank order (then file name).
list="$tmp/list"
for f in "$dir"/*.json; do
  [ -e "$f" ] || continue
  printf '%s\t%s\n' "$(info "$f" order)" "$f"
done | sort -n -k1,1 -s > "$list"

while IFS="$(printf '\t')" read -r _ src; do
  sources=$((sources + 1))
  file="$(basename "$src")"
  slug="${file%.json}"
  wav="$out/$slug.wav"
  if ! "$tool" render "$src" -o "$wav"; then
    echo "preset-listen: SKIPPED $file (render failed)" >&2
    failed=$((failed + 1))
    continue
  fi
  name="$(info "$src" name)"
  note="$(info "$src" note)"
  credit="$(info "$src" credit)"
  b64="$(base64 < "$wav" | tr -d '\n')"
  {
    printf '<section><h2>%s</h2>\n' "$(printf '%s' "$name" | html)"
    printf '<p class="note">%s</p>\n' "$(printf '%s' "$note" | html)"
    printf '<p class="credit">%s</p>\n' "$(printf '%s' "$credit" | html)"
    printf '<audio controls preload="none" src="data:audio/wav;base64,%s"></audio>\n' "$b64"
    printf '<p class="src">%s</p></section>\n' "$(printf '%s' "$file" | html)"
  } >> "$items"
  count=$((count + 1))

  if [ "$install" = 1 ]; then
    plugin="$(info "$src" plugin)"
    kind="$(info "$src" kind)"
    dest="$user"
    [ "$plugin" = "effect" ] && dest="$user/FX"
    ext="eg13perf"
    [ "$kind" = "patch" ] && ext="eg13patch"
    draft="Draft – $(printf '%s' "$name" | tr '/:' '--')"
    draftname="Draft – $name"
    mkdir -p "$dest"
    target="$dest/$draft.$ext"
    # The target always starts "Draft – ", so a non-draft preset is never touched. A draft that
    # exists may hold edits saved in EG13 that were not sent back yet: keep it unless --force.
    if [ -e "$target" ] && [ "$force" != 1 ]; then
      echo "kept your edited draft: $target; use --force to replace"
      kept=$((kept + 1))
    elif "$tool" compile "$src" --name "$draftname" -o "$tmp/draft.out"; then
      cp "$tmp/draft.out" "$target"
      echo "installed: $target"
      installed=$((installed + 1))
    else
      echo "preset-listen: could not compile $file for install" >&2
      failed=$((failed + 1))
    fi
  fi
done < "$list"

cat > "$out/index.html" <<HTML
<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EG13 listening: $bank</title>
<style>
  body { background:#14151a; color:#d8dae0; font:15px/1.4 -apple-system, system-ui, sans-serif; max-width:760px; margin:2rem auto; padding:0 1rem; }
  h1 { font-size:1.2rem; font-weight:600; }
  section { background:#1d1f27; border-radius:8px; padding:.8rem 1rem; margin:.8rem 0; }
  h2 { font-size:1.05rem; margin:0 0 .2rem; }
  p { margin:.15rem 0; }
  .note { color:#b4b8c4; } .credit { color:#80859a; font-size:.85rem; }
  .src { color:#80859a; font:12px ui-monospace, Menlo, monospace; }
  audio { width:100%; margin:.5rem 0 .2rem; }
</style></head><body>
<h1>EG13 listening: $bank ($count presets)</h1>
$(cat "$items")
</body></html>
HTML

echo
echo "Listening page: $out/index.html"
echo "WAVs: $out/*.wav ($count rendered, $failed failed of $sources)"
if [ "$install" = 1 ]; then
  echo
  echo "Installed $installed draft(s), kept $kept existing, in: $user"
  echo "Send changes back: tweak the draft in your DAW, then in EG13 Save over it (Save on a"
  echo "User preset), and for each one run:"
  echo "  $tool decompile \"<User folder>/Draft – <name>.eg13perf\" -o presets/$bank/<slug>.json"
  echo "(.eg13patch for a Patch; effect drafts live in the FX/ subfolder). Then drop the"
  echo "\"Draft – \" prefix from the \"name\" field in the source before committing."
fi
[ "$failed" = 0 ]
