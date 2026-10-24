#!/usr/bin/env bash
# Fails when a plugin bundle defines an Objective-C class that is not EG13's own.
#   scripts/check-objc-prefix.sh <bundle>
# Hosts load many plugins into one process, and the Objective-C runtime has one class
# namespace, so every class EG13 defines must start with EG13, or be a Swift class of a module
# whose name does (_TtC6EG13UI..., and the aliased _TtC18EG13SymbioticIcons...). A class of an
# unrenamed SymbioticIcons/SymbioticTokens module (_TtC15SymbioticIcons...) would collide with
# another plugin built from the same packages.
# One exception: clap-wrapper's AUv2 view classes (wrapAsAUV2_<type>_cocoaUI_com_symbioticaudio_eg13...)
# are named after the bundle identifier, so they are unique to EG13 even without the prefix. Classes the bundle only references (NSView, ...) are not checked.
set -euo pipefail
if [ $# -ne 1 ]; then
  echo "usage: check-objc-prefix.sh <bundle>" >&2
  exit 2
fi
BUNDLE="$1"
if [ -d "$BUNDLE/Contents/MacOS" ]; then
  BIN="$(find "$BUNDLE/Contents/MacOS" -type f | head -1)"
else
  BIN="$BUNDLE"
fi
[ -f "$BIN" ] || { echo "check-objc-prefix: no binary in $BUNDLE" >&2; exit 2; }

# Per architecture slice, `otool -ov` prints "Contents of (__DATA,__objc_classlist) section" and,
# for each class defined here, a "name" line after its class_ro_t. Referenced classes only
# appear as bind targets, which are not listed in the section.
CLASSES="$(otool -ov "$BIN" | awk '
  /^Contents of \(__DATA[_A-Z]*,__objc_classlist\)/ { inlist = 1; next }
  /^Contents of / { inlist = 0 }
  inlist && /^[ \t]+name 0x[0-9a-f]+ / { print $3 }' | sort -u)"

# `nm -m` lists the same classes as _OBJC_CLASS_$_<name> defined symbols.
NM_CLASSES="$(nm -m "$BIN" 2>/dev/null | awk '/_OBJC_CLASS_\$_/ && !/\(undefined\)/ { sub(/.*_OBJC_CLASS_\$_/, ""); print }' | sort -u)"

ALL="$(printf '%s\n%s\n' "$CLASSES" "$NM_CLASSES" | sed '/^$/d' | sort -u)"
BAD=""
COUNT=0
while IFS= read -r name; do
  [ -n "$name" ] || continue
  COUNT=$((COUNT + 1))
  if [[ "$name" =~ ^(EG13|_TtC[0-9]+EG13|wrapAsAUV2_[a-z]+_cocoaUI_com_symbioticaudio_eg13) ]]; then
    :
  else
    BAD="$BAD$name"$'\n'
  fi
done <<<"$ALL"

echo "check-objc-prefix: $COUNT Objective-C classes defined in $BIN"
if [ -n "$BAD" ]; then
  echo "check-objc-prefix: classes without the EG13 prefix:" >&2
  printf '%s' "$BAD" | sed 's/^/  /' >&2
  exit 1
fi
