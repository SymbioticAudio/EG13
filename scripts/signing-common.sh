# Shared by sign-and-notarize.sh and build-pkg.sh (sourced, not run).
# The six secrets (docs/release/signing-setup.md), read from the environment:
#   MACOS_DEVELOPER_ID_APP_P12, MACOS_DEVELOPER_ID_INSTALLER_P12  base64 of the exported .p12 files
#   MACOS_CERT_PASSWORD                                           the password of both .p12 files
#   APPLE_ID, APPLE_TEAM_ID, APPLE_APP_PASSWORD                   for notarytool
# DRY_RUN=1 prints each command instead of running it, with the secret values masked.

SIGNING_SECRETS=(MACOS_DEVELOPER_ID_APP_P12 MACOS_DEVELOPER_ID_INSTALLER_P12 MACOS_CERT_PASSWORD
  APPLE_ID APPLE_TEAM_ID APPLE_APP_PASSWORD)
DRY_RUN="${DRY_RUN:-0}"
TMP_BASE="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
TMP_BASE="${TMP_BASE%/}"
# The notarytool credentials live in the temporary keychain under this profile name, so the
# app-specific password is never on a notarytool submit or log command line.
NOTARY_PROFILE="eg13-notary"
OLD_KEYCHAINS=()
# Note: these scripts must run under macOS's /bin/bash 3.2, where "${a[@]}" of an empty array
# fails under set -u; possibly empty arrays are expanded as ${a[@]+"${a[@]}"}.

# True when all six secrets are set and not empty.
signing_secrets_present() {
  local s
  for s in "${SIGNING_SECRETS[@]}"; do
    [ -n "${!s:-}" ] || return 1
  done
}

# Prints a command with every secret value (and the keychain password) replaced by ***.
print_masked() {
  local out="+" arg s masked
  for arg in "$@"; do
    masked="$arg"
    for s in "${SIGNING_SECRETS[@]}" KEYCHAIN_PASSWORD; do
      if [ -n "${!s:-}" ] && [ "$s" != APPLE_TEAM_ID ]; then
        masked="${masked//"${!s}"/***}"
      fi
    done
    out+=" $(printf '%q' "$masked")"
  done
  echo "$out"
}

# Runs a command, or prints it under DRY_RUN=1.
run() {
  if [ "$DRY_RUN" = 1 ]; then
    print_masked "$@"
  else
    "$@"
  fi
}

# Creates a temporary keychain, imports the given base64 .p12 secrets into it and puts it on the
# search list. keychain_cleanup (set as an EXIT trap by the callers) deletes it again.
#   keychain_setup MACOS_DEVELOPER_ID_APP_P12 [MACOS_DEVELOPER_ID_INSTALLER_P12]
keychain_setup() {
  SIGN_TMP="$(mktemp -d "$TMP_BASE/eg13-signing.XXXXXX")"
  KEYCHAIN="$SIGN_TMP/eg13-signing.keychain-db"
  KEYCHAIN_PASSWORD="$(uuidgen)"
  OLD_KEYCHAINS=()
  local line
  while IFS= read -r line; do
    line="${line#"${line%%[![:space:]]*}"}"
    line="${line#\"}"
    OLD_KEYCHAINS+=("${line%\"}")
  done < <(security list-keychains -d user)
  run security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  run security set-keychain-settings -lut 21600 "$KEYCHAIN"
  run security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  local secret p12
  for secret in "$@"; do
    p12="$SIGN_TMP/$secret.p12"
    if [ "$DRY_RUN" = 1 ]; then
      echo "+ (decode \$$secret from base64 into $p12)"
    else
      printf '%s' "${!secret}" | base64 --decode >"$p12"
    fi
    run security import "$p12" -k "$KEYCHAIN" -f pkcs12 -P "$MACOS_CERT_PASSWORD" \
      -T /usr/bin/codesign -T /usr/bin/productbuild -T /usr/bin/productsign
    [ "$DRY_RUN" = 1 ] || rm -f "$p12"
  done
  run security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
  run security list-keychains -d user -s "$KEYCHAIN" ${OLD_KEYCHAINS[@]+"${OLD_KEYCHAINS[@]}"}
  # The one command line that carries the app-specific password; notarytool stores it in the
  # temporary keychain, and submit and log use the profile.
  run xcrun notarytool store-credentials "$NOTARY_PROFILE" --apple-id "$APPLE_ID" \
    --team-id "$APPLE_TEAM_ID" --password "$APPLE_APP_PASSWORD" --keychain "$KEYCHAIN"
}

keychain_cleanup() {
  [ -n "${SIGN_TMP:-}" ] || return 0
  if [ "${#OLD_KEYCHAINS[@]}" -gt 0 ]; then
    run security list-keychains -d user -s "${OLD_KEYCHAINS[@]}" || true
  else
    run security list-keychains -d user -s || true
  fi
  run security delete-keychain "$KEYCHAIN" || true
  rm -rf "$SIGN_TMP"
  SIGN_TMP=""
}

# Prints the SHA-1 of the one identity in the temporary keychain whose name starts with <kind>
# ("Developer ID Application" or "Developer ID Installer") and belongs to APPLE_TEAM_ID.
find_identity() {
  local kind="$1"
  if [ "$DRY_RUN" = 1 ]; then
    echo "<$kind identity of team $APPLE_TEAM_ID>"
    return 0
  fi
  # Application certificates are found under the code-signing policy; Installer ones are not
  # code-signing identities, so they are searched without a policy.
  local args=(find-identity -v)
  [ "$kind" = "Developer ID Application" ] && args+=(-p codesigning)
  args+=("$KEYCHAIN")
  local listing found
  listing="$(security "${args[@]}")" || { echo "security find-identity failed" >&2; return 1; }
  found="$(awk -v k="\"$kind: " -v t="($APPLE_TEAM_ID)\"" \
    'index($0, k) && index($0, t) { print $2; exit }' <<<"$listing")"
  if [ -z "$found" ]; then
    echo "no valid '$kind' identity for team $APPLE_TEAM_ID in the imported certificates" >&2
    return 1
  fi
  echo "$found"
}

