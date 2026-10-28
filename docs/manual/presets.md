# Presets

EG13's presets are chosen from the **Presets** button in the editor's header, `Presets ▾`, and the name strip under the header names what is loaded into each part (see [Interface](interface.md) for the header itself). This page covers what the presets are, what loading one changes, your own presets, and where the factory presets come from.

## Two kinds of preset

- A **Performance** (kind mark **A+B**, file extension `.eg13perf`) holds both parts and the master settings that belong to the sound. Loading one replaces both parts.
- A **Patch** (kind mark **A|B**, file extension `.eg13patch`) holds one part's sound. It loads into Part A or Part B, as you choose, and leaves the other part alone.

To load a Patch from the dropdown, open its row's submenu and choose **Load into A** or **Load into B**; clicking the row itself does not load anything. A Performance loads when you choose its row. There are no previous and next arrows: a Patch only ever goes into the part you name.

## What the header shows

The name strip under the header shows, for each part, the name of what is loaded into it:

- After a Performance: its name and the part letter for each part, `First Light·A` and `First Light·B`.
- After a Patch: the Patch's name for that part only, for example `Rain on Strings` for Part A. With a Patch in each part the strip reads `Rain on Strings` and `Teak Mallets`. Loading a Patch into Part B after a Performance gives `First Light·A` and `Teak Mallets`.
- **Init** on a part nothing has been loaded into, and on both after choosing Init from the menu.
- A `*` after a part's name is that part's **edited mark**: you have changed that part in EG13's editor since its sound was loaded (a knob, an engine, a setting or a route of that part). A master setting that belongs to the sound (Mode, Split Note, Soft Clip, Dry) marks both parts. The setup controls are the exception, because a preset never carries them: master Level, Voice Gain, Latency Mode, and each part's MIDI channel, Output, MPE mode and bend range and CC A to D do not set the mark. Loading a Performance or Init, or saving the whole instrument, clears both marks; loading or saving a Patch clears that part's. Changes the host makes (automation, an undo step, loading a project) do not set it: EG13 cannot tell automation apart from an edit you make in the host.

The project remembers what each part was loaded from and whether it was edited, so the strip shows the same names when you reopen the project. Loading a preset marks the project as changed in the host, and so does a change to a part's name or edited mark; setting them to what they already are, and opening the editor alone, do not.

## What a preset changes, and what it keeps

A preset carries the sound, not your setup.

A **Performance** loads:

- both parts' engines (Source, Filter, Shaper, Part FX and the four mod slots), their controls and the mod slots' scope (per voice or per part);
- both parts' modulation routes and envelope shapes;
- each part's Level, Pan, Glide, Transpose, Base Note, Voice Mode, Voices, Trigger, Bend Range, Mono, Input Source and Part FX Input;
- each part's Key Low and Key High;
- the master Mode, Split Note, Dry and Soft Clip.

A **Patch** loads the same part settings into the one part, but not the key range (Key Low and Key High stay as they were) and none of the master settings.

Both kinds **keep** these as you set them:

- each part's **MIDI Channel** and **Output**;
- each part's **MPE** zone and **MPE Bend Range**;
- each part's **CC A** to **CC D** numbers;
- the master **Voice Gain**, the master **Level** and the **Latency Mode**.

So a preset never moves a part to another MIDI channel or output bus, never changes the controller numbers you have set up, and never changes the latency the host has compensated for.

**Init** (in the dropdown) loads the bare init sound described in [Parts and routing](parts-and-routing.md), with the same rules: it keeps the setup above. A control's default (what a reset to default gives) is its init value, not the loaded preset's value.

**A new instance** of EG13 (the instrument) opens on Bank 1's first preset, **First Light**. A new instance of EG13 FX opens on its init sound. A project you reopen keeps its own sound.

If a preset cannot be loaded (a damaged file, or a preset for the other plugin), the sound and the name stay as they were and a message under the bar says why. Loading never stops the audio.

## Factory banks

Factory presets are built into the plugin and cannot be changed or deleted. The dropdown lists them in this order, then the User bank.

### Bank 1 (the instrument)

