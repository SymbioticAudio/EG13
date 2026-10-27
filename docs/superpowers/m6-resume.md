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

## Layout design log, round 2 (2026-10-25): horizontal module cards

Run: `swift run --package-path ui/EG13UI EG13UIPreview --editor` (add `--variant-b` for B; the page has an A / B switch). Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-editor docs/superpowers/m6-design/layout-r2` writes `editor-a-A.png`, `editor-b-A.png`, `editor-a-75-A.png` (variant A), the same three ending `-B`, `editor-a-mods-B.png` (B's Mods page), `master.png` (header close-up), `part-settings.png`, `worst-case-A.png` (the tallest engines in every slot, to show A's scroll) and the five `*-engines.png` contact sheets (every engine of a slot, one card under another, so the varying heights show).

### Owner's round-1 feedback, as decisions

1. The layout was crowded and small, with unused space and cut names. Fixed by horizontal cards and short labels that never cut.
2. Q1, emphasis: match the real modules (the VCV Rack Audible Instruments replicas), and the emphasis colour is always the shown part's accent. This is review finding I1 (P1 teal and P2 pink were hard-coded); fixed in this commit: `EG13ParamEmphasis.accent` has no colour, and `EG13Palette.knobStyle` resolves it to the shown part's accent. Test: `emphasisColourIsPartAccent`.
3. Q4: the master settings move to the header, in a labelled MASTER group (Mode, Latency, Split, Gain, Clip). The cog keeps only window size and About.
4. Q5: the Mode LCD segments are approved.
5. Q2 and Q3 (long names, dense mod cards) were not approved; they are solved by the new layout, not by a choice.
6. **Knob sizes (owner, 2026-10-25, replacing "2 basic sizes"): three sizes, large, medium and small, following the real panels.** First round 2 pass had two sizes; the owner changed it to three before review.

### What changed

- **Layout:** horizontal cards (header strip with the slot name and the LCD engine menu, then knob rows). Height follows content: one row for up to about 10 knobs in A's slot cards (13 in B), two rows for more; an Off engine is the header strip alone. Rows are balanced (16 knobs are 2 rows of 8) and a cell is at most 96 pt wide, so few knobs pack to the left. Mods are the same card, compact.
  - **A:** Mods in a 470 pt right-hand column of mini-cards; slot cards 698 pt wide. Typical states fit (editor-a-A.png). With four 16-knob engines the columns exceed the strip and scroll (worst-case-A.png).
  - **B:** slot cards full width (1176 pt, one row each); a VOICE / MODS switch in the header swaps to four full-width Mods cards. Never scrolls; the strip has spare height. The cost: Mods and slot cards are never on screen together.
  - Same views, one enum (`EG13EditorVariant`).
- **Header:** 56 pt (was 44) to give the MASTER group a label row. The part tabs lost the Source engine name (A and B only) to make room; the name is on the Source card.
- **Matrix row and bottom bar:** unchanged. A needs more height in the worst case; a possible proposal is to drop the matrix row's own strip and put the 12 route knobs in a 2-row block under the Mods column. Not done; owner call.
- **Knob sizes:** large 48 pt, medium 38 pt, small 28 pt (scaled from the panels' 52, 40-43 and 18 px components so a small knob is still usable). Every knob shows its value, mod knobs too. Mixed sizes in a row share a label line (each knob sits centred in a box as tall as the row's largest).
- **Labels:** one table, `EG13ParamLabels` (short name per parameter name; the full name is the tooltip and the VoiceOver label). A two-word label wraps to two lines before it shrinks or cuts; `EG13LabelFit` decides, and `labelsFit` measures every label of every engine at the narrowest cell of its card (slot 64 pt, mod 50 pt). Bar and matrix labels sit in wide cells.
- **Engine menu:** 190 pt wide so "State Variable Filter" is not cut (test `engineNamesFit`). The menu is now drawn from the card's engine control spec, so a card that drops the spec drops the menu (round-1 review minor), and the reachability list holds the menu.
- **Stepper well** is 64 pt again by default (round-1 review minor 1); the wider well (88 pt) is only for "Per voice", "Part mix" and the like.
- **Part settings** popover and part bar use the wide stepper.

### Emphasis and size per engine (data: `EG13PanelLayout`)

Rule: size = the real panel's knob component for that parameter (Rogan 3 and 6 large; Rogan 1 and 2 medium; trimmers and buttons small), except where the owner pointed at a panel (Liquid Filter: large Freq, small Res; Random Sampler: large Rate and Spread, medium the rest). Emphasis (accent cap and chip, the part's accent) = the panel's large knobs. A mod's sizes drop one step (owner: mods use small or medium). Modules: Macro Oscillator = Plaits, Digital Oscillator = Braids, Modal Synth = Elements, Resonator (Source and Part FX) = Rings, Analog Filter = Ripples (Liquid Filter), Ring Mod / Vocoder = Warps (Meta Modulator), EQ = Shelves, LFO = Tides 2, Random / S&H = Marbles (Random Sampler), Granular = Clouds (Texture Synthesizer). Details are in the report. Fallbacks (no replica panel, or a panel with equal knobs) are listed there and in the `EG13PanelLayout` comment: Audio Input, Classic Filters, State Variable Filter, Envelope, Function Generator, Granular Delay, Reverb, Digital Oscillator and EQ.

Not mapped: Frames (Keyframer/Mixer) and Veils (Quad VCA): EG13 has no engine derived from them.

### Open choices for the owner

1. **A or B first** (see above). B is calmer; A shows Mods and voice together but scrolls with four big engines.
2. Whether emphasis should be "large knobs" (now) or "coloured knobs on the panel" (Plaits has none; Clouds has five). Size and colour are independent here.
3. Matrix row: keep, or fold into A's Mods column.
4. The part tabs without the engine name.

Status: awaiting owner feedback (round 2 is not approved).

## Layout design log, round 3 (2026-10-25): panel colours, variant A

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-editor docs/superpowers/m6-design/layout-r3` writes `editor-a.png`, `editor-b.png` (Part B), `editor-a-75.png`, `master.png`, `worst-case-a.png` (both columns at full height with a line where the visible strip ends) and the five `*-engines.png` contact sheets.

