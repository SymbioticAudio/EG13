#!/usr/bin/env bash
# Signs, notarises and staples the shipped plugin bundles in place.
#   scripts/sign-and-notarize.sh [--dry-run] <plugins-dir>
#   scripts/sign-and-notarize.sh --self-test   (offline test of the signing helpers, run under
#                                                /bin/bash: see signing_self_test)
# <plugins-dir> holds EG13.component, EG13.vst3 and EG13.clap (e.g. build/mac/plugins); run it
# after the validators and before scripts/package-release.sh, so the zip holds the signed bundles.
# With all six secrets set (docs/release/signing-setup.md): imports the Developer ID Application
# certificate into a temporary keychain, signs each bundle inside-out with the hardened runtime and
# a secure timestamp, submits them to the notary service (notarytool submit --wait), staples the
# tickets and verifies; the keychain is deleted on exit. With any secret missing it prints
# "Signing skipped: Developer ID secrets not set" and exits 0.
# --dry-run prints the commands it would run (secret values masked) and changes nothing.
# Under GitHub Actions it writes signed=true or signed=false to $GITHUB_OUTPUT.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=signing-common.sh
source "$ROOT/scripts/signing-common.sh"

if [ "${1:-}" = --self-test ]; then
  signing_self_test
  exit
fi
if [ "${1:-}" = --dry-run ]; then
  DRY_RUN=1
  shift
fi
if [ $# -ne 1 ]; then
  echo "usage: sign-and-notarize.sh [--dry-run] <plugins-dir> | --self-test" >&2
  exit 2
fi
PLUGINS="$1"
BUNDLES=(EG13.component EG13.vst3 EG13.clap)

output() { [ -z "${GITHUB_OUTPUT:-}" ] || echo "$1" >>"$GITHUB_OUTPUT"; }

if ! signing_secrets_present; then
  echo "Signing skipped: Developer ID secrets not set"
  output signed=false
  exit 0
fi

for b in "${BUNDLES[@]}"; do
  [ -d "$PLUGINS/$b" ] || { echo "sign-and-notarize.sh: $PLUGINS/$b not found" >&2; exit 1; }
done

trap keychain_cleanup EXIT
keychain_setup MACOS_DEVELOPER_ID_APP_P12
IDENTITY="$(find_identity "Developer ID Application")"

sign() {
  run codesign --force --options runtime --timestamp --sign "$IDENTITY" --keychain "$KEYCHAIN" "$1"
}

for b in "${BUNDLES[@]}"; do
  bundle="$PLUGINS/$b"
  # Inside-out: any nested code (frameworks, helpers, dylibs) first, deepest first, then the bundle.
  # EG13's bundles hold one executable today; this keeps them signable if that changes.
  while IFS= read -r nested; do
    sign "$nested"
  done < <(find "$bundle/Contents" -mindepth 2 \
    \( -name '*.framework' -o -name '*.dylib' -o -name '*.bundle' -o -name '*.app' -o -name '*.xpc' \) \
    | awk '{ print gsub("/", "/"), $0 }' | sort -rn | cut -d' ' -f2-)
  sign "$bundle"
  run codesign --verify --strict --verbose=2 "$bundle"
done

# The notary service takes one zip of all three bundles; the tickets are stapled to each bundle.
SUBMIT_DIR="$(mktemp -d "$TMP_BASE/eg13-notarize.XXXXXX")"
trap 'keychain_cleanup; rm -rf "$SUBMIT_DIR"' EXIT
mkdir "$SUBMIT_DIR/EG13"
for b in "${BUNDLES[@]}"; do
  run ditto "$PLUGINS/$b" "$SUBMIT_DIR/EG13/$b"
done
run ditto -c -k --keepParent "$SUBMIT_DIR/EG13" "$SUBMIT_DIR/EG13-notarize.zip"
notarize "$SUBMIT_DIR/EG13-notarize.zip"

for b in "${BUNDLES[@]}"; do
  run xcrun stapler staple "$PLUGINS/$b"
  run xcrun stapler validate "$PLUGINS/$b"
done

if [ "$DRY_RUN" = 1 ]; then
  echo "Dry run: nothing was signed"
  output signed=false
else
  echo "Signed, notarised and stapled: ${BUNDLES[*]}"
  output signed=true
fi
