# EG13 M7: Release — design

**Date:** 2026-10-27
**Status:** brainstormed with the owner, pending written review
**Parent spec:** `docs/superpowers/specs/2026-10-20-eg13-design.md` (§7.3 Presets, §10 Testing and CI, §11 Milestones). Where this document and the parent differ, this one is newer and wins for M7; the parent's §7.3 and §11 are updated to match when M7's plan lands.

## 1. Intent

M7 turns EG13 from a working instrument into something a stranger can download, install and enjoy. The owner's picture of success is the first five minutes: someone loads EG13 for the first time and it is already playing "some delightful evolving ambient patch, two parts, that has ring mod, granular textures". Everything else in M7 (presets, browsing, install, docs) serves that first impression and what follows it.

M7 also carries EG13's reusable design work back to the shared design library, so the owner can use the cap and display styles in other plugins, even with different colour schemes.

## 2. Goals

1. **First-load patch.** A new instance of EG13 (the instrument) loads an original two-part ambient patch, not the bare init patch. It evolves on its own when a note is held, and uses ring modulation (the Ring Mod Shaper, the Warps port) and granular texture (Granular or Granular Delay Part FX, the Clouds and Beads ports). It is the first preset in Bank 1. The current init patch stays available as "Init" (§4.3).
2. **Factory presets: two banks.**
   - **Bank 1:** 30–50 original presets (owner), including the first-load patch. It mixes a few A+B combinations with single-part sounds (§4.1). Built from the patch ideas in the original modules' manuals and inspired by ambient patches shared by the VCV Rack community (the VCV forum threads, and "Melodic Ambient Patching" on Patchstorage). These are new patches that take ideas, not files. Each preset carries a one-line note naming the idea it comes from.
   - **Bank 2: catnap.** A rebuild of "~zero-g:catnap : vcv rack jam" by **plantay** (patchstorage.com/zero-gcatnap-vcv-rack-jam/, licence *Creative Commons Attribution 4.0*, confirmed on the page 2026-10-27), with attribution (owner).
3. **Preset browsing in the editor header** (owner): previous/next arrows, and a dropdown between them showing the current preset name and listing every preset grouped by bank (Bank 1, Bank 2, User). A single-part preset can be loaded into A or B (owner). *Owner redesign 2026-10-27 (see §4.3): the arrows are removed; a "Presets" button opens the dropdown and each part's name is shown under the header (round 2: a name strip).*
4. **User bank** (owner): users can save their own presets into a User bank in the same list.
4a. **EG13 FX** (owner): the same browser, with a small FX bank of its own.
5. **Signing and notarisation, switchable** (owner: "middle path"). The release pipeline can sign, notarise and build a `.pkg` installer, but this switches on only when Apple Developer ID credentials are added to the repository's GitHub secrets. Without them, the pipeline makes unsigned builds. The owner decides at release time, or in a later release, whether to pay for the Apple Developer Program (US$99/year).
6. **Distribution through GitHub Releases** (owner): for now, a zip that people unpack and drag into their plug-in folders. Once signing is on, a `.pkg` installer is offered instead.
7. **Docs** ready for strangers: the README as a front page, an install guide, a presets page with credits, and release notes.
8. **Design library contributions** (owner): EG13's reusable design choices go to symbiotic-design as requests in its `HANDOFF.md`, for the owner to review and include where appropriate.
9. **Tag v1.0.0.**

## 3. Non-goals for v1.0 (owner decisions, 2026-10-27)

- **Undo for route source, via and destination changes:** ships without it, added in a later update. `docs/manual/limitations.md` keeps the note.
- **Colour-blind palette option:** after v1.0 ("good practice").
- **Windows, Linux, AU v3, iPadOS:** later, per the parent spec §1.
- **Tags and search in the browser:** later, per the parent spec §7.3.
- **Mandatory signing:** v1.0 may ship unsigned.

**Anti-goals:**
- A first sound that is the bare init patch, or a patch that doesn't move.
- Presets copied from sources whose licence doesn't allow it, or credited carelessly. Only CC-licensed material is rebuilt, and always with credit.
- "Mutable Instruments" in the product name, UI or marketing (parent spec §1.1). Credits are factual: "DSP based on the open-source Eurorack firmware by Émilie Gillet."

## 4. Presets