### Owner's round 2 feedback, as decisions

1. **Knobs are not coloured by part** ("it lessens the value of the coloured knobs from the originals"). Each knob's cap (white, pink or teal), chip and size come from the real panel, the same in Part A and Part B. This **supersedes the round 3 controls rule** "emphasised = accent cap in the part's accent" and the round 2 "emphasis is the part's accent" (review I1 is therefore moot: no knob uses a part colour). Part identity stays on the A/B tabs, the bottom-bar badge and the meters (teal A, pink B).
2. Chips follow the knob colour: a pink or teal knob gets a chip in its colour, a white knob a plain label. A pink or teal knob's arc matches its cap.
3. **Variant A is chosen.** Variant B, its enum and the VOICE / MODS switch are deleted.
4. The part tab that is shown also names its Source engine; the other tab is a plain letter.
5. Engines with no usable panel, and all mods: white caps, the main one or two controls large.

### What changed

- `EG13PanelLayout` holds size and cap colour per P (`L M S` plus `p` pink or `t` teal). Colours read from the Audible Instruments sources (`Rogan*Red` = pink, `Green` = teal, `White` and `Gray` = white). `EG13ParamEmphasis` is now `plain`, `pink`, `teal`, `black`; the part is gone from `knobStyle`.
- Elements-derived Modal Synth matches the panel: pink Blow, Flow, Blow Timbre; teal Strike, Mallet, Strike Timbre; the rest white. Others: Clouds-derived Granular (Position, Density, Spread, In Gain pink; Size, Texture, Feedback, Reverb teal), Braids-derived Digital Oscillator (Color pink, Timbre teal), Liquid Filter-derived Analog Filter (Resonance pink), Shelves-derived EQ (Mid 1 pink, Mid 2 teal). Plaits, Rings, Warps, Tides 2 and Marbles are white panels.
- Variant A's worst case (four 16-knob engines in the slots and mods) is taller than the strip; each column scrolls (`worst-case-a.png`). It shows the full content height, since an image render cannot draw a scroll view.

