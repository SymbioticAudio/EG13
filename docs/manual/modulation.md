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
- Changing Mode stops the envelope, as on the module: a held note's envelope falls silent (within about a millisecond) until the next note, except Looping, which starts cycling again.
- Sustain Time and Seg 5 / Seg 6 only act in Custom.
- Defaults: ADSR, 1 ms attack, 0.5 s decay, full sustain, 0.5 s release, straight segments.

### LFO (based on Tides 2)

A slope generator with four outputs.

- **Mode**: *Cycle* (an LFO), *AD* (each note plays one attack-decay slope) or *AR* (rises while the note is held, falls when it is released).
- **Frequency**: ±4 octaves around the **Range**'s centre: *Medium* centres on 2 Hz, *Low* on 0.125 Hz (one cycle every 8 seconds). The module's audio range is not offered: EG13 reads modulation every 16 samples, too slowly for audio-rate modulation.
- **Shape**, **Slope** and **Smoothness** shape the wave as on the module: Slope moves the peak (a ramp down, a triangle at 50 %, a ramp up); Shape morphs the curve; Smoothness below 50 % rounds it off, above 50 % folds it.
- **Output** chooses what the four outputs carry, as the module's output mode button does: *Phase* (default: the same wave on all four, each later than the previous by an amount set by **Shift**; at the default 100 % they are a quarter cycle apart), *Gates* (out 1 the wave scaled by Shift, out 2 the raw wave, outs 3 and 4 end-of-attack and end-of-release gates), *Amplitude* (Shift pans the wave across the outputs) or *Frequency* (Shift picks frequency ratios between the outputs).
- Each new note on the voice restarts the cycle (per-voice scope). A per-part LFO restarts when you start playing after all keys were up.
- **Sync** *On* locks the LFO to the host's tempo: Frequency then picks a ratio from Tides' table (from one cycle every 16 beats to 16 cycles per beat; 50 % is one cycle per beat) and the cycle lines up with the song position. With the transport stopped it keeps running at the last tempo the host gave (120 BPM if none); when the transport starts it jumps into line with the song. With Sync on, notes do not restart the cycle.
- In Cycle mode the outputs swing from −100 % to +100 %; in AD and AR they run from 0 to 100 %.
- Defaults: Cycle at 2 Hz, Phase outputs a quarter cycle apart, Sync off.

### Random / S&H (based on Marbles)

Random gates and random voltages, with the module's controls (the knob mappings follow VCV's version of the module). Seven outputs: **T1**, **T2**, **T3** (gates: 0 or 100 %) and **X1**, **X2**, **X3**, **Y** (random values).

- The **T** section makes gates: **T Rate** sets the clock (T2 is the clock itself), **T Model** (*Coin Toss*, *Clusters*, *Drums*) how T1 and T3 share the beats, **T Bias** which of them wins more often, **T Jitter** how loose the timing is, **T Range** (*1x*, *1/4x*, *4x*) the clock's range.
- The **X** section makes values on each T1/T3 gate: **X Spread** the width of the distribution, **X Bias** where it leans, **X Steps** from smooth (low) through random values to notes of **X Scale** (high: *Major*, *Minor*, *Pentatonic*, *Pelog*, *Raag Bhairav*, *Raag Shri*), **X Mode** (*Identical*, *Bump*, *Tilt*) how the three X outputs differ. **Y** is a slower value (one for every four X values) with the same spread, bias and steps.
- **X Range**: *Narrow* and *Positive* give 0 to 100 % (Narrow is the module's 0–2 V, Positive its 0–5 V, both scaled to full range), *Full* gives −100 % to +100 % (default).
- **Deja Vu** loops the random sequences instead of always drawing new ones; **T Deja Vu** and **X Deja Vu** switch it on for each section and **Length** sets the loop's length (1 to 16 steps).
- **Sync** *On* clocks the T section from the host's tempo, one beat per clock, and T Rate then multiplies or divides the beat. It restarts its pattern when the transport starts or jumps; with the transport stopped it keeps the last tempo (120 BPM if none). It takes about two beats to lock.
- A new slot with Random / S&H is per part, so every voice hears the same random values; set Scope to per voice for a different sequence on each voice. Every voice and slot has its own random sequence.
- Defaults: VCV's, except X Range *Full*.
