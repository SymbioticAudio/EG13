# Smoothing

When a control jumps (an automation step, or a modulation change), each engine follows it the way its original module would: some smooth it, some don't.

- **Macro Oscillator:** Harmonics, Timbre, Morph, Frequency, Decay and LPG Colour follow Plaits' knob filter: Timbre, Morph, Decay and LPG Colour settle in about 25 ms (time constant), Harmonics and Frequency in about 50 ms. Model switches at once. Aux Mix is EG13's own control; a 5 ms fade removes clicks.
- **Digital Oscillator:** no smoothing, as on the hardware (Braids reads its knobs directly). Fast automation of Timbre or Color can therefore zipper.
- **Modal Synth:** Elements' firmware filters. Most controls have a time constant of about 50 ms; Flow, Mallet and Geometry are a little slower, and Position and Space take about half a second to settle. Model and Ext In switch at once.
- **Resonator:** Rings' knob filter (time constant about 50 ms) on Structure, Brightness, Damping, Position and Frequency (so Frequency glides briefly). Model, Exciter and Polyphony switch at once. Spread is EG13's own control; a 5 ms fade removes clicks.
- **Audio Input:** Gain has a 1 ms fade. Attack and Release are times, so they need none.
- **Analog Filter:** the VCV module reads its knobs every sample; EG13 moves Frequency and Resonance in a straight line across each 16-sample control block, so a step glides over a third of a millisecond. Mode switches at once.

After a reset, or when a voice starts from silence, every control starts at its current value with no glide.
