#!/usr/bin/env bash
# Packages a mac build into the release zip.
#   scripts/package-release.sh <build-dir> <version> <out-dir>
# Writes <out-dir>/EG13-<version>-macOS.zip, holding one folder EG13-<version>-macOS/ with
# EG13.component, EG13.vst3, EG13.clap, INSTALL.txt (plain text of docs/manual/install.md),
# LICENSE, THIRD-PARTY-NOTICES.md and SOURCE.txt (the repository URL, tag and commit).
# The Standalone EG13.app is a by-product of the build and is not shipped.
# Fails when the build's version is not <version>, or when a bundle holds any SymbioticBrand
# symbol (that artwork is all rights reserved and must never ship: build with EG13_NO_BRAND=ON).
# Bundles are copied as they are: run scripts/sign-and-notarize.sh on <build-dir>/plugins first for
# a signed zip. CI runs this on every push as a dry run of the release packaging.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ $# -ne 3 ]; then
  echo "usage: package-release.sh <build-dir> <version> <out-dir>" >&2
  exit 2
fi
BUILD="$1"
VERSION="$2"
OUT="$3"
PLUGINS="$BUILD/plugins"
NAME="EG13-$VERSION-macOS"
BUNDLES=(EG13.component EG13.vst3 EG13.clap)

fail() { echo "package-release.sh: $*" >&2; exit 1; }

# The version the build was configured with (CMakeLists.txt project()) must be the one asked for.
built="$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$BUILD/CMakeCache.txt" 2>/dev/null || true)"
[ -n "$built" ] || fail "no CMAKE_PROJECT_VERSION in $BUILD/CMakeCache.txt (not a configured build dir?)"
[ "$built" = "$VERSION" ] || fail "the build is version $built, not $VERSION"

for b in "${BUNDLES[@]}"; do
  [ -d "$PLUGINS/$b" ] || fail "$PLUGINS/$b not found (build EG13_all first)"
  v="$(plutil -extract CFBundleShortVersionString raw -o - "$PLUGINS/$b/Contents/Info.plist" 2>/dev/null || true)"
  [ "$v" = "$VERSION" ] || fail "$b has CFBundleShortVersionString '$v', not $VERSION"
done

# The SymbioticBrand artwork must not ship. Every Mach-O file in each bundle is checked (the main
# executable and any framework, dylib or helper); nm lists every architecture slice of a universal
# binary. Output goes into a variable first: under pipefail, `nm | grep -q` can die of SIGPIPE.
for b in "${BUNDLES[@]}"; do
  bin="$PLUGINS/$b/Contents/MacOS/EG13"
  [ -f "$bin" ] || fail "no binary at $bin"
  syms="$(nm -a "$bin")" || fail "nm failed on $bin"
  # Positive control: the editor's symbols (aliased symbiotic-design icons included) must be
  # found in the main executable, or the checks below prove nothing.
  grep -qE "EG13UI" <<<"$syms" || fail "self-check failed: no EG13UI symbol in $bin"
  grep -qE "EG13SymbioticIcons" <<<"$syms" || fail "self-check failed: no EG13SymbioticIcons symbol in $bin"
  machos=0
  while IFS= read -r -d '' f; do
    kind="$(file -b "$f")"
    case "$kind" in *Mach-O*) ;; *) continue ;; esac
    machos=$((machos + 1))
    syms="$(nm -a "$f" 2>&1)" || fail "nm failed on $f"
    if grep -qE "SymbioticBrand" <<<"$syms"; then
      fail "$f contains SymbioticBrand symbols: the artwork is all rights reserved and must not ship. Configure with -DEG13_NO_BRAND=ON (or EG13_NO_BRAND=1) and rebuild."
    fi
    if grep -aq "SymbioticBrand" "$f"; then
      fail "$f contains the string SymbioticBrand: the artwork is all rights reserved and must not ship."
    fi
  done < <(find "$PLUGINS/$b" -type f -print0)
  [ "$machos" -ge 1 ] || fail "self-check failed: no Mach-O file found in $b"
done
echo "ok: no SymbioticBrand symbols in ${BUNDLES[*]}"

INSTALL_MD="$ROOT/docs/manual/install.md"
[ -f "$INSTALL_MD" ] || fail "$INSTALL_MD not found (INSTALL.txt is made from it)"
for f in LICENSE THIRD-PARTY-NOTICES.md; do
  [ -f "$ROOT/$f" ] || fail "$ROOT/$f not found"
done

mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
STAGE="$(mktemp -d "$OUT/stage.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
DIR="$STAGE/$NAME"
mkdir "$DIR"

for b in "${BUNDLES[@]}"; do
  ditto "$PLUGINS/$b" "$DIR/$b"
  # A bundle without a valid signature of its own (the linker only signs the executable) gets an
  # ad-hoc one, as validate.sh gives it, so every unsigned zip is the same. A Developer ID
  # signature from sign-and-notarize.sh is kept as it is.
  if ! codesign --verify --strict "$DIR/$b" 2>/dev/null; then
    codesign --force --sign - "$DIR/$b"
  fi
done

# Markdown to plain text: headings lose their marks (level 1 and 2 are underlined), emphasis and
# code marks and code fences go, links to other manual pages keep their text, web links show their URL.
perl -CSD -pe '
  if (/^\s*```/) { $_ = ""; next; }
  s/\*\*(.+?)\*\*/$1/g; s/`([^`]*)`/$1/g;
  s/\[([^\]]+)\]\((?![a-z]+:)[^)]*\)/$1/g;
  s/\[([^\]]+)\]\(([a-z]+:[^)]*)\)/$1 ($2)/g;
  if (/^(#{1,2}) (.*)$/) { my $u = ($1 eq "#") ? "=" : "-"; $_ = "$2\n" . ($u x length($2)) . "\n"; }
  elsif (/^#{3,} (.*)$/) { $_ = "$1\n"; }
' "$INSTALL_MD" >"$DIR/INSTALL.txt"

cp "$ROOT/LICENSE" "$DIR/LICENSE"
cp "$ROOT/THIRD-PARTY-NOTICES.md" "$DIR/THIRD-PARTY-NOTICES.md"

REPO_URL="https://github.com/SymbioticAudio/EG13"
if [ -n "${GITHUB_SERVER_URL:-}" ] && [ -n "${GITHUB_REPOSITORY:-}" ]; then
  REPO_URL="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY"
fi
COMMIT="$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
cat >"$DIR/SOURCE.txt" <<EOF
EG13 $VERSION is free software under the GNU General Public License, version 3 (see LICENSE).
Its complete source code, including the build scripts, is at:

  $REPO_URL/tree/v$VERSION

Repository: $REPO_URL
Tag:        v$VERSION
Commit:     $COMMIT

Third-party components and their licences: THIRD-PARTY-NOTICES.md.
EOF

ZIP="$OUT/$NAME.zip"
rm -f "$ZIP"
ditto -c -k --keepParent "$DIR" "$ZIP"
echo "Wrote $ZIP"