# Submits a file (.zip or .pkg) to the notary service, waits, and fails unless it is Accepted.
notarize() {
  local file="$1" result status id
  local profile=(--keychain-profile "$NOTARY_PROFILE" --keychain "$KEYCHAIN")
  if [ "$DRY_RUN" = 1 ]; then
    run xcrun notarytool submit "$file" "${profile[@]}" --wait --output-format json
    return 0
  fi
  result="$(xcrun notarytool submit "$file" "${profile[@]}" --wait --output-format json)" || true
  echo "$result"
  status="$(plutil -extract status raw -o - - <<<"$result" 2>/dev/null || true)"
  id="$(plutil -extract id raw -o - - <<<"$result" 2>/dev/null || true)"
  if [ "$status" != Accepted ]; then
    echo "notarisation of $(basename "$file") failed (status: ${status:-none})" >&2
    if [ -n "$id" ]; then
      xcrun notarytool log "$id" "${profile[@]}" >&2 || true
    fi
    return 1
  fi
}

# Offline self-test of the code above with `security` and `xcrun` stubbed out (no keychain is
# touched, nothing is signed): keychain set-up and clean-up with a non-empty and an empty keychain
# search list, find_identity for both certificate kinds, and notarize. Run under /bin/bash to
# cover bash 3.2: /bin/bash scripts/sign-and-notarize.sh --self-test
signing_self_test() {
  local log failures=0 team=ABCDE12345 app_hash=1111111111111111111111111111111111111111
  local inst_hash=2222222222222222222222222222222222222222 search_list got
  log="$(mktemp "$TMP_BASE/eg13-selftest.XXXXXX")"
  check() { if [ "$2" = "$3" ]; then echo "ok: $1"; else echo "FAIL: $1: got '$2', want '$3'"; failures=$((failures + 1)); fi; }
  security() {
    echo "security $*" >>"$log"
    case "$1" in
      list-keychains) [ "$*" = "list-keychains -d user" ] && [ -n "$search_list" ] && echo "    \"$search_list\"" ;;
      find-identity)
        echo "  1) $app_hash \"Developer ID Application: Test ($team)\""
        if [ "$3" != "-p" ]; then echo "  2) $inst_hash \"Developer ID Installer: Test ($team)\""; fi
        echo "     2 valid identities found" ;;
    esac
    return 0
  }
  xcrun() {
    echo "xcrun $*" >>"$log"
    [ "$2" = submit ] && echo '{"id":"00000000-0000-0000-0000-000000000000","status":"Accepted","message":"ok"}'
    return 0
  }
  local MACOS_DEVELOPER_ID_APP_P12 MACOS_DEVELOPER_ID_INSTALLER_P12 MACOS_CERT_PASSWORD
  local APPLE_ID APPLE_TEAM_ID APPLE_APP_PASSWORD
  MACOS_DEVELOPER_ID_APP_P12="$(printf 'app' | base64)"
  MACOS_DEVELOPER_ID_INSTALLER_P12="$(printf 'installer' | base64)"
  MACOS_CERT_PASSWORD=certpw APPLE_ID=me@example.com APPLE_TEAM_ID=$team APPLE_APP_PASSWORD=app-pw
  DRY_RUN=0
  check "secrets present" "$(signing_secrets_present && echo yes)" yes
  for search_list in "/Users/test/Library/Keychains/login.keychain-db" ""; do
    : >"$log"
    keychain_setup MACOS_DEVELOPER_ID_APP_P12 MACOS_DEVELOPER_ID_INSTALLER_P12
    check "search list read (${search_list:-empty})" "${#OLD_KEYCHAINS[@]}" "$([ -n "$search_list" ] && echo 1 || echo 0)"
    got="$(find_identity "Developer ID Application")"
    check "Application identity" "$got" "$app_hash"
    got="$(find_identity "Developer ID Installer")"
    check "Installer identity" "$got" "$inst_hash"
    check "Application lookup uses the codesigning policy" \
      "$(grep -c "^security find-identity -v -p codesigning $KEYCHAIN\$" "$log")" 1
    check "Installer lookup has no policy" "$(grep -c "^security find-identity -v $KEYCHAIN\$" "$log")" 1
    check "notarize accepted" "$(notarize /tmp/x.zip >/dev/null && echo yes)" yes
    check "password only in store-credentials" "$(grep -c -- "--password app-pw" "$log")" 1
    check "submit uses the profile" \
      "$(grep -c "^xcrun notarytool submit /tmp/x.zip --keychain-profile $NOTARY_PROFILE --keychain $KEYCHAIN" "$log")" 1
    local kc="$KEYCHAIN"
    keychain_cleanup
    check "keychain deleted" "$(grep -c "^security delete-keychain $kc\$" "$log")" 1
    check "search list restored" "$(grep -c "^security list-keychains -d user -s${search_list:+ $search_list}\$" "$log")" 1
  done
  check "no identity for another team" "$(APPLE_TEAM_ID=OTHER find_identity "Developer ID Installer" 2>/dev/null || echo none)" none
  unset -f security xcrun check
  rm -f "$log"
  if [ "$failures" -ne 0 ]; then
    echo "signing self-test: $failures failed (bash $BASH_VERSION)" >&2
    return 1
  fi
  echo "signing self-test passed (bash $BASH_VERSION)"
}
