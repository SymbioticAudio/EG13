# Smoothing

When a control jumps (an automation step, or a modulation change), each engine follows it the way its original module would: some smooth it, some don't.

- **Macro Oscillator:** Harmonics, Timbre, Morph, Frequency, Decay and LPG Colour follow Plaits' knob filter: Timbre, Morph, Decay and LPG Colour settle in about 25 ms (time constant), Harmonics and Frequency in about 50 ms. Model switches at once. Aux Mix is EG13's own control; a 5 ms fade removes clicks.
- **Digital Oscillator:** no smoothing, as on the hardware (Braids reads its knobs directly). Fast automation of Timbre or Color can therefore zipper.
- **Modal Synth:** Elements' firmware filters. Most controls have a time constant of about 50 ms; Flow, Mallet and Geometry are a little slower, and Position and Space take about half a second to settle. Model and Ext In switch at once.
- **Resonator:** Rings' knob filter (time constant about 50 ms) on Structure, Brightness, Damping, Position and Frequency (so Frequency glides briefly). Model, Exciter and Polyphony switch at once. Spread is EG13's own control; a 5 ms fade removes clicks.
- **Audio Input:** Gain has a 1 ms fade. Attack and Release are times, so they need none.
- **Analog Filter:** the VCV module reads its knobs every sample; EG13 moves Frequency and Resonance in a straight line across each control block of up to 16 samples, so a step glides over at most a third of a millisecond at 48 kHz. Mode switches at once.
- **Classic Filters:** as in Surge, the filter's coefficients glide: each 16-sample control block they move a fifth of the way to a new target, in a straight line across the block, so a step settles in a few milliseconds (a time constant of about 5 blocks, 1.7 ms at 48 kHz). Type switches at once.
- **State Variable Filter:** Cutoff and Resonance move in a straight line across each 16-sample control block, as in the Macro Oscillator's own filters. Mode switches at once.
- **Ring Mod / Vocoder:** Warps' own knob filter, once per 60-sample block. Algorithm, Timbre and both Levels have a time constant of about 25 ms; Osc Pitch about 6 ms. Within each block Warps glides its parameters itself. Carrier, Modulator, Osc Shape and Mode switch at once.

After a reset, or when a voice starts from silence, every control starts at its current value with no glide.
