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
