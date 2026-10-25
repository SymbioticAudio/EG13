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