### Open choices for the owner

1. **Arc colour of white knobs:** the panel grey #E6E6E6 (`EG13Palette.whiteKnobArc`), light on the black skirt band and not a part colour. Alternative: pure white.
2. **Black (secondary) caps** (master Dry and Out, route amounts): a mid grey arc #A8A8A5 (`EG13Palette.blackKnobArc`), quieter than a white knob's, so the secondary knobs stay secondary. Alternative: the same as white knobs. This reverses round 2's "a black cap keeps the part-colour arc, never grey", at the owner's request.
3. **Blue knobs** (Plaits' LPG Colour and Decay, Clouds' Reverb are blue on the panel): shown teal, since EG13 has only pink and teal. Alternative: white.
4. **Fallback engines (no replica panel): white caps, main controls large.** Audio Input (Gain large), Envelope (Attack, Decay, Sustain, Release large in the panel table, medium in a mod), Function Generator (Attack, Rate), Reverb (Amount, Time), Digital Oscillator and Shelves EQ have a panel but no size difference (all medium). Pink or teal added only by analogy: Classic Filters and State Variable Filter take Liquid Filter's pink Resonance (small); Granular Delay takes Clouds' pink Time (as Position) and teal Size. Mods: white throughout.
5. Matrix row stays; the part tab names (from round 2) are now shown for the selected part only.

Status: **approved by the owner on 2026-10-25** ("round 3 approved"), with the open choices accepted as shown: white-knob arc #E6E6E6, black-cap arc #A8A8A5, blue panel knobs shown teal, and the fallback engines as listed.

## Modulation design log, round 1 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-modulation docs/superpowers/m6-design/modulation-r1` writes `editor-a.png`, `editor-b.png` (Part B), `editor-a-75.png`, `ring.png` (200 %), `chips.png` (200 %) and `traces.png` (200 %). The preview core animates its mod outputs from its tick count, so the PNGs are the same on every run: the patch is the init patch plus eight more routes, a chord held, 70 ticks in (an envelope 0.33 s into its cycle). The rulings below are the controller's (M6b Task 6 brief), not yet the owner's.

### What is shown

- **Ring** (`ring.png`): the Filter card (Cutoff and Resonance ride on routes) and eight knobs in each state: live above and below the set value, teal and pink caps, range only (no live value, no dot), black cap, small knob. The ring is a thin second arc in the knob's arc colour, drawn just inside the skirt band against the cap, under the value arc; the dot is the same colour. Nothing else on the knob changes.
- **Chips** (`chips.png`, and in `editor-a.png`): each card's header strip, to the right of the engine menu. A chip is a small LCD well (the engine menu's colours): dark at rest, lit green with the source's live level (the source trace's latest max); the steady source (velocity) shows at 70 %. Source has five routes (the fifth, "VEL → TIMBRE", is the "+1"), Filter two, Mod 1 has three routes into its own knobs and room for only "+3".
- **Traces** (`traces.png`, and in `editor-a.png`): the 64 x 20 pt well at the right end of a mod card's header strip, the slot's first output over 2 s, a min-max bar per frame and a line through the middles; the line breaks where an envelope is idle. The LFO (Mod 3) is bipolar and centred; the envelopes are bottom-up. Mod 4 is Off and has none.

### Open choices for the owner

1. **Ring placement and weight.** It sits in the 15 % of the skirt band the value arc leaves free next to the cap, so at 100 % it is about 1 pt wide on a large knob and 0.8 pt on a small one. On a pink or teal knob the ring and the dot are the same colour as the cap, so the dot reads as a bump on the cap's rim. Alternatives: thicken the ring by shrinking the value arc (this changes the approved arc), or put the ring outside the skirt.
2. **Chip colour.** LCD (dark to green) rather than the part colour or a grey; it matches the engine menus and adds no colour. Alternative: the part's ink colour (teal or pink) with the fill following the level.
3. **Mod card header is full.** The badge, engine menu and Scope leave room for the 64 pt trace and about one short chip; any more become "+k". Alternative: a narrower trace, or the trace in its own row (taller mod cards, which scroll sooner with four large engines), or chips only in the slot cards.
4. **Where Pitch and Amp chips go.** The Source card (they have no knob). Pan and Part Level routes show no chip (their controls are in the bottom bar). Alternative: chips on the bottom bar's Level and Pan.
5. **Trace scale.** A trace is centred when it has gone negative in the last 2 s, bottom-up otherwise (the core does not say which an output is), so an LFO that has only been positive for 2 s would redraw bottom-up.
6. **CPU.** The display costs about 11 % of one core in the preview with the init patch, a chord, three modulated knobs and three traces (budget 10 %): see the Task 6 report.

