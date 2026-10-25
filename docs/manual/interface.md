# Interface

EG13 opens its own window in the host. It shows one part at a time and lets you edit every control of that part and the master. This page is filled in as the interface is built.

## Window and scaling

The window is 1200 × 760 points at 100 %. You can resize it from 75 % (900 × 570) to 200 % (2400 × 1520). The proportions stay locked: dragging any edge or corner scales the whole window, and the layout scales with it, so nothing is cropped or rearranged at any size. In hosts that cannot resize plugin windows, the window stays at 100 %.

The window works in points, so on a Retina display it is drawn sharp at every size and EG13 ignores the host's own scale setting. Closing the window and opening it again starts at 100 %.

## Part tabs

The A and B tabs choose which part the window shows. Part A is teal, Part B is pink. *To be filled in by later M6a tasks.*

## Columns and cards

The part is laid out in columns: Source, Filter, Shaper and Part FX, then a Mods column. Each card has an engine selector and the engine's named knobs; unused controls are hidden. *To be filled in by later M6a tasks.*

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

The bottom bar holds the part settings, the master Dry and Out levels and the meters. The meters show each part's output after its level, pan and mute, refreshed 30 times a second. A meter rises instantly to a new peak and falls in dB, in a straight line on the meter, from the peak down to silence in 300 ms. A thin hold marker stays at the highest recent peak for 1.5 seconds, then falls the same way. A meter turns pink at 0 dBFS or above.

While you drag a knob, it shows the value you are setting even if the host or automation is moving the same parameter; when you let go it keeps your value until the host reports it (at most half a second), then follows the host again. The editor never sends edits of its own: only your gestures reach the host.

## Automation

Turning a knob in the window counts as a touch: your host records the move as automation while you hold the knob, and stops when you let go. If the window closes while a knob is held, the touch ends at once. *To be filled in by later M6a tasks.*
