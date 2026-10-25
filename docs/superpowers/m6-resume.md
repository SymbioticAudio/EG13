# M6 UI — resumed (2026-10-24)

Resumed as M6a (foundation and part editor): see `docs/superpowers/plans/2026-10-24-m6a-ui-foundation.md`. Spec §8 now holds the decisions below. The notes below are kept as history.

## Original pause note (owner, 2026-10-20)

UI work is paused until M6 comes up in normal milestone order, after M5. The plan has not been written yet. These notes hold the state to resume from.

## Owner decisions (supersede the "always dark" spec commit 8e5334b)

- Layout: "Columns + motion chips". The main view shows one Part, picked with the A/B tabs. Column cards run Source, Filter, Shaper, Part FX, then a MODS column with the matrix below it. A bottom bar holds the part settings, master Dry/Out and the meters. A collapsible keyboard sits underneath. Filter, Shaper, Part FX and Mods are placeholder cards until M3–M5.
- Palette: EG13 owns its colours (family rule: plugins share icons, type, layout and knob design; colours are per plugin). The palette is modelled on Mutable Instruments panels:
  - two accents: teal #00A1A7 (ink #007F8A) and pink-red #DA4061 (ink #C42C4E);
  - panels #E6E6E6 with a #D5D5D4 header strip and a lowercase engine title;
  - rack grey #333333 for the header and keyboard bar;
  - LCD readouts #383838 with #AFD22C;
  - window mid-grey #8C8C89;
  - Part A is teal, Part B is pink. Modules are not colour-coded.
- Knobs are flat with no ticks. The value arc sits inside the black skirt band over a faint track. Caps are white, accent or black. Emphasised parameters get accent label chips. Style options: arc on/off and placement, inner ring, ticks, cap style, label style, injected colours.
- Controls: custom EG13 SwiftUI controls with drawing kept separate from behaviour. AudioKit Controls is dropped. AudioKit Keyboard stays only if it can be styled to match. Whether the controls move into a shared SymbioticControls package is the owner's call.
- Dependencies approved: symbiotic-design from 0.1.0 (Tokens and Icons; Brand behind `#if canImport`).
- Approved mock (private to the owner): https://claude.ai/artifact/Dao1y9KMKtEV41Pvd5fwL3

## Spike code (unreviewed and incomplete)

`docs/superpowers/m6-spike/` holds the planning agent's experiments: a SwiftPM EG13UI package linked into a C++ bundle, a core-api C header and an SPSC queue test. Use it as research input for the M6 plan, not as finished code.

## To resume

1. Rebase this branch onto main after M2 to M5 have merged.
2. Update spec §8 with the decisions above.
3. Write the M6 plan with superpowers:writing-plans, then get owner review.

## Design log, round 1 (2026-10-24): controls

Run: `swift run --package-path ui/EG13UI EG13UIPreview` (interactive window, Controls page by default; `--editor` opens the editor page; the zoom control shows 75, 100 and 200 %). Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot docs/superpowers/m6-design/controls-r1` writes `controls-75.png`, `controls-100.png` and `controls-200.png` in that folder (ImageRenderer, no screen-recording permission). Each image shows Part A (teal) and Part B (pink).

Shown, every option labelled with its `EG13KnobStyle` name:

- Arc: off; on in the skirt band over a faint track (`.skirtBand`, the default); a ring outside the skirt (`.outside`); a thin ring hugging the cap (`.capRim`).
- Inner ring: off, on. Ticks: off, on.
- Cap: white (default), accent, black with an accent arc, black with the grey arc the master knobs use.
- Label: plain (default), chip; also chips on an accent cap and on a black cap.
- Sizes: hero, detail and bar knobs, a bipolar knob (arc from the centre) and a stepped knob.
- Engine picker (menu of engine names) and three stepped controls (voices, voice mode, channel).

Open choices for the owner:

1. Arc placement: `.skirtBand`, `.outside` or `.capRim`; or arc off.
2. Inner ring: yes or no.
3. Ticks: yes or no (they cost 8 % of the diameter, so the skirt shrinks).
4. Cap colours: white as the default with accent caps for emphasised knobs, and black caps for secondary knobs. Should a black cap keep the part's arc colour or the grey arc?
5. Label style: plain everywhere, or chips on emphasised knobs only (the current palette rule).
6. Stepped controls: the LCD well with minus and plus, as shown, or knobs for small stepped settings such as voices.
7. Engine picker: black well with a menu, as shown, or something lighter.
8. Whether chip ink colours (`#007F8A`, `#C42C4E`) read well under white text at 75 %.

Status: awaiting owner feedback (round 1 is not approved).

## Design log, round 2 (2026-10-24): controls

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot docs/superpowers/m6-design/controls-r2` writes `controls-75.png`, `controls-100.png` and `controls-200.png`.

Owner decisions from round 1:

1. Arc on, in the skirt band, only. The other placements stay in the code as unused options and are not shown.
2. Ticks off. The tick variant is gone from the Controls page.
3. Caps: white is the default, the accent cap marks emphasised knobs, and the black cap marks secondary knobs. A black cap keeps the part-colour arc, never grey (master Dry and Out follow the shown part's colour).
4. Labels: plain by default, with the coloured accent chip on emphasised knobs.
5. Stepped controls keep the minus and plus buttons.
6. The engine picker uses the LCD look of the other wells (#AFD22C on #383838).
7. The chip ink colours are fine.

Changed in round 2:

- Inner ring: now a thin mid-grey ring (the window grey, `#8C8C89`) on the cap's edge, 1.5 pt or 3 % of the diameter, drawn inside the cap. It was a faint ring on the skirt that could not be seen.
- LCD wells: bold, at least 12 pt (was medium, 11 pt). The palette colours are unchanged; no brighter green was needed.
- The Controls page shows the approved defaults (default, emphasised, secondary), the inner ring on and off for white and black caps, sizes (hero, detail, bar, bipolar, stepped), the engine picker and the steppers, in both part accents.