### 4.1 Two kinds of preset (owner, 2026-10-27: "both")
There are two kinds of preset, as in the parent spec §7.3:
- **Performance** (`.eg13perf`): both parts, the mode, and the master settings that a preset carries (§9). These are the "combination patches A+B".
- **Patch** (`.eg13patch`): one part's sound. It loads into A or B, chosen by the user, and leaves the other part alone.

Bank 1 has "a few combination patches A+B as well as single-part sounds" (owner). The first-load preset is a Performance. The builder proposes the split between the two kinds; Claude's call is about a third Performances.
- The preset format is EG13's existing state JSON (with `schemaVersion`), plus metadata:
  - name;
  - bank;
  - a short note (the idea's origin);
  - credit and licence, where relevant;
  - the preset format version.
- Factory presets ship inside the plugin bundle and are read-only.

### 4.2 Making the presets
- Claude drafts each preset by translating its idea into engine choices, P values, mod engines and routes, then renders it to audio so the owner can listen.
- The owner listens and adjusts by ear. **No preset ships until the owner has heard it.**
- The catnap rebuild reads the patch file itself (`JAM-Fixed-Mutable-Instruments-Jam-1-666c1d2bca1e1.vcv`) for module settings. It maps catnap's modules onto EG13's slots:
  - Marbles, Tides and Stages to the mod slots;
  - Plaits and Rings to the Sources;
  - Ripples to the Filter;
  - Warps to the Shaper;
  - Clouds to the Part FX;
  - the Plateau reverb has no EG13 equivalent and is left out (or approximated with the Elements reverb Part FX, the builder's call, to be noted in the preset).
- catnap has more modules than EG13 has slots in one part. Bank 2 holds the closest full rebuild across both parts, plus a few variations, each keeping a different side of the patch. The builder proposes the set; the owner approves it by ear.
- Each factory preset gets a golden render test (parent spec §10).

### 4.3 Browsing (header)
**Owner redesign, 2026-10-27** (after testing in Logic: stepping from a Performance into a Patch left the other part sounding, and nothing showed what was in Part B). This replaces the arrows and the name display below; the rest of §4.3 stands.
- No ◀ ▶ arrows, in either plugin. One LCD button labelled **Presets ▾** only opens the dropdown (same content and behaviour; Init, Save, Save As, Rename and Delete stay in it).
- *(Round 1, superseded by round 2 below: the names now sit in a name strip, not in the tabs.)* Each part tab shows what is loaded into its part instead of the Source engine name: a Patch's name; a Performance as `name·A` / `name·B`; otherwise `Init`. A `*` after a tab's name marks that part edited (a part's controls and routes mark it; a master control that is part of the sound marks both; setup controls never). Long names are cut with "…", full name (and a credit) on hover. The MASTER group is never squeezed.
- The dropdown ticks what A and B hold (a Performance once).
- **Round 2 (owner, 2026-10-28):** the names move out of the tabs into a slim name strip (24 pt) under the header row: one half per part, letter chip + name + `*`, the shown part's half filled like its tab, a click shows that part. The tabs are compact letters. The window stays 1200 × 760; the card strip gives up the 24 pt.
- The browser string is version 2 with `"edited":[a,b]`; any other string reads as Init (no migration before v1.0).

Original M7 design (superseded where the redesign says so):
- **Layout:** `◀  [ preset name ▾ ]  ▶` in the header's preset area (parent spec §8). The visual design goes to the owner as a design gate with screenshots, as in M6a and M6b.
- **Kinds in the list:** each entry shows a small mark for its kind, Performance (A+B) or Patch (one part).
- **Loading a Patch:** the user chooses A or B (owner).
  - A Patch row in the dropdown is a submenu, "Load into A" and "Load into B" (owner-approved at gate 5a, 2026-10-27: a macOS menu row cannot both load on click and open a submenu). Clicking the row itself does not load.
  - The arrows load a Patch into the part currently shown in the editor (the A/B tab).
- **Arrows:** step through the presets in list order and run on across bank boundaries. A Patch loads into the part currently shown. Claude's call: they wrap from the last User preset to the first Bank 1 preset.
- **Name display:** the name shown is the last preset loaded. When a Patch is loaded into one part, the header shows the part's name too, for example "A: Glass Bells". Claude's call: the header shows "A: … / B: …" when the two parts came from different Patches.
- **Dropdown:**
  - presets grouped under Bank 1, Bank 2 and User headings, with the current preset ticked;
  - then "Init", which loads the init patch;
  - then Save, Save As…, Rename… and Delete…, which apply to User presets only.
- **Edited state:** after any edit to a loaded preset, the name shows an edited mark (Claude's call: a trailing `*`) until the preset is saved or another is loaded.
- **Saving a factory preset:** factory presets are read-only, so Save on one acts as Save As into User (Claude's call).
- **Save As:** asks whether to save the whole instrument (Performance), Part A, or Part B (Patch).
- **Persistence:** the host's project state remembers which preset was loaded and whether it was edited.
- **Errors:** a preset that fails to load never stops audio. The sound and the name stay as they were, and the UI shows a message saying why (parent spec §9).
- **The FX plugin:** EG13 FX has the same browser (owner).
  - The FX bank is a small set of effect presets of its own (owner), for example granular washes and ring-mod treatments. Claude's call: about 8 to 12, reviewed by ear like the others.
  - EG13 FX lists only the FX bank and the user's FX presets, which are saved to a `FX/` subfolder of the Presets folder (Claude's call).
  - Instrument presets aren't offered in EG13 FX, and FX presets aren't offered in the instrument.
  - EG13 FX keeps its current init patch as its first-load state (Claude's call).

### 4.4 User bank
- **Storage:** one file per preset in `~/Library/Application Support/SymbioticAudio/EG13/Presets/` (parent spec §7.3).
- **Discovery:** the list is read from that folder when the dropdown opens, so a file dropped in from someone else shows up.
- **Sorting:** User presets are sorted by name (Claude's call).
- **Bad files:** unreadable files are skipped and listed in a message, not fatal.

## 5. Release and distribution

### 5.1 Pipeline
- **Trigger:** pushing a tag `vX.Y.Z` runs a release workflow. It builds the universal plugins (AU, VST3 and CLAP, instrument and FX), runs the validators and drafts a GitHub Release with the zip attached.
  - The owner writes or approves the release notes and publishes the draft. CI never publishes.
- **Signing switch:**
  - When the secrets are set, the same workflow:
    - signs with Developer ID Application;
    - notarises with notarytool;
    - staples the result;
    - builds a signed `.pkg` (Developer ID Installer) that installs to `/Library/Audio/Plug-Ins/…`.
  - When the secrets are missing, signing is skipped with a clear log line, not a failure.
  - The secret names and the setup steps are documented for the owner.
- **Zip contents:** the plugins, a short `INSTALL.txt`, `LICENSE` and the credits.
- **Version:** one source of truth for the version number, also shown in the About box (M6b) and in the plugin descriptors.

### 5.2 Install guide (unsigned)
- Drag each plugin into `~/Library/Audio/Plug-Ins/Components`, `VST3` or `CLAP`.
- Clear macOS's quarantine. There are two documented ways:
  - the System Settings approval;
  - one Terminal command.
- Rescan in the DAW. Logic users may need to reset the plugin manager.

## 6. Docs
- **README:** a front page with what EG13 is, a screenshot, the download link, the formats and system requirements, a link to the manual, the credits and the licence.
- **docs/manual:**
  - a new `install.md`;
  - a new `presets.md` covering the banks, the User bank, browsing, the credit line for each source, and catnap's full attribution and licence;
  - `index.md` updated;
  - `limitations.md` updated with the v1.0 non-goals.
- **Release notes for v1.0.0:** drafted by Claude, approved by the owner.

## 7. Design library contributions
- **Where:** requests are added to `~/workspace/symbiotic-design/HANDOFF.md` under "Requests from Mutable". The library's own session or the owner decides what to include. EG13 never forks tokens or icons (standing rule).
- **The candidates:**
  - **Knob:** the cap and skirt band, including the cap emphasis colours as slots, not fixed colours;
  - **Modulation wave mark on the cap:**
    - size: 45% of the cap width, 1.5 cycles;
    - colour: contrasting ink;
    - position: the bottom third when the pointer is up, the top third when it is down;
  - **LCD-style readout and button**, including the `MOTION n` pattern and its popover with level bars;
  - **Mini trace display:** min–max bars, refreshed at 15 Hz, going quiet when idle;
  - **Card-column layout:** columns end on one line, the shorter column spreads its gaps, the bottom bar floats one gap below, spare space collects at the window bottom.
- **What each request contains:**
  - the measured values;
  - a screenshot at 100% and 200%;
  - which values are palette slots, so other plugins can recolour them;
  - a pointer to the EG13 source file.
- **When included:** once the library ships a component, EG13 switches to it in a later change, so the two never drift apart.

## 8. Also in M7
- **Clean-up:** the minor findings deferred from the M6b reviews. The builder chooses which are worth fixing; anything left goes into `limitations.md` or the issue tracker.
  - **UI bridge and modulation publishing:**
    - UiBridge: the `detached_` comment overstates what it protects;
    - a drain request doesn't set `pumpPending_`, which causes harmless duplicate pumps;
    - there's no two-thread or TSan test, and no UiBridge-path test for `dropHostUpdate`;
    - a voice that has been released freezes its published modulation values;
    - unused P's publish finite values;
    - no test that a per-part slot publishes with no notes playing.
  - **Routes:** `route_set` re-validates the unchanged source and via against the current engines. As a result, a route with a field the current engine hides can't be re-saved with only one field changed. There's also no test for an open destination menu during an engine change.
  - **Generated UI tables:**
    - the table strings are exposed through libm on x86_64;
    - `tools/ui_tables` builds unconditionally;
    - the ctest failure shows no diff.
  - **UI:**
    - the cap radius is defined twice;
    - the reset-pending state allocates at 15 Hz;
    - A0 and C8 can only be reached on the key-range strip through the steppers.
  - **Settings:** a failed write is reverted silently; the `EG13_SETTINGS_PATH` override isn't guarded out of release builds; there's no fsync before the rename.
- **Owner host checks before tagging:** the M6b hands-on checks (PR #5 description) and a play-through of the first-load patch in Logic and in Reaper or Bitwig. Claude reminds the owner before the tag.

## 9. Builder's calls (Claude, from the builder check; owner may override)
- **What a preset overwrites:** a preset carries the sound, not the user's setup.
  - **Loaded:**
    - both parts' slots, engines, P's, scopes and routes;
    - part level, pan, glide, transpose and base note;
    - voice mode and voice count;
    - trigger, bend range and Part FX input;
    - mode and split note;
    - key ranges and Dry level;
    - Soft Clip.
  - **Kept from the current state:**
    - Latency Mode (it needs a host restart);
    - each part's MIDI channel and output bus;
    - MPE zone and MPE bend range;
    - CC A–D;
    - Voice Gain;
    - master output level.
  - Documented in `presets.md`.
- **Init and reset to default:** a knob's reset still goes to the init patch value (the descriptor defaults), and "Init" is the descriptor defaults plus the parent spec's init routes. Only a *new instance* opens on the first-load preset. Existing state and golden tests keep Init as their baseline.
- **Edited mark:** set only by changes made in EG13's editor, including loading a different preset into a slot. Host automation and host-side edits don't set it, since hosts can't tell automation apart from user edits.
- **Install locations:** the zip guide uses `~/Library/Audio/Plug-Ins/…`. The `.pkg` installs to `/Library/Audio/Plug-Ins/…`, as is standard for installers. Its readme, and the install guide, tell users to delete a zip-installed copy first.
- **Repository visibility:** the GitHub repo is already public (its API answers without authentication). GPL-3.0 source availability is met by the public repo and tag. The zip includes `LICENSE` and a line pointing to the source at that tag. M7 includes a licence audit of the shipped binaries: the submodules, the VST3 SDK, and the fonts and icons from symbiotic-design (the SymbioticBrand artwork is all rights reserved and must not ship; release builds use the MIT standard icons unless the owner says otherwise).
- **catnap source file:** downloaded from Patchstorage (permitted by CC BY 4.0) into a git-ignored folder, used as reference only, and not committed.
- **The owner's by-ear round trip:** User-bank Save lands before preset authoring. The owner adjusts a draft in a DAW and saves it as a User preset (or sends notes), and Claude promotes the file into the factory bank. Presets are reviewed in batches of about 10.
- **symbiotic-design requests:** Claude adds them to `HANDOFF.md` in `~/workspace/symbiotic-design`, on a branch there, and commits. Pushing that branch needs the owner's approval, as in this repo.

## 10. Owner answers to the builder check (2026-10-27)
1. **Patches and Performances:** both. The dropdown can load a single part into A or B. The presets include a few A+B combinations as well as single-part sounds (§4.1, §4.3).
2. **EG13 FX:** the same browser, with a small FX bank of presets (§4.3).
3. **Clouds Stretch:** fix it in our build (2026-10-27). `Window::Start()` had lost its `done_ = false` upstream, which left Stretch silent; the line is restored in a shadow copy, `third_party/eurorack-patches/clouds/dsp/window.h`. The bare-module reference for Stretch in the fidelity tests includes this fix, and the adapters stay bit-exact against it.
