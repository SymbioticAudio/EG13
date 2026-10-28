#!/usr/bin/env bash
# Builds the signed, notarised installer.
#   scripts/build-pkg.sh [--dry-run] <plugins-dir> <version> <out-dir>
# Writes <out-dir>/EG13-<version>-macOS.pkg, which installs EG13.component, EG13.vst3 and EG13.clap
# to /Library/Audio/Plug-Ins/{Components,VST3,CLAP} (for all users, as installers usually do; the
# zip guide uses ~/Library instead). Its welcome text tells users to remove a zip-installed copy.
# Run it only after scripts/sign-and-notarize.sh has signed <plugins-dir>: it needs the same six
# secrets (it signs with the Developer ID Installer certificate and notarises the .pkg) and fails
# without them. --dry-run prints the signing and notarising commands instead of running them.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=signing-common.sh
source "$ROOT/scripts/signing-common.sh"

if [ "${1:-}" = --dry-run ]; then
  DRY_RUN=1
  shift
fi
if [ $# -ne 3 ]; then
  echo "usage: build-pkg.sh [--dry-run] <plugins-dir> <version> <out-dir>" >&2
  exit 2
fi
PLUGINS="$1"
VERSION="$2"
OUT="$3"
NAME="EG13-$VERSION-macOS"

fail() { echo "build-pkg.sh: $*" >&2; exit 1; }

signing_secrets_present || fail "the Developer ID secrets are not set: the .pkg is only built when signing is on"

# bundle  install location                       package identifier
COMPONENTS=(
  "EG13.component /Library/Audio/Plug-Ins/Components com.symbioticaudio.eg13.pkg.au"
  "EG13.vst3 /Library/Audio/Plug-Ins/VST3 com.symbioticaudio.eg13.pkg.vst3"
  "EG13.clap /Library/Audio/Plug-Ins/CLAP com.symbioticaudio.eg13.pkg.clap"
)
for c in "${COMPONENTS[@]}"; do
  read -r b _ _ <<<"$c"
  [ -d "$PLUGINS/$b" ] || fail "$PLUGINS/$b not found"
  # The bundles must already carry a Developer ID signature (sign-and-notarize.sh).
  if [ "$DRY_RUN" != 1 ]; then
    codesign --verify --strict "$PLUGINS/$b" || fail "$b is not validly signed: run sign-and-notarize.sh first"
    info="$(codesign -dv --verbose=2 "$PLUGINS/$b" 2>&1)" || fail "codesign -dv failed on $b"
    grep -q "^Authority=Developer ID Application" <<<"$info" ||
      fail "$b is not signed with a Developer ID Application certificate: run sign-and-notarize.sh first"
  fi
done

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
WORK="$(mktemp -d "$TMP_BASE/eg13-pkg.XXXXXX")"
trap 'keychain_cleanup; rm -rf "$WORK"' EXIT
mkdir -p "$WORK/packages" "$WORK/resources"

# One component package per format. Bundles are not relocatable: otherwise Installer "upgrades" a
# copy it finds elsewhere (such as a zip-installed one in ~/Library) instead of installing here.
PKG_ARGS=()
for c in "${COMPONENTS[@]}"; do
  read -r b location id <<<"$c"
  root="$WORK/root-$b"
  mkdir -p "$root"
  ditto "$PLUGINS/$b" "$root/$b"
  plist="$WORK/$b.plist"
  pkgbuild --analyze --root "$root" "$plist" >/dev/null
  i=0
  while plutil -extract "$i.RootRelativeBundlePath" raw -o - "$plist" >/dev/null 2>&1; do
    plutil -replace "$i.BundleIsRelocatable" -bool NO "$plist"
    plutil -replace "$i.BundleIsVersionChecked" -bool NO "$plist"
    i=$((i + 1))
  done
  [ "$i" -gt 0 ] || fail "pkgbuild found no bundle in $root"
  pkgbuild --root "$root" --component-plist "$plist" --install-location "$location" \
    --identifier "$id" --version "$VERSION" "$WORK/packages/$id.pkg"
  PKG_ARGS+=(--package "$WORK/packages/$id.pkg")
done

cat >"$WORK/resources/welcome.txt" <<EOF
EG13 $VERSION

This installs the EG13 and EG13 FX plugins for all users of this Mac:

  AU    /Library/Audio/Plug-Ins/Components/EG13.component
  VST3  /Library/Audio/Plug-Ins/VST3/EG13.vst3
  CLAP  /Library/Audio/Plug-Ins/CLAP/EG13.clap

If you installed EG13 from the zip before, remove that copy first: delete EG13.component,
EG13.vst3 and EG13.clap from the Components, VST3 and CLAP folders in
~/Library/Audio/Plug-Ins/ (the Library folder in your home folder). With two copies
installed, a host may load the old one instead of this version.

After installing, rescan plugins in your host. EG13 is free software under the GNU GPL
version 3; the source code is at https://github.com/SymbioticAudio/EG13.
EOF

DIST="$WORK/distribution.xml"
productbuild --synthesize "${PKG_ARGS[@]}" "$DIST"
# Title, welcome text, no customisation, both architectures.
perl -0pi -e "s|<installer-gui-script minSpecVersion=\"(\\d+)\">|<installer-gui-script minSpecVersion=\"\$1\">\\n    <title>EG13 $VERSION</title>\\n    <welcome file=\"welcome.txt\" mime-type=\"text/plain\"/>\\n    <domains enable_anywhere=\"false\" enable_currentUserHome=\"false\" enable_localSystem=\"true\"/>|" "$DIST"
perl -0pi -e 's|<options customize="never" require-scripts="false"(?: hostArchitectures="[^"]*")?/>|<options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>|' "$DIST"
grep -q '<welcome file="welcome.txt"' "$DIST" || fail "could not add the welcome text to $DIST"
grep -q 'hostArchitectures="arm64,x86_64"' "$DIST" || fail "could not set the architectures in $DIST"

keychain_setup MACOS_DEVELOPER_ID_INSTALLER_P12
IDENTITY="$(find_identity "Developer ID Installer")"
PKG="$OUT/$NAME.pkg"
rm -f "$PKG"
run productbuild --distribution "$DIST" --resources "$WORK/resources" --package-path "$WORK/packages" \
  --version "$VERSION" --sign "$IDENTITY" --keychain "$KEYCHAIN" --timestamp "$PKG"
run pkgutil --check-signature "$PKG"
notarize "$PKG"
run xcrun stapler staple "$PKG"
run xcrun stapler validate "$PKG"
if [ "$DRY_RUN" = 1 ]; then
  echo "Dry run: no .pkg was written"
else
  echo "Wrote $PKG"
fi
