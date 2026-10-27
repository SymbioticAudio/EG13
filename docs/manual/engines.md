# Engines

Every engine shows its controls as P1, P2, … in the host. A slot has more parameters than most engines use: the unused ones are named "(unused)" and show "-". The Source slot has 16 parameters, the Filter slot 4 and the Shaper slot 12, plus a selector for the engine in each slot (the Filter's and Shaper's are called *Type*).

In the host, each part has these parameter groups: *Part A/Source*, *Part A/Filter*, *Part A/Shaper* (and the same for *Part B*), plus *Part A* and *Part B* for the part settings, and *Part A/Mod 1* … *Part A/Mod 4* and *Part A/Matrix* for modulation (see [Modulation](modulation.md), *In the host*). The Source's selector is called *Engine*; the Filter's and Shaper's are called *Type*. Types are stepped parameters with a fixed range of 0 to 31, so adding engines later never changes the range; a value with no engine selects **Off**. A Filter or Shaper P1.. is named by its engine (for example *Frequency* for the Analog Filter) and shows "(unused)" and "-" beyond the engine's own controls.

Defaults: in the instrument, every part starts with Filter *Type* **State Variable Filter** at a cutoff of 70 % (about 2.5 kHz; the init patch's envelope opens it, see [Modulation](modulation.md)) and a resonance of Q 0.5 and Shaper *Type* **Off**. EG13 FX's Part A starts with Filter *Type* set to **Analog Filter** (with the Analog Filter's own default controls), after the Audio Input source; its Part B starts with Filter and Shaper **Off**. Choosing an engine gives it its own default controls (or, later in a project, the settings it had when you last left it).

**Reset to default in the host** (a double-click in most hosts) works differently for the Filter and Shaper controls: a plugin can give each host parameter only one default value, so a reset sets a Filter or Shaper P to the same value whatever *Type* is selected. The values are chosen so that resetting a slot's controls gives a usable sound on every engine:

- **Filter** P1 to P4: 100 % (in the instrument 70 %, the init patch's cutoff), 0 %, the first option, 50 %. Every filter then has no resonance. In EG13 FX it is also fully open, so the level does not change; in the instrument the cutoff sits at the init patch's 70 % (about 2.5 kHz). The filters then play as the Analog Filter in LP4 (its own defaults), Classic Filters as *Vember Classic Lowpass 12 dB Standard* with Character centred (its own defaults), the State Variable Filter in LP with Q 0.5 (its own default resonance is Q 0.71).
- **Shaper** P1 to P12: the EQ is flat (every gain 0 dB, Input Pad *Off*, Output *EQ*); its frequencies and Qs sit at the values the Ring Mod / Vocoder needs, which changes nothing while the gains are 0 dB. The Ring Mod / Vocoder gets its own defaults (*Analog RM*, Timbre 50 %, Carrier *Internal Osc*, Modulator *Voice*, Osc Pitch 0, Mode *Modulation*, Carrier Level 47 %), except Mod Level, which resets to 50 % instead of 47 %. It is then about as loud as at its own defaults (+5.1 dB over a 110 Hz saw, against +4.6 dB).

A slot's *Type* resets to *Off*, except the Filter's in the instrument (*State Variable Filter*) and in EG13 FX's Part A (*Analog Filter*). A new project's *Off* slots hold these reset values; *Off* ignores them.

The Part FX slot and its engines are described in [Part FX](part-fx.md).

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
- Velocity sets the Resonator's level. A new note's velocity fades in with a time constant of 5 ms (about 20 ms to settle), so it does not click; with Polyphony above 1 it also changes the level of earlier strings still ringing, as there is one level for the whole Resonator.
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
- Ripples' FM knob and its CV and Gain inputs are not controls here: modulation will come from EG13's modulation matrix in a later version, and the LP4 VCA output is LP4 with a fixed level.
- Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state (no jump).
- EG13 builds the Analog Filter with the same fast-math settings as VCV Rack, so it can differ from a strict build by about −90 dBFS (measured), which is inaudible.

### Classic Filters (sst-filters, the filters of the Surge synthesizer)

Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance**, **Model** and **Character**.

- **Model** chooses one of 205 filter types: low-, high-, band-pass and notch filters of 12 and 24 dB, ladder models (Legacy, Vintage, Diode), OB-Xd and K35 models, Cutoff Warp and Resonance Warp, the Xpander's pole mixes, Tri-pole, the Fast SVF's shelves and bell, combs, and Sample & Hold. The default is *Vember Classic Lowpass 12 dB Standard*. The list keeps its order in every future version, so a saved type always comes back. (Model is named so that it cannot be mistaken for the Filter slot's *Type*, which chooses the engine.)
- **Character** is the extra control some types have: OB-Xd 4-pole *Morph*, K35 *Continuous* saturation, the Fast SVF shelves' and bell's gain, and the comb filters' variable mix. Other types ignore it.
- Changing Model takes effect at once and restarts the filter, as switching a filter type on hardware does; it can click on a sounding note.
- Stereo: left and right always run together inside one filter, at no extra cost.

### State Variable Filter

The clean, digital state-variable filter that the Eurorack modules use inside (no module offers it on its own). Controls: **Cutoff** (20 Hz to 20 kHz, default fully open), **Resonance** (Q 0.5 to 40; the default, Q 0.707, gives a flat passband) and **Mode**: *LP*, *BP* or *HP*. In *BP* mode the peak stays at unity gain whatever the resonance.

As in the original, the cutoff runs a little low near the top of the range (about 15% at 20 kHz when running at 48 kHz), so the Hz readout is approximate up there.

Stereo: while the voice is stereo, a second filter runs for the right channel, starting from the left filter's exact state.

## Shapers

Every voice has a Shaper slot after its Filter. **Off** passes the sound through unchanged.

**DC filter.** Every Shaper but Off is followed by a DC filter: a high-pass at 8 Hz (second order), on each voice, with nothing to set. The Ring Mod / Vocoder's fold and ring modulation can put out a steady offset (up to about −13 dBFS) that nothing after the Shaper would remove, and that a long reverb or the master would carry. The filter takes it below −60 dBFS, and changes the sound by less than 0.01 dB above 40 Hz (the EQ's response is unchanged there). It is part of the engine's sound inside the slot, so it fades with the engine when you change the Shaper type. With the Shaper *Off* there is no filter and the sound passes bit for bit. The Ring Mod / Vocoder and the EQ themselves still match their originals exactly (the filter comes after them).

### Ring Mod / Vocoder (based on Warps)

Warps combines two signals, a **carrier** and a **modulator**, in one of its algorithms. EG13 routes them as the module does: there are two inputs, CARRIER and MODULATOR, and when the internal oscillator is on it replaces whatever would feed CARRIER. Two controls choose what feeds each input.

- **Carrier**:
  - *Internal Osc* (default): Warps' own oscillator, playing the voice's pitch plus **Osc Pitch** (±24 semitones). **Osc Shape** picks one of the module's three oscillator modes. Warps chooses the waveform itself for each algorithm region: *Sine / Saw*, *Triangle / Pulse* or *Saw / Noise*, where the first name is the waveform for the cross-modulation algorithms and the second for the vocoder. Osc Pitch and Osc Shape only matter with this setting. As on the module, **Carrier Level** has no effect while the internal oscillator plays.
  - *Voice*: the voice itself, meaning the sound reaching the Shaper from this voice's Source and Filter. It plays the part of the cable plugged into the module's input.
  - *Source AUX*: the Source's second output: the Macro Oscillator's AUX, the Resonator's even partials. Other sources give their main output.
  - *Input*: the part's Input, as its Input source setting routes it.
  - *Other part*: the other part's output.
- **Modulator**: *Voice* (default), *Source AUX*, *Input* or *Other part*, with the same meanings. Voice into the modulator with the internal oscillator as carrier is the classic way to play Warps; for example, the vocoder then puts the voice's formants on the oscillator. Voice into both inputs is allowed: it is self-modulation.
- The routing and its defaults are provisional. They may be revisited when patching arrives in a later version. New options are only ever added at the end of each list, so a saved patch keeps its routing. A change of default would apply to new patches only.
- If a chosen signal isn't there (no sidechain, a silent part), that input is silent.
- The side signals are time-aligned with the voice: *Source AUX* and *Input* reach Warps at the same moment as the voice's own sound (the Input is delayed by the Source's latency, as the voice is). *Other part* is the other part's finished output (whether or not that part is heard, see Parts and routing), so it arrives one Shaper latency behind the voice (plus the 64-sample feed from Part B to Part A).
- **Algorithm** is Warps' big knob. The display names nine positions, after what the module does there: *Crossfade*, *Fold*, *Analog RM*, *Digital RM*, *XOR*, *Compare*, then the vocoder: *Vocoder*, *Vocoder (long release)* and *Vocoder (frozen)*. Turning clockwise through the vocoder lengthens the release of its envelope followers, and fully clockwise it freezes the modulator's formants. Starting a note at *Vocoder (frozen)*, the vocoder is silent, as on the module: it freezes the formants it has, and from a reset it has none. The default is *Analog RM*, the ring modulator the engine is named after. **Timbre** is the algorithm's own control. At its very top (and the frequency shifter's phase shift at Algorithm's top) EG13 stops a hair short of 100 % (by 0.0015 %, inaudible): at exactly 100 % the original firmware reads one value past the end of a table.
- The knob is continuous and the display shows the nearest position. Between positions the sound blends: neighbouring cross-modulations crossfade, the stretch from *Compare* to *Vocoder* passes through the plain Modulator signal, and the vocoder's release changes gradually (it freezes only in the last few percent).
- **Carrier Level** and **Mod Level** drive Warps' input amplifiers. Their default, 47 %, passes a full-scale voice at unity gain without saturating (measured: +0.03 dB, 0.06 % distortion). Turn them up for the module's overdriven, squarer character (at 100 % the amplifier adds about 12 dB and heavy distortion).
- **Mode** *Freq shifter* is the module's hidden frequency-shifter mode.
  - Timbre mixes the upper and lower sidebands, Carrier Level sets feedback and Mod Level the dry/wet mix, whatever the Carrier.
  - With Carrier = Internal Osc, it shifts the Modulator signal (the voice, by default) by an amount set by Algorithm alone, from no shift at the centre to about 2 kHz down or up at the ends. Osc Pitch has no effect here; Osc Shape picks the waveform of the shifting oscillator.
  - With any other Carrier, it multiplies the carrier and modulator into a single sideband instead, and Algorithm sets the carrier's phase shift.
- Warps runs at 96 kHz in blocks of 60 samples. That adds to EG13's fixed latency ([Latency](latency.md)).
- Stereo: while the voice is stereo, a second Warps runs for the right channel. It cannot copy the left one's state, so it starts fresh and fades in over 10 ms. The Source AUX, Input and Other part signals are mono (left and right summed).

### EQ (the VCV model of Shelves)

A four-band equalizer modelled on the analog circuit: **High Freq** / **High Gain** (high shelf), **Mid 1 Freq** / **Mid 1 Gain** / **Mid 1 Q**, **Mid 2 Freq** / **Mid 2 Gain** / **Mid 2 Q** (two bells), **Low Freq** / **Low Gain** (low shelf). Frequencies run 20 Hz to 20 kHz, gains ±18 dB, Q 0.5 to 40. At the defaults (gains 0 dB) it is flat.

- **Input Pad** (*Off* / *−6 dB*) is the module's pre-gain option: the input goes in 6 dB lower, leaving headroom before the circuit clips, and the output is 6 dB lower too. The circuit clips at about ±10.5 V inside, which is about twice EG13's full scale at the input: with high gains (up to +18 dB) a loud input clips, and the output can briefly ring above full scale.
- **Output** chooses what you hear: the *EQ*, or one of the module's six filter outputs: *Mid 1 LP*, *Mid 1 BP*, *Mid 1 HP*, *Mid 2 LP*, *Mid 2 BP*, *Mid 2 HP*, which turns the EQ into a multimode filter. A change of Output takes effect at once, like moving the cable on the module, so it can click on a sounding note.
- Shelves' CV inputs are not controls here: modulation will come from EG13's modulation matrix in a later version.
- Stereo: while the voice is stereo, a second EQ runs for the right channel, starting from the left one's exact state.
- EG13 builds the EQ with the same fast-math settings as VCV Rack, so it can differ from a strict build by about −95 dBFS (measured), which is inaudible.
