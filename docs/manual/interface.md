# Interface

EG13 opens its own window in the host. It shows one part at a time and lets you edit every control of that part and the master. EG13 has no playable keyboard: play it from your host's keyboard or MIDI input. The only keys in the window are the key-range graphic in Part settings, which shows which keys reach which part and does not make a sound.

## Window and scaling

The window is 1200 × 760 points at 100 %. You can resize it from 75 % (900 × 570) to 200 % (2400 × 1520). The proportions stay locked: dragging any edge or corner scales the whole window, and the layout scales with it, so nothing is cropped or rearranged at any size. In hosts that cannot resize plugin windows, the window stays at 100 %.

The window works in points, so on a Retina display it is drawn sharp at every size and EG13 ignores the host's own scale setting. A new window opens at the default size from Settings (100 % until you change it).

## Layout

The editor is one window with the two parts as tabs, the Mods in a column on the right and the output bar along the bottom.

## Header

The header strip holds, from the left, the EG13 name, the A and B part tabs, the **preset bar**, and on the right the **MASTER** group and the settings button.

- The **A and B tabs** choose which part the window shows. Part A is teal, Part B is pink. The shown tab is filled with the part's colour. The shown tab also names the part's Source engine. Switching tabs changes every card, the matrix, the part settings and the bottom bar to that part; the master controls stay as they are.
- The **MASTER** group holds the plugin-wide settings, each with a small label under it: **Mode** (Single, Layer, Split or Multi), **Latency** (Live or Studio), **Split** (the split note), **Gain** (voice gain) and **Clip** (soft clip). Master **Dry** and **Out** stay in the bottom bar. The group is the same on both tabs.
- The **cog** opens the **Settings and About** panel (see below).

## Presets

The preset bar sits in the header between the part tabs and the MASTER group: `◀ [ name ▾ ] ▶`, in the same green-on-grey LCD look as the engine menus. [Presets](presets.md) describes the banks, what a preset changes and keeps, and the User folder.

