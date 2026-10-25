# Interface

EG13 opens its own window in the host. It shows one part at a time and lets you edit every control of that part and the master. The window has no on-screen keyboard yet: play the plugin from your host's MIDI. (A keyboard is planned for a later version.)

## Window and scaling

The window is 1200 × 760 points at 100 %. You can resize it from 75 % (900 × 570) to 200 % (2400 × 1520). The proportions stay locked: dragging any edge or corner scales the whole window, and the layout scales with it, so nothing is cropped or rearranged at any size. In hosts that cannot resize plugin windows, the window stays at 100 %.

The window works in points, so on a Retina display it is drawn sharp at every size and EG13 ignores the host's own scale setting. Closing the window and opening it again starts at 100 %.

## Header

The header strip holds, from the left, the EG13 name, the A and B part tabs, and on the right the master **Mode** (Single, Layer, Split or Multi), the **master settings** button and the About button.

- The **A and B tabs** choose which part the window shows. Part A is teal, Part B is pink. Each tab names the part's Source engine. The shown tab is filled with the part's colour. Switching tabs changes every card, the matrix, the part settings and the bottom bar to that part; the master controls stay as they are.
- **Master settings** (the cog) holds the master settings that are not Mode, Dry or Out: **Split Note**, **Voice Gain**, **Soft Clip** and **Latency Mode**, and the window size (75 %, 100 %, 125 %, 150 %, 200 %; the host may keep the window as it is).

## Columns and cards

The shown part is laid out in columns: **Source**, **Filter**, **Shaper** and **Part FX**, then a **Mods** column, with the **matrix** below them.

- Each card has a header strip with its name in small capitals, the engine's name in lowercase, and an **engine menu** (a green-on-grey LCD well). Below it is a grid of knobs, four to a row: one for each parameter the selected engine uses, named as the engine names it (the Granular Part FX calls its fifth knob Density, the Resonator calls it Spread). A knob the engine does not use is hidden, so a card with the engine Off shows only its menu. A card never shows a knob that does nothing.
- A card shows up to 16 knobs (Source), 4 (Filter), 12 (Shaper) and 16 (Part FX). Long names are shown a little smaller, and cut with an ellipsis when they still do not fit; hover or the VoiceOver label gives the whole name.
- Knobs come in three looks. **Emphasised** knobs have an accent cap and a coloured name tag: the first two knobs of each Source, Filter, Shaper and Part FX card, which are the engine's main controls (the first is teal, the second pink), and the part's **Level**. **Secondary** knobs have a black cap with the part's colour in the arc: master **Dry** and **Out** and the matrix routes. Every other knob has a white cap.
- The **Mods** column has four compact mod cards, numbered 1 to 4. Each has an engine menu, a **Scope** stepper (per voice or per part) and a grid of knobs, eight to a row. A mod card's knobs show no value under the knob; hover over a knob to see its value.
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

## Bottom bar and meters

The bottom bar holds, from the left: the shown part's **Level**, **Pan**, **Glide**, **Voice Mode** and **Voices**, a **Part settings** button, the master **Dry** and **Out**, and a meter for each part.

- **Part settings** opens a panel with the rest of the part's settings, as steppers: Transpose, Base Note, MIDI Channel, Key Low, Key High, Trigger, Bend Range, Mono, Output, Input Source, **Part FX Input** (what the Part FX card listens to: the part mix, the input, or both), MPE and MPE Bend Range, and the four controller numbers CC A to CC D. Click anywhere else to close it.
- **Dry** is the level of the plugin's input mixed straight into the output, delayed to stay in time with the processed sound. **Out** is the master level.
- Each part has a **stereo meter** (left and right bars) in the part's colour, A teal and B pink, A first. The meters show each part's output after its level, pan and mute, refreshed 30 times a second. A meter rises instantly to a new peak and falls in dB, in a straight line on the meter, from the peak down to silence in 300 ms. A thin hold marker stays at the highest recent peak for 1.5 seconds, then falls the same way. A bar turns white at 0 dBFS or above.

While you drag a knob, it shows the value you are setting even if the host or automation is moving the same parameter; when you let go it keeps your value until the host reports it (at most half a second), then follows the host again. The editor never sends edits of its own: only your gestures reach the host.

## Automation

Turning a knob in the window counts as a touch: your host records the move as automation while you hold the knob, and stops when you let go. If the window closes while a knob is held, the touch ends at once.