Status: awaiting owner feedback.

## Modulation design log, round 2 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-modulation docs/superpowers/m6-design/modulation-r2` writes `editor-a.png`, `editor-b.png`, `editor-a-75.png`, `ring.png` (four caps, live and range-only, 200 %) and `motion-menu.png` (the Filter card's list, 200 %). Same patch and tick count as round 1.

### Owner feedback on round 1, as decisions

1. **Ring.** Almost invisible. Decision: remove the thin inner ring and the dot. The arc is on the cap itself, in the LCD green (#AFD22C), from the set value to the live value, at the value arc's angles; width 18 % of the cap radius, at least 2.5 pt; range-only at 35 % opacity. The value arc in the skirt band is untouched.
2. **Motion chips.** Not understood; "a separate drop down menu for Motion might make more sense, when Motion is in use". Decision: chips deleted. A card with routes into it has one `MOTION n` LCD button after the engine picker (none without routes); it opens a popover (a menu cannot show live levels) listing each route in full ("Mod 2 Envelope → Cutoff  +30 %") with a level bar per row; a row calls the click closure Task 7 wires to the matrix. Mod cards get the same button; the trace keeps its place.
3. **Crowding.** Card headers were very crowded. Resolved by the chips going: a header is now badge or icon and title, engine menu, Scope (mods), the button, and the trace (mods).
4. **CPU.** "Let's try 15 fps motion." Decision: every second tick (15 Hz) refreshes the modulation fields (arcs, lists, traces). Knobs, meters and the rest stay at 30 Hz. The 0.001 threshold and the per-param observation isolation are kept. The trace holds 30 frames (still 2 s) and each frame is the min-max over both ticks it covers (the core clears its range on every read, so the skipped tick's range is folded in). An engine change refreshes at once.

### Result

CPU, `--mod-load`, release, 20 s window from 6 s after launch, three runs: 5.1, 5.1, 4.6 % of one core (round 1: 11.4, 12.0, 13.3 %).

### Choices made here, open for the owner

1. **Dark outline.** The green on the white cap was marginal, so every cap's arc has a 1.2 pt dark outline (the LCD well colour). Alternative: no outline, or only on the white cap.
2. **Arc over the indicator.** The arc is drawn over the cap's indicator line; where they meet, the green covers the indicator's tip by a few points.
3. **Popover, not a menu.** Needed for the live level bars. It is 340 pt wide, LCD colours.
4. **Row text.** Source name in full ("Mod 1 Envelope"), destination without its card prefix ("Cutoff"), the amount as set (not live).
5. **Pitch and Amp** still list on the Source card; Pan and Part Level have no list.

Status: awaiting owner feedback.


## Modulation design log, round 3 (2026-10-26): the wave mark

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-modulation docs/superpowers/m6-design/modulation-r3` writes `editor-a.png`, `editor-b.png`, `editor-a-75.png`, `mark.png` (one knob per cap colour at 75, 100 and 200 %, at the minimum, the maximum and the middle), `motion-menu.png` and `traces.png`.

Owner decisions from round 2:

1. The green arc did not read at 75 %, read poorly at 100 % and fails for colour-blind users: removed, live and range-only, with its outline. Knobs no longer observe the modulated value and no longer redraw per frame.
2. The MOTION button and its list are kept.
3. A knob that is modulated shows a **shape on its cap** and no amount (a knob cannot convey two values well). Live detail stays in the MOTION list and traces. Clarified later the same day: a larger generic wave, 75 % of the cap's width, 1.5 sine cycles, centred, stroke at least 2 pt, drawn under the pointer; dark (#333333) on white, white on pink, teal and black. It is static and never the source's own shape.
4. A colour-blind palette setting is deferred to M7.

Built: the mark is `EG13SineCycle` stroked in `EG13KnobShape`, shown when `EG13Param.isRouted` (a visible route targets the knob; written only when it changes, so the knob ignores every modulation value update). Click: the knob's NSView surface takes a 14 x 10 pt click area at the cap's centre (a press that stays within 3 pt is a click and opens a popover with `EG13MotionList` filtered to the knob; a longer drag, or a double-click, is the normal knob gesture). The context-menu fallback was not needed. CPU with `--mod-load`: 3.8, 4.0, 4.0 % (round 2: 5.1, 5.1, 4.6).

Open choices:

- The wave is full opacity. If the pointer reads poorly over it on a white cap, lighten the wave to about 60 %.
- The click area is small and central so most of the cap still behaves as a knob; the mark itself is larger than the click area.
- The mark appears at the next modulation refresh (up to 67 ms) after a route is added or removed.
- Popover behaviour inside the real plugin's NSHostingView and the click area's hit-testing are untested by automation (the preview renders cannot show them).

## Modulation design log, round 4 (2026-10-26): a small wave that dodges the pointer

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-modulation docs/superpowers/m6-design/modulation-r4` (`mark.png`, `editor-a.png`, `editor-a-75.png` are the round's files; the run also writes others).

Owner feedback on round 3: the large wave crossed the pointer and cluttered the cap. Decision: shrink it to about 45 % of the cap's width (1.5 cycles, stroke at least 2 pt, so 1.5 pt at 75 %) and place it in the top or bottom third of the cap, whichever the pointer is not in. Pointer in the upper half (from its actual angle, horizontal counts as upper): wave in the bottom third; pointer in the lower half (near min or max): wave in the top third. Same inks (#333333 on white; white on pink, teal, black), full opacity. The wave moves only when the value crosses horizontal; it is never animated.

Built: `EG13ModMarkGeometry.pointerIsUp` and `centre(capRadius:normalized:)` (the wave's centre is 0.55 cap radii from the cap's centre); the click area follows the wave. Test "the wave never intersects the pointer" sweeps 0 to 1 in 1 % steps on all three knob sizes against the wave's stroked bounding box and the pointer's width.

Open choices: the wave jumps between the two places at horizontal; a small knob's wave is close to the cap's edge; the 14 x 10 pt click area moves with the wave.

**Modulation display approved by the owner on 2026-10-26** (round 4: the 45 % wave in the top or bottom third, the MOTION n menu, 15 Hz traces).

## M6b layout design log, round 1 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r1 docs/superpowers/m6-design/m6b-layout-r1`. The preview core has the modulation demo's routes; PNGs are 100 % unless named.

### What is shown

- `editor-matrix.png`, `editor-matrix-75.png`: Part A with the right column on MATRIX (12 route rows: source, via and destination menus, a small amount knob, a clear button), at 100 % and 75 %.
- `editor-mods.png`: the same with MODS shown.
- `part-settings-split.png`, `part-settings-layer.png`: the Part settings content for A and B side by side (at 150 %), with the key-range strip. Split: A low, B high, the split marker between. Layer: both parts over most of the range, no marker.
- `settings.png`, `about.png`: the Settings and About panel over the editor.
- `context-menu.png`: the knob context menu (Harmonics, which has one route) as a list: "Modulate from" with its sources, and "Remove modulation: <source>". A real NSMenu cannot be snapshotted, so this is a drawn copy; the real menu uses the system look.

### Open choices for the owner

1. **Settings and About: sheet or overlay.** Built as an in-editor overlay (dimmed editor, panel in the middle), not a SwiftUI `.sheet`: a sheet is a host-window sheet that would not scale with the editor's zoom and that image renders cannot show, and its behaviour inside a plugin host window is untested. A sheet is a small change if you prefer it.
2. **Matrix row layout.** Source, via, destination, amount knob, clear, with the row number at the left. Is the amount knob (28 pt) large enough, and should the amount read to the right as now, or left of the knob?
3. **MODS / MATRIX switch placement.** Top left of the right column, as an LCD pair. Alternatives: top right, or in the header.
4. **Part settings strip.** The key-range strip is at the top of each part's card with the bands above the keys. Should the strip sit nearer Key Low and Key High?
5. **Settings content.** Only the two decided settings and the current-window size buttons. The text under each setting is explanatory; shorter text is possible.
6. **Context menu.** "Modulate from" opens a submenu of every source (21 in the demo). A grouped submenu (Keys, Controllers, Mods) is possible if the list is too long.
7. **About.** Version and build come from the core (`0.6.0 (abc1234)` in a real build; the preview core says "preview").

### Not yet tested

The overlay inside a real host's NSHostingView (clicks outside, focus) and the settings file on another machine are untested by automation.


## M6b layout design log, round 2 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r2 docs/superpowers/m6-design/m6b-layout-r2` writes `editor-matrix.png`, `editor-mods.png`, `editor-busy.png` (Modal Synth with 16 knobs as the Source, Granular Delay with 15, the most a Part FX has, as the Part FX) and `context-menu.png` (a drawn copy: Modulate from > Keys / Controllers / Mods, with Controllers and Mods opened).

Round 1 feedback: the owner approved everything else; two changes.

Decisions
1. **Spread the left column.** The four cards keep their heights; the free height is shared equally between them: gap = max(8 pt, (column height - sum of card heights) / (n - 1)), no space above the first or below the last (EG13Layout.spreadGap, drawn by EG13SpreadStack). Cards taller than the column keep the 8 pt gap and the column scrolls as before. Also applied to the right column's Mods view (four mini-cards), so both columns line up; the matrix view is unchanged.
2. **Group "Modulate from".** Keys (codes 1-8), Controllers (9-16), Mods (32 and up, one submenu per mod slot titled with the slot's name, e.g. "Mod 1 Envelope"). Derived from the codes (EG13SourceGrouping), not hand-listed; empty groups (a mod slot that is Off) are left out. The matrix row's source and via menus use the same grouping (they replace Performance / Mod 1...4). "Matrix full" is unchanged.

Status: awaiting owner feedback.


## M6b layout design log, round 3 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r3 docs/superpowers/m6-design/m6b-layout-r3` writes `editor-matrix.png`, `editor-mods.png` and `editor-busy.png` (same patches as round 2).

Owner decision (2026-10-26), reversing round 2's spread layout:
1. **Stack from the top.** The cards in both columns, the left module cards and the right Mods mini-cards, stack from the top with the normal gap (EG13Layout.gap, 8 pt). Spare height collects at the bottom of the column. A busy patch behaves as before: minimum gaps, and the column scrolls. EG13SpreadStack, EG13Layout.spreadGap and its tests are deleted.
2. Round 2's grouped "Modulate from" menus stay.

Status: awaiting owner feedback.


## M6b layout design log, round 4 (2026-10-26)

Screenshots: `swift run --package-path ui/EG13UI EG13UIPreview --snapshot-layout-r4 docs/superpowers/m6-design/m6b-layout-r4` writes `editor-matrix.png`, `editor-mods.png` and `editor-busy.png` (the busy patch now also has the tallest engine in every mod card, so the Mods column really overflows).

Round 3 feedback: "that didn't work": the bottom bar was still pinned to the window bottom.

Decisions
1. **The bottom bar floats up.** It sits one EG13Layout.gap under the taller of the two columns (left cards, or the right Mods or Matrix content). Every vertical gap is that gap (header to columns, between cards, columns to bar); the blank space collects below the bar. Bar top = min(header + gap + max(left, right) + gap, the pinned position): a busy patch fills the strip, the columns scroll and the bar is at the bottom as before. Window size and the 1200x760 canvas are unchanged; the content heights are measured (EG13Scrolling's onHeight).
2. **Mod card header.** The trace shrinks to the room left (minimum 40 pt). If that is not enough next to MOTION n (Mod 1 with an Envelope engine is about 20 pt short), the MOTION button moves to a second header line instead of clipping anything.

Status: awaiting owner feedback.