- **Arrows:** step back and forward through the list in order, across banks (Bank 1, Bank 2, then User). After the last preset they wrap to the first. A Patch loads into the part the window shows (the A or B tab).
- **Name:** shows what you loaded last. A Performance shows its name. After loading a Patch into one part the bar names each part, for example "A: Glass Bells / B: Warm Drift". A sound nobody has loaded shows "Init". A long name is cut with "…" so the bar never pushes the MASTER group; the full name is the bar's VoiceOver label.
- **Edited mark:** a `*` after the name means you have changed the sound since you loaded it. It clears when you save or load another preset. Changes the host makes (automation, loading a project or an undo step) do not set it.
- **Dropdown:** click the name. The list is grouped under **Bank 1**, **Bank 2** and **User** headings (EG13 FX lists **FX** and **User**). The preset the shown part came from has a tick. Each entry has a kind mark: **A+B** is a Performance (both parts, loaded whole) and **A|B** is a Patch (one part's sound). A Performance loads when you choose it. A Patch row opens a submenu, **Load into A** or **Load into B**, so you choose the part; clicking the row itself does not load. The arrows load a Patch into the shown part. The User group is read again each time you point at the bar, so files you add to the Presets folder appear.
- **Init:** loads the init sound into both parts and shows "Init".
- **A new instance** of EG13 (the instrument) opens on Bank 1's first preset, **First Light**, a two-part ambient Performance, and the bar shows its name. Choose **Init** for the bare init sound. EG13 FX opens on its init sound. A project you reopen keeps its own sound.
- **Save:** writes the loaded preset back to its User file. A factory preset is read-only, so Save on one makes a User copy under the same name. Save is off when nothing is loaded, or when the two parts came from different presets (use Save As).
- **Save As…:** a small panel asks for a name and what to save: **Whole instrument** (a Performance), **Part A** or **Part B** (a Patch). If a User preset of that name and kind exists, the panel asks "Replace it?" before it overwrites anything.
- **Rename…** and **Delete…** apply to a User preset only (the one the shown part came from). Delete asks for confirmation and removes the file; the sound stays as it is.
- **Messages:** a preset that cannot be loaded, files skipped in the User folder, or a failed save show as a small LCD line under the name for four seconds. A preset that fails to load never stops the sound. Loading a preset credited to someone else (Bank 2) shows its credit and licence there in the same way, for example "~zero-g:catnap by plantay (patchstorage.com/zero-gcatnap-vcv-rack-jam) — CC BY 4.0"; a warning from loading takes its place. Its row in the dropdown shows the same text as help when you hover over it. EG13's own presets show no credit line.
- The loaded name and the edited mark are saved with the host project, so reopening a project shows them again.

## Cards

The shown part is laid out as horizontal cards, one under another: **Source**, **Filter**, **Shaper** and **Part FX**, and the four **Mods**. The bottom bar sits under them. The right-hand column has a **MODS / MATRIX** switch (see Matrix below).

- The four slot cards fill the left side. The four Mods are a column of mini-cards on the right.
- Each card has a slim header strip with the slot's name and the **engine menu** (a green-on-grey LCD well, wide enough for the longest engine name). A mod card has its number, the engine menu and a **Scope** stepper (per voice or per part). Under the header are the knobs of the selected engine, as many rows as they need: a card with eight knobs is one row, a card with sixteen is two rows. A card whose engine is Off is its header strip alone. A knob the engine does not use is hidden, so a card never shows a knob that does nothing.
- If four tall engines together are taller than the window allows (for example a 16-knob Source, a 16-knob Part FX and three 16-knob Mods), the column scrolls. Every knob stays reachable. A scroll that starts over a knob turns the knob (see Knobs and steppers below), not the column: to scroll the column, scroll over the gaps between knobs, a label or the scroller.
- **Value text:** the text under a knob is the engine's value. A long one is shown in a short form (for example "Vocoder (long release)" reads "Vocoder long", and the Classic Filters models read "Cls LP 12 Std" for "Vember Classic Lowpass 12 dB Standard"); if it still needs room it wraps onto a second line. It is never cut with "..." and never shrunk. The full text is in the knob's tooltip and is what VoiceOver reads.
- **Names:** a knob shows a short name, taken from the abbreviation printed on the real module panel where there is one (BRIGHT, STRUCT, RES, LPG COL, S.RATE). A name is never cut and never shrunk: a name of two words that does not fit wraps to two lines. Hover over a knob for its full name and value; VoiceOver reads the full name.
- **Three knob sizes: large, medium and small**, following the real module's panel for the engine. For example, Granular follows the Clouds panel: Position, Size and Pitch are large, Density and Texture medium, Freeze small. An engine with no matching panel has medium knobs and one or two large main controls. A mod card's knobs are one step smaller (no large knob). Every knob shows its value under it.
- **Knob colours** come from the real panel, so they are the same in Part A and Part B (a part is told apart by the A and B tabs, the part badge in the bottom bar and the meters, not by knob colour). A knob has a white, pink or teal cap. A pink or teal knob has a name tag in its colour and an arc in its colour; a white knob has a plain name and a light grey arc. For example, the Modal Synth follows the Elements panel: Blow, Flow and Blow Timbre are pink, Strike, Mallet and Strike Timbre teal, the rest white. Engines with no matching panel (and every mod) have white caps, with the main one or two controls large. **Secondary** knobs have a black cap with a grey arc: master **Dry** and **Out** and the matrix routes.
- The matrix routes' amount knobs are secondary (black) knobs; they are in the Matrix pane (see Matrix below).

## Matrix

The top of the right-hand column has a two-part LCD switch, **MODS** and **MATRIX**. MODS (the default) shows the four mod cards; MATRIX shows the part's 12 routes, one row each. Each part has its own matrix, and the pane stays on MATRIX when you switch the A and B tabs.

- A row has, from the left: the route number, a **source** menu, **via**, a **via** menu, a **destination** menu, a small **amount** knob with its value beside it, and a **clear** button (x).
- The three menus are green-on-grey LCD wells showing the choice in a short form ("M2 ENV" for Mod 2 Envelope, "MODWHL" for Mod Wheel, "FLT CUTOFF" for Filter: Cutoff, "M1 REL SHP" for Mod 1: Release Shape). Hover for the full name; the menu items and VoiceOver use full names. A choice is never cut or shrunk. **None** is the first item of every menu and shows as "NONE".
- **Source** and **via** list the same sources: the sources in three submenus: **Keys** (velocity, release velocity, note, gate, random, pressure, slide, note bend), **Controllers** (mod wheel, pitch bend, channel pressure, CC A to D, transport phase) and **Mods**, which has one submenu per mod slot named for its engine ("Mod 1 Envelope") holding that slot's outputs. A mod slot whose engine has fewer outputs, or is Off, lists fewer. **Destination** lists the controls, under a submenu per card in this order: Source, Filter, Shaper, Part FX, Mod 1 to Mod 4, then Voice (Pitch, Amp, Pan, Part Level). Only the controls the cards' current engines use are listed, named as the engine names them.
- **(hidden):** a route saved with a source, via or destination that the current engines do not have (for example an output of a mod slot that is now Off) keeps it, and its well reads "(HIDDEN)". The route does nothing visible, and there is no mark or Motion row for it. If you change another field of that route, the hidden fields are cleared (set to None), because the core only accepts codes the engines have; the field you did not touch and can see stays as it was.
- The **amount** knob is the route's amount parameter, with the usual gestures (drag, scroll, double-click to reset, arrow keys) and automation.
- **Clear** sets source, via and destination to None and the amount to 0 (the amount as one edit, so a host records it). It is dimmed on a route that is already empty.
- Choosing a **Motion** row (or a row in a knob's mark popover) switches to MATRIX, scrolls to that row and outlines it for a couple of seconds.
- The cards in both columns (Source, Filter, Shaper and Part FX on the left, the four Mods mini-cards on the right) keep their own heights, one under another with at least an 8 pt gap. The taller column keeps the 8 pt gaps; the shorter one spreads its cards over the taller column's height with equal gaps, so both columns end on the same line. The MATRIX view is one block and is not stretched: if it is the taller side, the left cards spread to its height. The bottom bar floats up to sit one 8 pt gap under the columns; every other fixed vertical gap in the editor is that same 8 pt, and the blank space collects below the bottom bar. When the content is taller than the room (a busy patch), the columns fill the height and scroll, and the bottom bar sits at the bottom of the window.
- A Mod card's trace shrinks to the room left in the header (at least 40 pt). If it still does not fit next to the MOTION button, the button moves to a second line in the card's header; nothing is cut off.

## Modulate from

Right-click a knob (or ctrl-click it) to open its context menu. Nothing about the knob changes, no gesture starts and the value is not touched. A knob that is not a matrix destination (the bottom-bar Pan knob, the master controls) has no menu. Voice pan is still a destination: choose it under Voice in the matrix destination menu.

- **Modulate from** opens the sources in the same three groups as the matrix's source menu (Keys, Controllers, Mods > a submenu per mod slot). Choosing one puts the source into the **first empty route** of the shown part, aimed at this knob, at an amount of +25 % (the amount is set as one edit). An empty route is one with no source, via or destination.
- When all 12 routes are in use, the item reads **Matrix full** and is dimmed.
- For each route that modulates the knob, the menu also has **Remove modulation: <source>** (for example "Remove modulation: Mod 1 Envelope"). It clears that route like the matrix's clear button.

## Settings and About

The cog in the header opens a panel over the editor, with two tabs, **Settings** and **About**. The rest of the editor is dimmed behind it; **Close**, or a click outside the panel, closes it. (It is drawn inside the editor, not as a separate window, so it scales with the window and works the same in every host.)

**Settings** are for the whole Mac: every EG13 window, in every project, uses them (a change to the modulation display reaches open windows straight away; in a host that runs plug-ins in separate processes, a window picks it up the next time it opens), and they are never saved in a project. They are stored in `~/Library/Application Support/SymbioticAudio/EG13/settings.json`. EG13 writes the file only when you change a setting. If the file is missing, damaged or from a newer EG13, EG13 uses the defaults and leaves the file alone until you change something. If EG13 cannot write the file (for example the folder is read-only), the Settings tab says so, and the change applies to the windows opened in this session only.

- **Default window size for new editors:** 75 %, 100 %, 125 %, 150 % or 200 % (default 100 %). It applies to windows opened from then on; a window that is already open keeps its size. A host that cannot resize plugin windows still gets the default size when the window opens, but cannot change it afterwards.
- **Modulation display:** On (default) or Off. Off hides the wave marks, the MOTION buttons and the mod traces, and stops their 15 Hz refresh, which saves a little CPU. It changes nothing in the sound or the matrix, and the matrix and the mod cards stay fully editable.
- **Window size** buttons (75 % to 200 %) resize the current window now. They are not saved; use the default above to change what new windows open at. The host may refuse the new size.

While the panel is open, the editor behind it cannot be clicked, scrolled or reached by VoiceOver. Press Esc, click the dimmed area or press CLOSE to close it.

**About** shows the product name (EG13 or EG13 FX), the version and build, the credit "DSP based on the open-source Eurorack firmware by Émilie Gillet.", the licence (GPL-3.0) and the repository, github.com/SymbioticAudio/EG13 (click it to open it in your browser).

## Modulation display

A knob that a route modulates shows a **wave mark** on its cap; a card with routes into it has a **Motion** button in its header; each mod card shows a **trace**. Nothing else about a knob changes, and a knob with no route looks as it always did. (The window follows the part's most recently started voice, the same voice as Part Level; settings that belong to the whole part show the part's value.)

- **Wave mark:** a knob that at least one route targets shows a small wave (one and a half sine cycles, a stroke of at least 2 pt) on its cap, about 45 % of the cap's width and centred left to right. It sits in the bottom third of the cap while the pointer is in the upper half (above horizontal), and in the top third while the pointer is in the lower half (near minimum or maximum), so the pointer never crosses it; it jumps between the two only when the value takes the pointer across horizontal. It is dark grey on a white cap and white on pink, teal and black caps. It says only that the knob is modulated: it has no amount and never animates; the live detail is in the Motion list and the traces. A knob with no visible route (or only routes the engines hide) has no mark. **Click the mark** (a 14 by 10 pt area around it) to open a list of that knob's routes, the same rows as the Motion list; choosing a row opens it in the matrix like a Motion row. Dragging anywhere on the knob, including from the mark, adjusts it as always; a double-click resets it. VoiceOver offers the same list as the action "Show modulation routes".
- **Motion button:** a card with at least one route into its controls shows a small LCD button after the engine menu, "MOTION 3" for three routes; a card with none has no button. Click it to open a list with one row per route, in route order, written in full: "Mod 2 Envelope → Cutoff  +30 %" is Mod 2's envelope into the Cutoff at plus 30 percent. Each row has a small level bar that follows its source live (a fixed source such as velocity or the mod wheel has an empty bar). Routes into Pitch and Amp are listed on the Source card; routes into Pan and Part Level have no card (those controls are in the bottom bar) and are not listed. A route the part's engines hide (a source the mod engine does not have, a control the engine does not use) is not listed and has no mark. Choosing a row switches the right-hand column to MATRIX and scrolls to that route's row, which is outlined for a moment. A mod card has the same button for routes into its own parameters.
- **Trace:** each mod card with an engine shows, at the right of its header strip, the last two seconds of the slot's first output. Each frame is a vertical bar from the lowest to the highest value in it, so a fast LFO reads as a band, and a line joins the bars' middles. A moment with no output (a stopped envelope) breaks the line. A per-voice slot shows the most recently started voice only while that voice sounds: once the note has finished, the trace breaks, and after two seconds of nothing it stops redrawing. An output that holds still (a per-part envelope with its gate off) likewise stops redrawing once two seconds of the same value fill the trace. An output that never goes negative is drawn from the bottom up; one that does (an LFO) is drawn centred on a faint zero line.
- **Rate:** the Motion lists and the traces update 15 times a second; knob values, meters and everything else stay at 30.
- **Cost:** with the init patch, a held chord, three modulated knobs and three traces, the preview app's main thread takes about 3.6 % of one core (Apple M1, release build, CPU time over 20 s starting 6 s after start, `--mod-load`; runs 4.15, 3.70 and 3.55 %). The 10 % budget set for this display is met.

## Knobs and steppers

- **Drag** a knob up or down: 200 points of travel sweep the whole range. Hold **Shift** to move ten times finer.
- **Scroll** over a knob: one scroll line moves 1/100 of the range (one step on a stepped setting). Shift is ten times finer. This is deliberate: it also applies inside a scrolling column, so scrolling over a knob never moves the column.
- **Double-click** a knob to reset it to its default.
- With a knob focused, the **arrow keys** move it 1/100 of the range (Shift: a tenth of that), or one step.
- Stepped settings (voices, channel, mode and so on) always land on whole steps, and show their value in the LCD well; the minus and plus buttons move one step.
- Each drag, scroll or key press is one touch for your host's automation. If the touch is interrupted (the window closes mid-drag), the knob keeps the value it had reached and the touch ends.
- VoiceOver reads each knob's name and value, and adjusts it up or down by one step or 1/100 of the range.
- The engine menu in a card's header lists the engines by name; choosing one is a single automatable change.
- Stepped settings in the header (Latency, Split, Gain, Clip) show a short name under the well; the full name is for VoiceOver.

## Bottom bar and meters

The bottom bar holds, from the left: the shown part's **Level** (silence to +12 dB, 0 dB at its default; see [Parts and routing](parts-and-routing.md)), **Pan**, **Glide**, **Voice Mode** and **Voices**, a **Part settings** button, the master **Dry** and **Out** (grouped under the caption **OUTPUT**, so it is not mistaken for the header's MASTER group), and a meter for each part.

- **Part settings** opens a panel with the rest of the part's settings, as steppers: Transpose, Base Note, MIDI Channel, Key Low, Key High, Trigger, Bend Range, Mono, Output, Input Source, **Part FX Input** (what the Part FX card listens to: the part mix, the input, or both), MPE and MPE Bend Range, and the four controller numbers CC A to CC D. Click anywhere else to close it.
- The **key-range strip** at the top of Part settings is a non-playing 88-key strip, A0 to C8. Part A's key range is a teal band above the keys and Part B's a pink one, with a tint over the keys they cover. Drag the end of a band to change that part's **Key Low** or **Key High**; the stepper below shows the same value. A part's Low never goes above its High. The ends of the strip stand for the whole MIDI range: drag Low past the leftmost key and Key Low is 0, drag High past the rightmost key and Key High is 127, so a part dragged "all the way" plays every note, the ones beyond A0 and C8 included. Drag an end onto the leftmost or rightmost key itself and it lands on A0 or C8. In between, a dragged end lands on the key under it. A Key Low or Key High beyond the strip (the default 0 and 127, or a value set with the stepper) is drawn at the strip's end, and the stepper shows the exact value. In **Split** mode a vertical marker shows the **Split Note** (the first key that plays Part B); drag it to move the split, which is the same setting as Split in the MASTER group. The marker is hidden in the other modes, and Part B's band is hidden in **Single** mode, where Part B cannot sound. Each drag is one edit for the host's undo and automation. With VoiceOver, each handle is named with its value (for example "Part A lowest key, C2", or "C-1" for 0) and the adjust gestures move it by one semitone, over the full 0 to 127 range.
- **Dry** is the level of the plugin's input mixed straight into the output, delayed to stay in time with the processed sound. **Out** is the master level.
- Each part has a **stereo meter** (left and right bars) in the part's colour, A teal and B pink, A first. The meters show each part's output after its level, pan and mute, refreshed 30 times a second. A meter rises instantly to a new peak and falls in dB, in a straight line on the meter, from the peak down to silence in 300 ms. A thin hold marker stays at the highest recent peak for 1.5 seconds, then falls the same way. A bar turns white at 0 dBFS or above.

While you drag a knob, it shows the value you are setting even if the host or automation is moving the same parameter; when you let go it keeps your value until the host reports it (at most half a second), then follows the host again. The editor never sends edits of its own: only your gestures reach the host.

## Automation

Turning a knob in the window counts as a touch: your host records the move as automation while you hold the knob, and stops when you let go. If the window closes while a knob is held, the touch ends at once.

## Performance

Measured on an Apple M1 (base), release build, with the preview app that runs the editor's views and its 30 Hz tick over a fake plugin (a host's own drawing and the real plugin are not included):

