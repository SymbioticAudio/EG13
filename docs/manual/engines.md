# Engines

Every engine shows its controls as P1, P2, … in the host. A slot has more parameters than most engines use: the unused ones are named "(unused)" and show "-". The Source slot has 16 parameters.

## Sources

### Macro Oscillator (based on Plaits)

Controls: **Model** (24 models, from Virtual analog to Hi-hat), **Harmonics**, **Timbre**, **Morph**, **Frequency** (±12 semitones), **Decay**, **LPG Colour** and **Aux Mix**.

- Each note triggers the model and opens its low-pass gate with the note's velocity.
- Decay and LPG Colour are the settings that Plaits keeps behind its Morph and Timbre knobs.
- **Aux Mix** blends the module's AUX output into the main output (0 % = OUT only). The AUX output can also feed the Ring Mod / Vocoder's carrier or modulator (*Source AUX*), whatever Aux Mix is set to.
- The output is mono.

### Digital Oscillator (based on Braids)

Controls: **Shape** (47 shapes), **Timbre**, **Color**, **Frequency** (±12 semitones), **Signature**, **Bit Depth** (2 to 16 bits) and **Sample Rate** (4 kHz to 96 kHz).

- Braids' own envelope and quantizer are not exposed; EG13's envelopes replace them.
- A level follows each note's gate and velocity, before the Signature waveshaper as on the module, so velocity also changes how hard the waveshaper is driven.
- The output is mono.

### Modal Synth (based on Elements)

All 16 panel controls: **Model** (Modal, String, Strings), **Contour**, **Bow**, **Blow**, **Strike**, **Flow**, **Mallet**, **Bow Timbre**, **Blow Timbre**, **Strike Timbre**, **Geometry**, **Brightness**, **Damping**, **Position**, **Space** and **Ext In**.

- **Ext In** routes the part's Input like the module's two EXT IN jacks: *Off*, *Blow*, *Strike* or *Both* (default *Strike*).
  - *Blow* feeds the Input into the blow exciter. That exciter is gated by the exciter envelope, so with *Blow* the Input is silent until you hold a note, as on the hardware.
  - *Strike* excites the resonator directly, with or without a note.
- The output is stereo; Space sets its width.
- Controls are smoothed as in the firmware, so some move slowly: Space and Position take about half a second to settle after a jump ([Smoothing](smoothing.md)).
- In the instrument, sound excited by the Input arrives up to about 6 ms after the Dry signal ([Latency](latency.md)).

### Resonator (based on Rings)

Controls: **Model** (Modal, Sympathetic, String, FM Voice, Sympathetic Q, String + Reverb), **Structure**, **Brightness**, **Damping**, **Position**, **Exciter**, **Frequency** (±12 semitones), **Spread** and **Polyphony**.

- **Exciter:** *Internal* (each note strums the resonator), *Input* (the part's Input excites it; notes only set the pitch), or *Both*.
- **Polyphony** 1, 2 or 4: with more than one voice, earlier strums keep ringing at their own pitch while new ones start.
- **Spread** 0 folds the module's odd and even outputs to mono, the way the module does when only OUT is patched. Spread 1 puts the odd output on the left and the even output on the right.
- The played note reaches the Resonator as the module's V/OCT input against a tonic of C4 plus **Frequency**.
  - With the Sympathetic String model at Structure below about 1/7, the strings drone at that tonic, as on the hardware with V/OCT patched.
  - Small pitch bends pass through Rings' own note filter, so they are smoothed.
  - Moving **Frequency** glides briefly rather than jumping.
- The Resonator responds to the Input even before the first note (useful in EG13 FX).
- Its even output can also feed the Ring Mod / Vocoder's carrier or modulator (*Source AUX*). This is the module's raw even (AUX jack) output, scaled by velocity: it does not follow **Spread** and keeps the polarity the module gives that jack.

### Audio Input

Plays the part's Input through each voice: notes open a gate with **Attack** (1 ms to 2 s) and **Release** (5 ms to 5 s); **Gain** sets the level (−24 to +12 dB). The Input is stereo when the host sends stereo.

## Filters

Every voice has a Filter slot after its Source. **Off** passes the sound through unchanged. All filters' first control is the cutoff, on the same scale (20 Hz to 20 kHz), so one modulation amount sweeps every filter alike. Every filter starts fully open, so choosing one does not change the level.

### Analog Filter (the VCV model of Ripples)

Controls: **Frequency** (20 Hz to 20 kHz, default fully open), **Resonance** (default 0) and **Mode**: *LP4* (24 dB/octave low-pass), *LP2* (12 dB low-pass) or *BP2* (12 dB band-pass).

- A circuit model of the analog filter, including its gentle saturation; at full Resonance it self-oscillates.
- The module's output amplifiers invert the signal; EG13 turns it back, so the filtered sound adds to the Dry signal instead of cancelling it.
- Ripples' FM knob and its CV and Gain inputs are not controls here: modulation comes from EG13's modulation matrix (M4), and the LP4 VCA output is LP4 with a fixed level.
- Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state (no jump).
- EG13 builds the Analog Filter with the same fast-math settings as VCV Rack, so it can differ from a strict build by about −90 dBFS (measured), which is inaudible.

### Classic Filters (sst-filters, the filters of the Surge synthesizer)

Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance**, **Type** and **Character**.

- **Type** chooses one of 205 filters: low-, high-, band-pass and notch filters of 12 and 24 dB, ladder models (Legacy, Vintage, Diode), OB-Xd and K35 models, Cutoff Warp and Resonance Warp, the Xpander's pole mixes, Tri-pole, the Fast SVF's shelves and bell, combs, and Sample & Hold. The default is *Vember Classic Lowpass 12 dB Standard*. The list keeps its order in every future version, so a saved type always comes back.
- **Character** is the extra control some types have: OB-Xd 4-pole *Morph*, K35 *Continuous* saturation, the Fast SVF shelves' and bell's gain, and the comb filters' variable mix. Other types ignore it.
- Changing Type takes effect at once and restarts the filter, as switching a filter type on hardware does; it can click on a sounding note.
- Stereo: left and right always run together inside one filter, at no extra cost.

### State Variable Filter

The clean, digital state-variable filter that the Eurorack modules use inside (no module offers it on its own). Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance** (Q 0.5 to 40; the default, Q 0.707, gives a flat passband) and **Mode**: *LP*, *BP* or *HP*. In *BP* mode the peak stays at unity gain whatever the resonance.

As in the original, the cutoff runs a little low near the top of the range (about 15% at 20 kHz when running at 48 kHz), so the Hz readout is approximate up there.

Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state.
