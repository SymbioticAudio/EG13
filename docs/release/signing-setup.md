# Turning on signing and notarisation

The release workflow (`.github/workflows/release.yml`) signs, notarises and builds the `.pkg` installer only when six repository secrets are set. Without them it still drafts the release with the unsigned zip, and its log says `Signing skipped: Developer ID secrets not set`. This page is the one-time setup that turns signing on.

You need a Mac with Keychain Access and an Apple Account.

## 1. Join the Apple Developer Program

Enrol at <https://developer.apple.com/programs/enroll/> (paid yearly; as an individual or as an organisation). Signing certificates and notarisation need an active membership.

Your **Team ID** is a 10-character code shown under Membership details at <https://developer.apple.com/account>. It is the `APPLE_TEAM_ID` secret.

## 2. Create the two Developer ID certificates

Only the Account Holder of the team can create Developer ID certificates.

1. On your Mac, open Keychain Access and choose Keychain Access > Certificate Assistant > Request a Certificate From a Certificate Authority. Enter your email address, choose **Saved to disk**, and save the `.certSigningRequest` file.
2. At <https://developer.apple.com/account/resources/certificates/add>, choose **Developer ID Application**, upload the request, then download the certificate and double-click it to add it to your login keychain.
3. Repeat step 2 (a new request is fine, or the same one) choosing **Developer ID Installer**.

Keychain Access > login > My Certificates now lists `Developer ID Application: <name> (<Team ID>)` and `Developer ID Installer: <name> (<Team ID>)`, each with its private key under it. The plugins are signed with the first, the `.pkg` with the second.

## 3. Export each certificate as a .p12

1. In Keychain Access > My Certificates, select `Developer ID Application: …` (the certificate with its private key, not the key alone), choose File > Export Items, and save it as `developer-id-application.p12` in the Personal Information Exchange (.p12) format. Choose a strong password.
2. Do the same for `Developer ID Installer: …` as `developer-id-installer.p12`, **with the same password** (the workflow uses one password for both).
3. Turn each file into one line of base64:

   ```
   base64 -i developer-id-application.p12 | tr -d '\n' | pbcopy   # paste into MACOS_DEVELOPER_ID_APP_P12
   base64 -i developer-id-installer.p12 | tr -d '\n' | pbcopy     # paste into MACOS_DEVELOPER_ID_INSTALLER_P12
   ```

Keep the `.p12` files somewhere safe (a password manager), or delete them once the secrets are set: anyone with a file and its password can sign as you. You can revoke a certificate at the developer site.

## 4. Make an app-specific password for notarisation

Notarisation (`notarytool`) signs in with your Apple Account and an app-specific password, not your real password.

1. Sign in at <https://account.apple.com>, open Sign-In and Security > App-Specific Passwords, and create one named, for example, `EG13 notarytool`.
2. Copy the password (`xxxx-xxxx-xxxx-xxxx`). It is the `APPLE_APP_PASSWORD` secret; your Apple Account email is `APPLE_ID`.

The Apple Account must be a member of the team from step 1.

## 5. Add the six secrets

On GitHub, open the repository's Settings > Secrets and variables > Actions, and add these **repository secrets** (New repository secret):

| Secret | Value |
| --- | --- |
| `MACOS_DEVELOPER_ID_APP_P12` | the base64 of `developer-id-application.p12` (step 3) |
| `MACOS_DEVELOPER_ID_INSTALLER_P12` | the base64 of `developer-id-installer.p12` (step 3) |
| `MACOS_CERT_PASSWORD` | the password of both `.p12` files |
| `APPLE_ID` | the email address of your Apple Account |
| `APPLE_TEAM_ID` | your Team ID (step 1) |
| `APPLE_APP_PASSWORD` | the app-specific password (step 4) |

Or with the GitHub CLI, from the folder holding the `.p12` files:

```
gh secret set MACOS_DEVELOPER_ID_APP_P12 --repo SymbioticAudio/EG13 < <(base64 -i developer-id-application.p12 | tr -d '\n')
gh secret set MACOS_DEVELOPER_ID_INSTALLER_P12 --repo SymbioticAudio/EG13 < <(base64 -i developer-id-installer.p12 | tr -d '\n')
gh secret set MACOS_CERT_PASSWORD --repo SymbioticAudio/EG13
gh secret set APPLE_ID --repo SymbioticAudio/EG13
gh secret set APPLE_TEAM_ID --repo SymbioticAudio/EG13
gh secret set APPLE_APP_PASSWORD --repo SymbioticAudio/EG13
```

(the last four prompt for the value). All six must be set; with any one missing, signing is skipped.

## What happens on the next tag

On a `v*` tag the workflow then, after the build, tests and validators:

1. imports the Developer ID Application certificate into a temporary keychain (with the notarytool credentials, stored there as a keychain profile), signs `EG13.component`, `EG13.vst3` and `EG13.clap` with the hardened runtime and a secure timestamp, sends them to Apple's notary service and waits (usually a few minutes), and staples the tickets to the bundles (`scripts/sign-and-notarize.sh`);
2. runs the plugin validators again on the signed bundles, then zips them (`scripts/package-release.sh`);
3. builds `EG13-<version>-macOS.pkg`, which installs to `/Library/Audio/Plug-Ins/{Components,VST3,CLAP}`, signs it with the Developer ID Installer certificate, notarises and staples it (`scripts/build-pkg.sh`);
4. hands the zip and the `.pkg` to a separate job, the only one allowed to write to the repository, which attaches them to the draft release.

The temporary keychain is deleted at the end of each step. If notarisation is rejected, the step fails and prints Apple's log; nothing is attached. Once signing works, say so in the release notes and in `docs/manual/install.md` (the quarantine step is then not needed).

To check the signing helpers offline (with `security` and `xcrun` stubbed out), run `/bin/bash scripts/sign-and-notarize.sh --self-test`. To see the commands without signing anything, run the script with fake values:

```
MACOS_DEVELOPER_ID_APP_P12=x MACOS_DEVELOPER_ID_INSTALLER_P12=x MACOS_CERT_PASSWORD=x \
APPLE_ID=me@example.com APPLE_TEAM_ID=ABCDE12345 APPLE_APP_PASSWORD=x \
  scripts/sign-and-notarize.sh --dry-run build/mac/plugins
```

## Renewing

Developer ID certificates last five years. When one is renewed, export it again (step 3) and replace its secret. If you revoke an app-specific password, make a new one and replace `APPLE_APP_PASSWORD`.
