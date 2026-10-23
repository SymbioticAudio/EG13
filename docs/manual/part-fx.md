# Part FX

Each part has one **Part FX** slot after its voices. It holds an effect that processes the part's sound before level, pan and the Mono switch. The slot has a *Type* selector and 16 controls, P1 to P16, like the Source slot. Types are stepped parameters with a fixed range of 0 to 31; a value with no engine selects **Off**.

Engines: **Off**, **Granular**, **Granular Delay**, **Resonator** and **Reverb**. Each one is described in its own section below.

## The Part FX slot

- **Off** passes the part's sound unchanged. While Part FX is Off, the *Input* setting (below) is ignored, so Off never lets the Input leak into the part.
- P1 is the dry/wet **Mix** of every engine, so "Part FX P1" always means dry/wet. The other controls are named by the engine and show "(unused)" and "-" beyond the engine's own controls.
- **Reset to default in the host** sets each P to one value whatever the *Type*: P1 to P4 50 %, P5 75 %, P6 50 %, P7 to P12 0 %, P13 to P16 50 %. Each engine's own defaults are these values, so resetting never changes an effect from its defaults.
- Stepped choices inside an engine (Mode, Quality, Model and so on) are controls too: P's read in 16 fixed steps. Steps past the last option read as the last option, and new options are only ever added at the end. Switches such as Freeze are on at 50 % or more.
- The controls can be modulated: the modulation matrix can target any Part FX P (see [Modulation](modulation.md)).
- The effect keeps running while no voice sounds, so frozen buffers, tails and an Always-on part's Input keep playing.
- Changing *Type* keeps the old effect running for the slot's delay and then crossfades to the new one over 10 ms. The new effect starts empty and the old effect's tail is cut.
- Defaults: both parts start with Part FX **Off** in the instrument. In EG13 FX, Part A starts with **Granular** and Part B with **Off**. The *Input* setting starts at *Part mix* everywhere.

## Input

Each part has a *Part FX Input* setting:

- **Part mix:** the sum of the part's voices goes through the effect.
- **Input:** only the part's Input (as its Input source routes it: External, Other part or Both) goes through the effect. The part's voices skip the effect and are still heard. They are delayed by the effect's delay so they stay in time with it.
- **Part mix + Input:** both go through the effect.

With nothing on the Input (no sidechain, or Other part in Single mode) the effect receives silence in *Input* mode, and the voices are still heard.

## Notes and triggers

Every note the part plays is also sent to the Part FX: a voice trigger, not a legato glide, and on an Always-on part every key press. Engines use it as follows:

- **Granular:** each note starts a grain trigger.
- **Granular Delay:** with *Seed* set to *Notes*, each note is a seed.
- **Resonator:** each note strums it at that note's pitch.
- **Reverb:** ignores notes.

The note and the controls take effect after the voices' delay, on a 16-sample grid, so they line up with the sound they act on.

## Granular

Based on Clouds. To be written when the engine arrives (M5 Task 4).

## Granular Delay

Based on Beads. To be written when the engine arrives (M5 Task 5).

## Resonator

Based on Rings. To be written when the engine arrives (M5 Task 7).

## Reverb

The reverb at the end of Elements (the module behind the Modal Synth), used on its own, with its controls brought out separately. It runs at the module's own rate of 32 kHz, so, as on the module, everything that passes through it, the dry part included, loses the very top of the treble.

- **Amount** (P1): the module's own dry/wet crossfade. 0 % is the dry sound only, 100 % the reverb only, 50 % (the default) an equal mix.
- **Time** (P2): how long the tail rings, from short (0 %) to very long (100 %). The range is the one Clouds uses for its reverb of the same design.
- **Diffusion** (P3): how quickly the echoes smear into a smooth wash. Low settings keep more of the separate early echoes. The range is Elements' own, so it is a subtle control.
- **Brightness** (P4): a low-pass inside the reverb's loop. Low settings make the tail darker as it decays; high settings keep it bright. The range is Clouds' too.
- **Freeze** (P9): on (50 % or more), the reverb holds its current tail indefinitely and ignores new input, as Elements does at the top of its Space range. While frozen, Time and Brightness have no effect (the tail neither decays nor darkens); Amount still sets how much of the frozen tail you hear against the dry sound. Turning Freeze off lets the tail decay again with the current Time.
- P5 to P8 and P10 to P16 are unused.

On the module one *Space* knob moves the amount and the time together, and the diffusion and brightness are fixed by each patch. Here they are four separate controls, so any of them can be set or modulated on its own. The sound enters the reverb at the module's fixed input gain (0.2).