Forty presets made for EG13: thirteen Performances and twenty-seven Patches. They are ambient pads, textures and sequences first, with keys, basses, leads and two small drum kits. Each preset's note (in its source file, `presets/bank1/`) names the idea it comes from.

- Performances: First Light, Tide Pools, Speaking Glass, Harbour Lights, Pentatonic Breath, Comb Choir, Cardboard Kit, Night Market, Cellar Jazz, Spiral Stair, Glass Zither, Still Water, Aurora Loom.
- Patches: Rain on Strings, Vowel Drift, Swarm Horizon, Barber Pole, Prism Fog, Wind Organ, Tape Memory, Teak Mallets, Sympathy Haze, Bell Tines, Clockwork Bloom, Dusty Kalimba, Rubber Floor, Pressed Velvet, Pachinko, Sour Squelch, Belfry, Disco Toms, Backward Glass, Bronze Stabs, Bow Arm, Howl Lead, Warm Blanket, Workhorse Bass, Ricochet, Chapel Drawbars, Tin Choir.

Several presets respond to the mod wheel, aftertouch or MPE pressure and slide (CC 74); Harbour Lights, Cardboard Kit, Cellar Jazz and Still Water are split at C4. Clockwork Bloom, Pachinko, Sour Squelch and Night Market follow the host's tempo.

### Bank 2: catnap (the instrument)

Seven presets rebuilt from a VCV Rack patch shared under a Creative Commons licence (credits below): the whole patch, three of its parts, and three variations by EG13 that are not in the patch.

- **catnap** (Performance): the whole patch. It starts droning as soon as it is loaded, with no note held, because its Part B is set to Trigger *Always on*, as the patch's oscillator runs untriggered. Part B is heard only through Part A (the mode is *Single*). Your notes strike the bell in Part A.
- **catnap — plaits** (Patch): the chord-drone side, played from the keys.
- **catnap — rings** (Patch): the bell side, one strike per note.
- **catnap — warps** (Performance): the ring-modulator corner of the patch, which the patch itself keeps turned down, brought up.
- **catnap — clouds** (Patch): a variation: the patch's Clouds stage brought forward, a dense, wide, octave-up grain cloud of the chord side; the mod wheel past half-way holds it frozen.
- **catnap — tides** (Patch): a variation: the patch's slow filter swell made into a pad, with the chords held as you play them; the mod wheel speeds the swell up and aftertouch opens the filter.
- **catnap — night** (Performance): a variation: the whole patch, slower and darker. Like **catnap**, it starts droning as soon as it is loaded, with no note held (its Part B is set to Trigger *Always on*).

Loading a Bank 2 preset shows its credit and licence under the bar for four seconds; hovering over its row in the dropdown shows them too.

### FX (EG13 FX)

EG13 FX has a bank of its own, twelve effect Performances: Slow Bloom, Octave Shimmer, Glass Spectrum, Worn Tape Echo, Grain Stutter, Ring Bells, Drifting Shift, Sympathetic Room, Cathedral Air, Stepped Filter, Ladder Sweep and Chord Vocoder. They are granular washes, shimmer, tape echo, ring modulation and frequency shifting, a resonator room, a long reverb, moving filters and a vocoder. They need no notes: put EG13 FX on an audio track and play audio through it.

EG13 FX lists only the FX bank and your own FX presets. The instrument's presets are not offered in EG13 FX, and FX presets are not offered in the instrument.

## Your own presets (the User bank)

- **Save As…** saves the current sound under a new name. Choose **Whole instrument** to save a Performance, or **Part A** or **Part B** to save that part as a Patch. If a User preset of that name and kind exists, EG13 asks before replacing it.
- **Save** writes the loaded preset back to its User file. On a factory preset it saves a User copy under the same name; if that User copy already exists, Save opens Save As with the name filled in and asks "Replace it?". Save is greyed out when Init is loaded (there is no name to save under) and when the two parts came from different presets (there is no single preset to save over): use Save As.
- **Rename…** and **Delete…** act on the User preset the shown part came from. Delete removes the file; the sound you are hearing stays.
- A saved preset keeps its **name** only. The note, credit and licence of a factory preset are not copied into your file. If you share a changed copy of a Bank 2 preset, credit the original yourself (see Credits).

