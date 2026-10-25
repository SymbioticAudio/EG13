# Interface

EG13 opens its own window in the host. It shows one part at a time and lets you edit every control of that part and the master. The window has no on-screen keyboard yet: play the plugin from your host's MIDI. (A keyboard is planned for a later version.)

## Window and scaling

The window is 1200 × 760 points at 100 %. You can resize it from 75 % (900 × 570) to 200 % (2400 × 1520). The proportions stay locked: dragging any edge or corner scales the whole window, and the layout scales with it, so nothing is cropped or rearranged at any size. In hosts that cannot resize plugin windows, the window stays at 100 %.

The window works in points, so on a Retina display it is drawn sharp at every size and EG13 ignores the host's own scale setting. Closing the window and opening it again starts at 100 %.

## Layout (round 2: not approved yet)

The layout below is a design proposal in two variants, **A** and **B**, for the owner to choose between; the unchosen one will be removed. Both draw the same cards. They differ only in where the four Mods go.

## Header

The header strip holds, from the left, the EG13 name, the A and B part tabs, and on the right the **MASTER** group and the settings button.

- The **A and B tabs** choose which part the window shows. Part A is teal, Part B is pink. The shown tab is filled with the part's colour. Switching tabs changes every card, the matrix, the part settings and the bottom bar to that part; the master controls stay as they are.
- The **MASTER** group holds the plugin-wide settings, each with a small label under it: **Mode** (Single, Layer, Split or Multi), **Latency** (Live or Studio), **Split** (the split note), **Gain** (voice gain) and **Clip** (soft clip). Master **Dry** and **Out** stay in the bottom bar. The group is the same on both tabs.
- In variant B, a **VOICE / MODS** switch sits between the tabs and the MASTER group. VOICE shows the Source, Filter, Shaper and Part FX cards; MODS shows the four Mods cards.
- The **cog** opens a small panel with the **window size** (75 %, 100 %, 125 %, 150 %, 200 %; the host may keep the window as it is) and **About**.

## Cards

The shown part is laid out as horizontal cards, one under another: **Source**, **Filter**, **Shaper** and **Part FX**, and the four **Mods**. The **matrix** row sits under them, and the bottom bar under that.

- **Variant A:** the four slot cards fill the left side. The four Mods are a column of mini-cards on the right.
- **Variant B:** the slot cards use the full width. The Mods are on a page of their own behind the VOICE / MODS switch, each card also full width.
- Each card has a slim header strip with the slot's name and the **engine menu** (a green-on-grey LCD well, wide enough for the longest engine name). A mod card has its number, the engine menu and a **Scope** stepper (per voice or per part). Under the header are the knobs of the selected engine, as many rows as they need: a card with eight knobs is one row, a card with sixteen is two rows. A card whose engine is Off is its header strip alone. A knob the engine does not use is hidden, so a card never shows a knob that does nothing.
- In variant A, if four tall engines together are taller than the window allows (for example a 16-knob Source, a 16-knob Part FX and three 16-knob Mods), the column scrolls. Every knob stays reachable. Variant B never needs to scroll.
- **Names:** a knob shows a short name, taken from the abbreviation printed on the real module panel where there is one (BRIGHT, STRUCT, RES, LPG COL, S.RATE). A name is never cut and never shrunk: a name of two words that does not fit wraps to two lines. Hover over a knob for its full name and value; VoiceOver reads the full name.
- **Three knob sizes: large, medium and small**, following the real module's panel for the engine. For example, Granular follows the Clouds panel: Position, Size and Pitch are large, Density and Texture medium, Freeze small. An engine with no matching panel has medium knobs and one or two large main controls. A mod card's knobs are one step smaller (no large knob). Every knob shows its value under it.
- **Emphasised** knobs have an accent cap and a coloured name tag, in the shown part's colour (teal in Part A, pink in Part B). They are the large knobs of the real panel (for a panel with no large knob, its coloured ones; see the design log), and the part's **Level**. Emphasis is not tied to size. **Secondary** knobs have a black cap with the part's colour in the arc: master **Dry** and **Out** and the matrix routes. Every other knob has a white cap.
- The **matrix** row has the part's 12 route amounts, labelled Route 1 to Route 12. Choosing each route's source and destination is not in this version: routes keep the sources and destinations they have, and you can set their amounts. (Source and destination choices arrive with the matrix editor in a later version.)

## Knobs and steppers

- **Drag** a knob up or down: 200 points of travel sweep the whole range. Hold **Shift** to move ten times finer.
- **Scroll** over a knob: one scroll line moves 1/100 of the range (one step on a stepped setting). Shift is ten times finer.
- **Double-click** a knob to reset it to its default.
- With a knob focused, the **arrow keys** move it 1/100 of the range (Shift: a tenth of that), or one step.
- Stepped settings (voices, channel, mode and so on) always land on whole steps, and show their value in the LCD well; the minus and plus buttons move one step.
- Each drag, scroll or key press is one touch for your host's automation. If the touch is interrupted (the window closes mid-drag), the knob keeps the value it had reached and the touch ends.
- VoiceOver reads each knob's name and value, and adjusts it up or down by one step or 1/100 of the range.
- The engine menu in a card's header lists the engines by name; choosing one is a single automatable change.
- Stepped settings in the header (Latency, Split, Gain, Clip) show a short name under the well; the full name is for VoiceOver.

## Bottom bar and meters

The bottom bar holds, from the left: the shown part's **Level**, **Pan**, **Glide**, **Voice Mode** and **Voices**, a **Part settings** button, the master **Dry** and **Out**, and a meter for each part.

- **Part settings** opens a panel with the rest of the part's settings, as steppers: Transpose, Base Note, MIDI Channel, Key Low, Key High, Trigger, Bend Range, Mono, Output, Input Source, **Part FX Input** (what the Part FX card listens to: the part mix, the input, or both), MPE and MPE Bend Range, and the four controller numbers CC A to CC D. Click anywhere else to close it.
- **Dry** is the level of the plugin's input mixed straight into the output, delayed to stay in time with the processed sound. **Out** is the master level.
- Each part has a **stereo meter** (left and right bars) in the part's colour, A teal and B pink, A first. The meters show each part's output after its level, pan and mute, refreshed 30 times a second. A meter rises instantly to a new peak and falls in dB, in a straight line on the meter, from the peak down to silence in 300 ms. A thin hold marker stays at the highest recent peak for 1.5 seconds, then falls the same way. A bar turns white at 0 dBFS or above.

While you drag a knob, it shows the value you are setting even if the host or automation is moving the same parameter; when you let go it keeps your value until the host reports it (at most half a second), then follows the host again. The editor never sends edits of its own: only your gestures reach the host.

## Automation

Turning a knob in the window counts as a touch: your host records the move as automation while you hold the knob, and stops when you let go. If the window closes while a knob is held, the touch ends at once.