Open question for the owner: inner ring on or off. It is visible on black caps and subtle on white ones; compare the second row.

## Round 3 / approval (2026-10-24): controls approved by the owner; inner ring dropped

The owner approved the round 2 controls ("it all looks pretty good") with one change: drop the inner ring entirely, because it could not be seen on white or black caps. `innerRing` is removed from `EG13KnobStyle` and the drawing. Screenshots: `docs/superpowers/m6-design/controls-r3/` (75, 100 and 200 %).

Final choices:

- An arc in the skirt band over a faint track, with no ticks and no inner ring.
- A white cap by default.
- Emphasis shown by an accent cap with an accent chip.
- A black cap with the part-colour arc for secondary knobs.
- Minus and plus steppers.
- The engine picker drawn as an LCD (#AFD22C on #383838).
- Bold LCD text, at least 12 pt.

## Layout design log, round 1 (2026-10-25): the one-part editor

Run: `swift run --package-path ui/EG13UI EG13UIPreview --editor` (the editor in a window over a fake plugin with the plugin's whole parameter table and the real engine and P names). Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-editor docs/superpowers/m6-design/layout-r1` writes the files below to that folder (ImageRenderer, so the engine menus show their closed state).

Shown:

- `editor-a.png`, `editor-b.png`: the editor at 100 % for Part A (teal) and Part B (pink). `editor-a-75.png`: Part A at 75 %.
- `part-settings.png`: the Part settings popover content, for both parts.
- `source-engines.png`, `filter-engines.png`, `shaper-engines.png`, `partfx-engines.png`, `mods-engines.png`: that slot's card for every engine (Part A colours; the mods sheet shows the mod 1 card).
- Layout, top to bottom: header (EG13 name, A/B tabs, Mode, master settings, About); Source, Filter, Shaper and Part FX cards (four knobs to a row) with the MODS column (four compact mod cards, eight knobs to a row); the matrix row (Route 1 to 12); the bottom bar (part Level, Pan, Glide, Voice Mode, Voices; Part settings; master Dry and Out; a stereo meter per part).
- The controls are the approved round 3 controls. Every parameter of the shown part and the master is reachable from exactly one control; `EG13LayoutTests` checks it for every engine of every slot, on both parts.

Decisions for the owner to see:

1. **No keyboard in M6a.** The mock's collapsible keyboard is left out: the core API has no note input yet, so it moves to M6b with the live modulation display (spec §8 split). The toggle strip is gone.
2. **Peak-hold mark on the meters** (added in the Task 8 model, drawn here): a thin white line at the highest recent peak for 1.5 s. A bar turns white at 0 dBFS or above (pink was the clip colour, but pink is Part B's colour).

Open choices:

1. **Which knobs are emphasised** (accent cap + chip). Proposal: the first two P's of every Source, Filter, Shaper and Part FX engine (teal, then pink, in both parts), and the part Level in the bottom bar. Secondary (black cap, part-colour arc): master Dry and Out and the 12 route amounts. Mod knobs stay white. The first two P's are often a model or mode selector (Macro Oscillator's P1 is Model); a per-engine choice (the old table named Harmonics and Timbre for Macro Oscillator) is possible and is data only (`EG13Emphasis`).
2. **Long knob names.** Four knobs per row at 1200 pt leaves about 45 pt a knob, so names such as Brightness, Structure or Smoothness shrink to 65 % and are then cut (`BRIGHTNE...`); on a chip (emphasised knob) the cut is worse. The full name is in the tooltip and for VoiceOver. Options: narrow the MODS column to widen the cards; give Filter and Shaper narrower cards and Source and Part FX five knobs to a row; or emphasise only knobs with short names.
3. **Mod cards are dense.** To fit 16 knobs in two rows in a quarter of the strip, mod knobs are 28 pt with no value text (tooltip shows it). The alternative is a taller strip by dropping the matrix row or the bottom bar's height.
4. **Master settings.** Split Note, Voice Gain, Soft Clip and Latency Mode have no place in the mock's header or bottom bar, so they are in a popover behind the cog next to About, with the window size choices. Spec §8 puts the Latency Mode in settings in M6b; it is here already so that all 7 master parameters are reachable.
5. **Mode** is four LCD segments (green on dark) instead of the system segmented control, which cannot be drawn in the screenshots and did not match the LCD look.
6. **Header.** The patch name with previous, next and save (presets are M7; the buttons did nothing) is gone. The wordmark is the SymbioticAudio brand logo and "EG13" when the brand package is linked, otherwise just "EG13". Each part tab names the part's Source engine.
7. **Inner gaps in an engine's P range are hidden** too (the LFO's unused P8 and P9), so the knobs pack to the left.
8. **Matrix:** only the amounts are knobs. Route sources and destinations keep their init-patch values until M6b.
9. **Control tweaks:** the stepper well is 88 pt wide (was 64) so "Per voice", "Other part" and "Part mix" fit; knob names may shrink to 65 %; a knob can take a label, a smaller diameter and no value text (`EG13KnobLayout`). The knob drawing is unchanged.

Status: awaiting owner feedback (round 1 is not approved).
