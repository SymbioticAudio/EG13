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
  run security list-keychains -d user -s "$KEYCHAIN" "${OLD_KEYCHAINS[@]}"
}

keychain_cleanup() {
  [ -n "${SIGN_TMP:-}" ] || return 0
  if [ "${#OLD_KEYCHAINS[@]}" -gt 0 ]; then
    run security list-keychains -d user -s "${OLD_KEYCHAINS[@]}" || true
  fi
  run security delete-keychain "$KEYCHAIN" || true
  rm -rf "$SIGN_TMP"
  SIGN_TMP=""
}

# Prints the SHA-1 of the one identity in the temporary keychain whose name starts with <kind>
# ("Developer ID Application" or "Developer ID Installer") and belongs to APPLE_TEAM_ID.
find_identity() {
  local kind="$1" policy=()
  if [ "$DRY_RUN" = 1 ]; then
    echo "<$kind identity of team $APPLE_TEAM_ID>"
    return 0
  fi
  [ "$kind" = "Developer ID Application" ] && policy=(-p codesigning)
  local found
  found="$(security find-identity -v "${policy[@]}" "$KEYCHAIN" |
    awk -v k="\"$kind: " -v t="($APPLE_TEAM_ID)\"" 'index($0, k) && index($0, t) { print $2; exit }')"
  if [ -z "$found" ]; then
    echo "no valid '$kind' identity for team $APPLE_TEAM_ID in the imported certificates" >&2
    return 1
  fi
  echo "$found"
}

# Submits a file (.zip or .pkg) to the notary service, waits, and fails unless it is Accepted.
notarize() {
  local file="$1" result status id
  if [ "$DRY_RUN" = 1 ]; then
    run xcrun notarytool submit "$file" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
      --password "$APPLE_APP_PASSWORD" --wait --output-format json
    return 0
  fi
  result="$(xcrun notarytool submit "$file" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
    --password "$APPLE_APP_PASSWORD" --wait --output-format json)" || true
  echo "$result"
  status="$(plutil -extract status raw -o - - <<<"$result" 2>/dev/null || true)"
  id="$(plutil -extract id raw -o - - <<<"$result" 2>/dev/null || true)"
  if [ "$status" != Accepted ]; then
    echo "notarisation of $(basename "$file") failed (status: ${status:-none})" >&2
    if [ -n "$id" ]; then
      xcrun notarytool log "$id" --apple-id "$APPLE_ID" --team-id "$APPLE_TEAM_ID" \
        --password "$APPLE_APP_PASSWORD" >&2 || true
    fi
    return 1
  fi
}
