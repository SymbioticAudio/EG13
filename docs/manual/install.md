# Installing EG13

EG13 runs on macOS 14 (Sonoma) or later, on Apple silicon and Intel Macs. It comes as an AU, a VST3 and a CLAP plugin; each holds both EG13 (the instrument) and EG13 FX (the audio effect). There is no standalone app.

## From the zip

The release zip, `EG13-<version>-macOS.zip`, holds `EG13.component`, `EG13.vst3` and `EG13.clap`, this guide as `INSTALL.txt`, the licence (`LICENSE`), the credits and licences of the code EG13 is built from (`THIRD-PARTY-NOTICES.md`) and where to find the source code (`SOURCE.txt`).

1. Open the zip (double-click it in Finder).
2. Copy each plugin you want into its folder in your home folder's Library. In Finder, choose Go > Go to Folder and enter the path:
   - `EG13.component` into `~/Library/Audio/Plug-Ins/Components`
   - `EG13.vst3` into `~/Library/Audio/Plug-Ins/VST3`
   - `EG13.clap` into `~/Library/Audio/Plug-Ins/CLAP` (create the `CLAP` folder if it is not there)
3. Clear macOS's quarantine (below).
4. Rescan plugins in your host (below).

To update, replace the old copies with the new ones. To uninstall, delete the three files.

## Clearing the quarantine

Unless the release says it is signed and notarised, macOS has not checked the plugins and blocks them the first time a host loads them. Clear the quarantine in one of two ways:

- **System Settings:** start your host so that it tries to load EG13. macOS reports that it could not verify the plugin; choose Done. Open System Settings > Privacy & Security, scroll to Security, and choose Open Anyway next to the message about EG13. Confirm with your password, then restart the host. Repeat for each format you installed.
- **Terminal:** run this command once; it clears all three plugins at once (a plugin you did not install is reported as missing, which is harmless):

  ```
  xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/EG13.component ~/Library/Audio/Plug-Ins/VST3/EG13.vst3 ~/Library/Audio/Plug-Ins/CLAP/EG13.clap
  ```

## Rescanning in your host

Most hosts scan for new plugins when they start, so quit and reopen the host after installing. If EG13 still does not appear:

- **Logic Pro** (and GarageBand) use the AU. Open Logic Pro > Settings > Plug-in Manager, select EG13 and choose **Reset & Rescan Selection**. If it is listed as failed, clear the quarantine (above) and rescan again. Restarting the Mac also makes macOS register the AU again.
- **REAPER** uses the AU, VST3 or CLAP. Open REAPER > Settings > Plug-ins > VST and choose **Re-scan** (or **Clear cache/re-scan**); the CLAP page under Plug-ins has the same button. A plugin REAPER failed to load before the quarantine was cleared is remembered: use Clear cache/re-scan.
- **Bitwig Studio** uses the VST3 or CLAP (it does not load AU). Open Settings > Locations and rescan the plug-in locations, or restart Bitwig.

EG13 appears as **EG13** (an instrument) and **EG13 FX** (an audio effect) from the manufacturer **SymbioticAudio**.

## Playing EG13 FX in Logic Pro

EG13 FX processes audio and also plays its parts from MIDI. In Logic Pro there are two ways to set it up:

- **On an audio track (or bus):** insert EG13 FX as an **Audio FX**; it processes that track. To play its parts from MIDI as well, use Logic's MIDI-controlled effect route: in EG13 FX's plug-in header, choose a software instrument track in the MIDI side chain menu, then play that instrument track.
- **On a software instrument track:** in the **Instrument** slot choose **AU MIDI-controlled Effects > SymbioticAudio > EG13 FX**, then set the **Side Chain** menu in the plug-in window's header to the audio track or bus to process. With Side Chain set to **None**, Logic gives the plug-in no input and it is silent, even the parts that play from MIDI alone.

The instrument (EG13) has no audio input; to process audio, use EG13 FX (see [Parts and routing](parts-and-routing.md)).

## From the installer (.pkg)

When a release is signed and notarised, it also has an installer, `EG13-<version>-macOS.pkg`. It installs the three plugins for all users of the Mac, in `/Library/Audio/Plug-Ins/Components`, `VST3` and `CLAP` (the Library folder at the top of the disk), and needs no quarantine step.

If you installed EG13 from the zip before, delete that copy from `~/Library/Audio/Plug-Ins/` first: with two copies installed, a host may load the old one. To uninstall, delete `EG13.component`, `EG13.vst3` and `EG13.clap` from the folders in `/Library/Audio/Plug-Ins/`.

## Source code

EG13 is free software under the GNU General Public License, version 3. `SOURCE.txt` in the zip gives the address of the source code of that release.