- **Main thread, idle:** about 0.4 % of one core with a static patch and no notes sounding (the preview app with `--editor-static`, CPU time over 20 s starting 6 s after start; runs 0.35, 0.35 and 0.40 %). The budget is 5 %. M6a measured 0.25 %; the small rise is the 15 Hz modulation tick, which reads the core's modulation and finds nothing to redraw. The wave marks and Motion buttons are static and cost nothing at idle, and a trace that shows nothing, or the same value for two seconds, stops redrawing.
- **Main thread, modulation moving:** about 3.5 to 4 % of one core while the mod traces redraw at 15 Hz (the preview app with `--editor`, whose fake plugin animates every mod slot: runs 3.55, 3.60 and 3.90 %; with `--mod-load`, the init patch with a held chord, three modulated knobs and three traces: 4.15, 3.70 and 3.55 %; budget 10 %). In the plugin this is the cost while notes sound, or while a per-part mod slot (an LFO, say) keeps moving; it falls back to the idle figure two seconds after the last note ends.
- **Main thread, dragging:** about 13 % of one core while one knob sweeps its whole range 30 times a second (it was 24 % while every knob watched the same edit state). Each parameter is now its own observation unit, so an edit, or a host automation move, rebuilds only the controls that show that parameter (one knob per edit in the preview); the rest is SwiftUI's own per-frame work over the whole window. This is for information and is not held to a budget.
- **Audio thread:** applying 64 knob edits queued for one 128-sample block at 48 kHz (`UiBridge::drain`, which also reports them to the host) takes 2.3 microseconds, 0.084 to 0.087 % of that block's real-time budget (2667 microseconds; it was 0.088 %). The budget is 0.1 %. A drag produces a few edits per block, so real use is far below this.
- **Modulation publish (audio thread):** publishing the live modulation values and sampling the mod outputs for the traces, with both parts holding 8 voices each, adds 0.4 to 1.5 microseconds to a 128-sample block at 48 kHz (a block takes about 381 microseconds), 0.017 to 0.055 % of its real-time budget (it was 0.036 %). The budget is 0.5 %.