### The User folder

User presets are files in

```
~/Library/Application Support/SymbioticAudio/EG13/Presets/
```

and EG13 FX's in the `FX` folder inside it. (In Finder, choose Go > Go to Folder and paste the path.) One file holds one preset. The User bank lists them by name, ignoring case.

- **File names:** the file is named after the preset. Characters that cannot be in a file name on some systems (`/ \ : * ? " < > |` and control characters) become `-`, leading dots and surrounding spaces go, and the name is cut at 80 characters. The preset keeps the name exactly as you typed it, and that is the name the bar shows.
- **Sharing:** send the `.eg13perf` or `.eg13patch` file; the person receiving it drops it into their Presets folder (EG13 FX presets into `FX`). The folder is read again each time the pointer reaches the Presets button, so the file appears without restarting the host.
- **Files that cannot be listed** are skipped, and a message names them (up to three, then a count): a file that is not an EG13 preset or cannot be read, a preset from a newer version of EG13, or a preset for the other plugin (an EG13 FX preset in the instrument's folder, or the other way round). The other presets still list.

## Credits

**Bank 1** and the **FX** bank are original presets made for EG13 (licence GPL-3.0-or-later, as EG13). They take ideas, not files, from:

- the user manuals of the original Eurorack modules that EG13's engines are based on (each preset's note names the manual and the idea);
- ambient patches shared by the VCV Rack community in the VCV community forum's ambient threads;
- "Melodic Ambient Patching in VCV Rack" on Patchstorage.

These are inspiration only: no patch file was copied or translated for Bank 1 or the FX bank.

**Bank 2** is a rebuild of **"~zero-g:catnap : vcv rack jam" by plantay**, published at <https://patchstorage.com/zero-gcatnap-vcv-rack-jam/> under the Creative Commons Attribution 4.0 International licence (CC BY 4.0, <https://creativecommons.org/licenses/by/4.0/>). Each Bank 2 preset carries the credit "~zero-g:catnap by plantay (patchstorage.com/zero-gcatnap-vcv-rack-jam)" and the licence "CC BY 4.0". The patch's author does not endorse EG13. The patch file is not included with EG13.

The presets are changed from the patch:

- The patch was rebuilt for EG13's fixed architecture: its modules' settings were translated onto EG13's engine slots (the Plaits, Rings, Ripples, Warps and Clouds ports for the sound; Marbles and Tides as mod slots), and its patch cables onto routes.
- The Plateau reverb is left out: EG13 has no equivalent (its saved Wet is 0 in the patch).
- Rings is played from the keys, not struck at random by Marbles, so the bell follows your notes.
- The latch that freezes Clouds (a Branches toggle in the patch) is approximated by a second Random / S&H slot whose output freezes the granular effect about half the time.
- The Tides swell is not reset by Branches; it cycles freely.
- In "catnap — warps", the ring modulator's modulator is the Plaits drone from Part B; in the patch it is Clouds' output, which carries that drone, and EG13 cannot feed a part's Part FX output back into its Shaper.
- The full rebuild drones as soon as it loads, because Part B is *Always on*, as the patch's untriggered oscillator is.
- "catnap — clouds", "catnap — tides" and "catnap — night" are variations by EG13, not part of the patch. "clouds" plays the chords from the keys without the Marbles sequence or the bell, makes the Clouds grains longer, denser, wider and wetter, and adds a mod-wheel freeze; "tides" plays the chords from the keys without the Marbles sequence, sweeps the low-pass wider and darker, mixes Clouds lower without freezing it, and adds mod-wheel and aftertouch routes; "night" slows the Marbles clock, the Tides swell (about 8 s instead of 4.8 s) and the freezing, lowers the Ripples cutoff, dulls and lengthens the bell, and makes the Clouds grains longer, sparser and more reverberant.

Each preset's note gives the details. The full notice is in `THIRD-PARTY-NOTICES.md`.
