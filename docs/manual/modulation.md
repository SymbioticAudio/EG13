# Modulation

Each part has four **mod slots** and a **modulation matrix** of twelve routes. A mod slot holds a modulation engine (an envelope, an LFO, a random source or a function generator) or is empty. A route takes a **source** (a mod slot's output, or one of the fixed sources such as velocity or the mod wheel), scales it by an **amount** and sends it to a **destination**: any control of the Source, Filter or Shaper, the pitch, the Amp, the pan, the part level, or a control of another mod slot.

In the host, each part has the groups *Part A/Mod 1* … *Part A/Mod 4* (each with *Engine*, *Scope* and P1–P16) and *Part A/Matrix* (*Route 1* … *Route 12*, the amounts). Which source and destination each route uses is part of the patch: it is saved with the project, and the editor arrives with EG13's own interface. A route with no source or no destination does nothing and its amount shows "-".

## How modulation runs

Each modulation engine runs at its module's own rate, as on the hardware (Envelope 31.25 kHz, LFO 62.5 kHz, Random / S&H 32 kHz, Function Generator 48 kHz), whatever your project's sample rate, so its times and rates are the module's. EG13 reads every engine's outputs once every 16 samples (a third of a millisecond at 48 kHz), works out the routes, and moves the results smoothly across the next 16 samples (see [Smoothing](smoothing.md)). A key press reaches a modulation engine within a quarter of a millisecond.

## Mod engines

### Envelope (based on Stages)

An envelope made of up to six segments, as one gated group of the module. One output, from 0 to 100 %.

- **Mode**: *ADSR* (Attack, Decay, Sustain, Release), *AD* (Attack then Decay; it always runs to the end, even if you let go early), *Looping* (Attack and Decay repeat for as long as the voice plays: a simple LFO with shapeable slopes) or *Custom*.
- **Attack**, **Decay**, **Release** are times from 1 ms to 16 s (half-way is about half a second). **Sustain** is a level. Each **Shape** bends its segment: 50 % is a straight line; lower and higher values curve it one way or the other.
- **Custom** plays up to six segments whose kinds (*Ramp*, *Step* or *Hold*) and loops are part of the patch, as you would set them with the module's buttons; the default custom shape is the ADSR. Each segment has two controls, in order: Attack and Attack Shape are segment 1's, Decay and Decay Shape segment 2's, Sustain and Sustain Time segment 3's, Release and Release Shape segment 4's, then Seg 5 A/B and Seg 6 A/B. As on the module, a Ramp's first control is its time and the second its shape; a Hold's first is its level and the second its time; a Step's first is its level and the second its glide. (EG13's own editor for Custom shapes arrives with the interface.)
- With a single segment, Custom plays the module's single-segment functions, as one Stages channel with its gate patched does: a Ramp is a decay envelope (looping: an LFO tapped by the notes), a Step samples and holds its level on each note (with glide), a Hold plays a pulse at its level for the time its second control sets (looping: a gate that lets each note through with a probability set by its second control).
- A new note on the voice restarts the attack from wherever the envelope is, so fast repeated notes do not click.
- Changing Mode while the envelope runs continues from wherever it is in the new layout, as on the module.
- Defaults: ADSR, 1 ms attack, 0.5 s decay, full sustain, 0.5 s release, straight segments.
