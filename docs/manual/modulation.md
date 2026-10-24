# Modulation

Each part has four **mod slots** and a **modulation matrix** of twelve routes. A mod slot holds a modulation engine (an envelope, an LFO, a random source or a function generator) or is empty. A route takes a **source** (a mod slot's output, or one of the fixed sources such as velocity or the mod wheel), scales it by an **amount** and sends it to a **destination**: any control of the Source, Filter or Shaper, the pitch, the Amp, the pan, the part level, or a control of another mod slot.

In the host, each part has the groups *Part A/Mod 1* … *Part A/Mod 4* (each with *Engine*, *Scope* and P1–P16) and *Part A/Matrix* (*Route 1* … *Route 12*, the amounts). Which source and destination each route uses is part of the patch: it is saved with the project, and the editor arrives with EG13's own interface. A route with no source or no destination does nothing and its amount shows "-". A route saved by a newer version of EG13, with a source, destination or via this version does not know, also does nothing; it is kept and saved back unchanged. So are routes and envelope segments a newer version adds beyond what this one holds, as long as you do not change the route or envelope they belong to.

## How modulation runs

Each modulation engine runs at its module's own rate, as on the hardware (Envelope 31.25 kHz, LFO 62.5 kHz, Random / S&H 32 kHz, Function Generator 48 kHz), whatever your project's sample rate, so its times and rates are the module's. EG13 reads every engine's outputs once every 16 samples (a third of a millisecond at 48 kHz), works out the routes, and moves the results smoothly across the next 16 samples (see [Smoothing](smoothing.md); with host buffers shorter than 16 samples, routes into the Filter and Shaper glide only to the end of each buffer, which can change a render by about 1 % of the level at most, briefly, during a fast change; with longer buffers far less, if at all). A key press reaches a modulation engine within about half a millisecond.

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
- **Sync** *On* locks the LFO to the host's tempo: Frequency then picks a ratio from Tides' table (from one cycle every 16 beats to 16 cycles per beat; 50 % is one cycle per beat) and the cycle lines up with the song position. With the transport stopped it keeps running at the last tempo the host gave (120 BPM if none); when the transport starts it jumps into line with the song. With Sync on, notes do not restart the cycle, Range is ignored, and the notes no longer shape *AD* and *AR*: *AD* plays one attack-decay per cycle, and *AR* rises for the first half of each cycle and falls for the second. The host still shows Frequency's value in semitones with Sync on (it cannot see the Sync setting): read it as the ratio's position.
- In Cycle mode the outputs swing from −100 % to +100 %; in AD and AR they run from 0 to 100 %.
- Defaults: Cycle at 2 Hz, Phase outputs a quarter cycle apart, Sync off.

### Random / S&H (based on Marbles)

Random gates and random voltages, with the module's controls (the knob mappings follow VCV's version of the module). Seven outputs: **T1**, **T2**, **T3** (gates: 0 or 100 %) and **X1**, **X2**, **X3**, **Y** (random values).

- The **T** section makes gates: **T Rate** sets the clock (T2 is the clock itself), **T Model** (*Coin Toss*, *Clusters*, *Drums*) how T1 and T3 share the beats, **T Bias** which of them wins more often, **T Jitter** how loose the timing is, **T Range** (*1x*, *1/4x*, *4x*) the clock's range.
- The **X** section makes values on each T1/T3 gate: **X Spread** the width of the distribution, **X Bias** where it leans, **X Steps** from smooth (low) through random values to notes of **X Scale** (high: *Major*, *Minor*, *Pentatonic*, *Pelog*, *Raag Bhairav*, *Raag Shri*), **X Mode** (*Identical*, *Bump*, *Tilt*) how the three X outputs differ. **Y** is a slower value (one for every four X values) with the same spread, bias and steps.
- **X Range**: *Narrow* and *Positive* give 0 to 100 % (Narrow is the module's 0–2 V, Positive its 0–5 V, both scaled to full range), *Full* gives −100 % to +100 % (default).
- **Deja Vu** loops the random sequences instead of always drawing new ones; **T Deja Vu** and **X Deja Vu** switch it on for each section and **Length** sets the loop's length (1 to 16 steps).
- **Sync** *On* clocks the T section from the host's tempo, one beat per clock, and T Rate then multiplies or divides the beat. It restarts its pattern when the transport starts or jumps; with the transport stopped it keeps the last tempo (120 BPM if none). It takes about two beats to lock.
- A new slot with Random / S&H is per part: the part has one random sequence, and all its voices hear the same values. Set Scope to per voice and each voice gets its own sequence. Different slots and different parts never share a sequence.
- Defaults: VCV's, except X Range *Full*.

### Function Generator (based on Peaks)

One channel of the module in its full-control mode. One output.

- **Mode**: *Envelope* (an ADSR: **Attack**, **Decay**, **Sustain**, **Release**, from 0 to 100 %), *LFO* (**Rate** from 0.03 Hz to 155 Hz; **Waveform** *Sine*, *Triangle*, *Square*, *Steps* or *Noise*; **Wave Shape** folds the sine, skews the triangle, sets the square's width or the number of steps, or sets how smooth the noise is; **Phase** is where a new note restarts the cycle), *Tap LFO* (the notes you play are the taps: the LFO follows their tempo, with Waveform, Wave Shape and Phase as in LFO mode) or *Random* (a random wave at **Rate**; **Smoothness** goes from stepped, through straight lines, to smooth curves).
- Envelope mode has its own controls (Attack, Decay, Sustain, Release), so switching to an LFO mode and back restores them. The LFO modes share theirs: LFO and Random use the same Rate, and LFO and Tap LFO the same Waveform, Wave Shape and Phase. Attack, Decay, Sustain and Release sit at the same positions as the Envelope engine's, so routes into them suit both.
- A new note restarts the envelope, and the LFO at its Phase (per-voice scope).
- The envelope runs from 0 to 100 %; the LFOs swing from −100 % to +100 % (the module's ±5 V).
- Defaults: Envelope with the fastest attack, about 0.4 s decay and release, full sustain; LFO at about 2.2 Hz, sine.

## The matrix

Each of a part's twelve routes has a **source**, a **destination**, an **amount** from −100 % to +100 % (the host parameter *Route 1* … *Route 12*), and optionally a second source, **via**, that scales the amount (for example an LFO into pitch *via* the mod wheel: the wheel sets the vibrato depth). A route adds source × amount × via to its destination; a negative *via* inverts the amount.

**Sources**

- Per note: *Velocity*, *Release Velocity* (how fast the key was let go; 0 until it is), *Note* (the keyboard position: C4 is 0, and 100 % into any filter's cutoff makes the filter follow the keyboard exactly, an octave per octave), *Gate* (100 % while the key is held), *Random* (a new value from 0 to 100 % on each note), *Pressure* (polyphonic aftertouch or MPE pressure), *Slide* (MPE slide, CC74), *Note Bend* (MPE per-note pitch bend: 100 % at the MPE Bend Range).
- Per part: *Mod Wheel* (CC1), *Pitch Bend* (−100 % to +100 %), *Channel Pressure*, *CC A* to *CC D* (four controllers you choose with the part's CC A–D settings), *Transport Phase* (where the song is within the current bar, 0 to 100 %; it keeps running at the last tempo while the transport is stopped).
- Each mod slot's outputs.

**Destinations**: any control of the Source, Filter or Shaper (P1, P2, …), *Pitch*, *Amp*, *Pan*, *Part Level*, any control of the part's Part FX (P1 to P16), and any control of the part's mod slots.

**How routes add up**: every control is a position from 0 to 100 %. The routes into it are added to where its knob is, and the result stops at 0 % and 100 %. So a cutoff at 70 % with +30 % from an envelope opens fully at the envelope's peak; two routes of +80 % into a control at 50 % stop at 100 %. A stepped control (a mode or a model) moves through its options the same way.

- **Pitch** is in semitones: ±100 % is ±24 semitones (so 2 % is about half a semitone). Pitch routes come on top of the part's pitch bend and transpose. Pitch routes sum to at most ±48 semitones.
- **Pan** starts from the centre: −50 % is fully left, +50 % fully right.
- **Amp**: see Amp and voices below.
- **Part Level** adds to the part's Level setting.
- **Part FX controls** add to the Part FX's P1 to P16 the same way. They are part-wide, so per-note sources (velocity, a per-note envelope or LFO) read the most recently played note.

## Mod slots: engine and scope

Each mod slot has an **Engine** (Off, Envelope, LFO, Random / S&H, Function Generator) and a **Scope**:

- *Per voice*: every voice has its own copy of the engine. A voice starting from silence starts its copies afresh, and each new note on a voice retriggers them (an envelope restarts its attack from where it is; an LFO restarts its cycle). What counts as a new note follows the part's *Voice mode*:
  - *Poly*: every note. A key played again while its voice still sounds retriggers that voice.
  - *Mono*: every note, whether or not it overlaps the one before, including the return to a key still held when you let go of the top one.
  - *Legato*: only a note played after every key was up. An overlapping note, or the return to a held key, glides on without retriggering, so the envelope carries on through the phrase. Its *Note Bend*, *Pressure* and *Slide* still switch to the new note's (on an MPE controller, its own channel's).
- *Per part*: one copy for the whole part, which every voice hears. Its gate is held while any key of the part is down and it triggers when the first key goes down after all were up, so a per-part envelope works like a paraphonic synth's single-trigger envelope and a per-part LFO keeps running while you play.

Choosing an engine gives it its own settings, including its scope (Random / S&H starts per part, the others per voice). Each engine remembers its settings in each slot, as the Source, Filter and Shaper engines do. Changing the engine takes effect at once: the new engine starts from the beginning. Changing the scope does too: the copies the slot switches to start from the beginning.

**Routes between mod slots** (for example Mod 1's LFO into Mod 2's Decay) use the value the source had 16 samples earlier. That keeps loops (Mod 1 into Mod 2 into Mod 1, or a slot into its own controls) stable and the order of the slots irrelevant. Routes into everything else use the current value.

If a mod engine ever produces an invalid value, it restarts and its outputs read 0 for that moment (16 samples).

**Part Level** and routes into a per-part slot take per-note sources (velocity, a per-voice mod slot, …) from the most recently started voice. Part Level takes a per-voice mod slot's value from 16 samples earlier.

## Amp and voices

Every voice ends with an **Amp** and a **pan**, after the Shaper.

Modulation is time-aligned with the sound it shapes. The engines take a moment to produce a note's sound (see [Latency](latency.md)), so EG13 holds back each change until the sound it acts on gets there. The Filter and Shaper get their changes as the Source's sound reaches them. The Amp, pan and Part Level get theirs as the voice's finished sound arrives. An envelope on the Amp or the Filter therefore starts exactly with the note's first sound, and a short note keeps its attack.

- If no route goes to *Amp*, the Amp simply follows the key: it opens over 5 ms when the note starts and closes over 5 ms when you let go. That cuts a sound that would otherwise ring on after the key (a struck Resonator, a Macro Oscillator drum): route an envelope to the Amp to shape the release.
- If any route goes to *Amp*, the Amp's level is the sum of those routes (0 to 100 %). The init patch routes Mod 1's envelope to the Amp, scaled by velocity.
- A route to *Pan* moves the voice from the centre (−50 % fully left, +50 % fully right), using the part's own pan law (the centre keeps full level on both sides).
- A voice stops, and is free for a new note, when its note is off and its sound after the Amp has been silent (below −90 dBFS) for 50 ms. It also stops when the sound reaching its Shaper has been silent that long, as before, so a Ring Mod / Vocoder whose internal oscillator would sound forever does not hold the voice.
- In an *Always on* part the Amp stays open (its voice is always on). Its per-voice envelopes and LFOs still follow the keys you hold: the first key down triggers them, so an envelope routed to the Amp turns an always-on drone into a keyed sound.

## Levels

EG13 keeps room for chords and evens out its Sources (gain staging):

- **Voice Gain** (a master setting): each voice plays 12 dB lower (*−12 dB*, the instrument's default), so a chord of eight loud voices does not overload the mix. *0 dB* plays each voice at full level: EG13 FX starts there (it usually plays one voice).
- **Source trims**: each Source is turned up or down at its output, before the Filter, so one held note at the engine's default settings comes out about as loud on every Source: the Digital Oscillator 3 dB down, the Modal Synth and the Resonator 12 dB up, the Macro Oscillator and Audio Input unchanged. This also means the Filter and the Shaper see similar levels from every Source. Switching the Source while a note is held plays the new Source at its own trim from its first sample.
- **Audio Input chords**: every Audio Input voice carries the same Input, so EG13 divides each one by the number of sounding keys (held keys, sustain and Always on count). A chord plays the Input at one voice's level instead of several times louder. The division glides over a few milliseconds when you add or release a key, and a note you have released keeps its level as it fades out, so a released chord does not swell.
- **Soft Clip** (a master setting, on by default): the main and Bus 2 outputs pass unchanged below −3 dBFS; above that they bend smoothly towards full scale and never go over it. A NaN is silenced. Switch it off to hear EG13's output exactly (the host may then clip it).

## In the host

- *Part A/Mod 1* … *Part B/Mod 4*: **Engine** (a stepped list from 0 to 31; values without an engine select Off), **Scope** (*Per voice* / *Per part*) and P1–P16, named after the engine in the slot (unused ones show "(unused)" and "-").
- *Part A/Matrix*, *Part B/Matrix*: **Route 1** … **Route 12**, the routes' amounts from −100 % to +100 %. The text follows the route's destination: semitones for Pitch, percent otherwise, "-" for a route that is not set up.
- Per part: **MPE** (*Off*, *Lower zone*, *Upper zone*), **MPE Bend Range** (default ±48 semitones), **CC A** to **CC D** (the controller numbers of the four CC sources; defaults 2 Breath, 4 Foot, 11 Expression, 16).
- Master: **Voice Gain** (*−12 dB* / *0 dB*) and **Soft Clip** (*Off* / *On*), see Levels.

**Reset to default** in the host sets a mod slot's P to the same value whatever engine is in the slot (a plugin can give each parameter only one default). The values suit every engine: P1 and P2 go to 0 (each engine's first option, or its shortest attack), P3 to P9 to 50 % except P6 to 100 %, and P10 to P16 to 0. That gives the Envelope's and Function Generator's ADSR (1 ms attack, about half-second decay and release, full sustain), the LFO cycling at 2 Hz, and Random / S&H at the module's centre settings with a bipolar range. Exceptions: in the instrument, Mod 3's P3 resets to about 5 Hz (the init patch's vibrato), and in EG13 FX, Part A Mod 1's P3 to about 0.25 Hz. Scope resets to per voice (EG13 FX's Part A Mod 1: per part). A route amount resets to its init patch value (0 for routes the init patch does not use).

## MIDI, MPE and expression

- **Mod wheel** (CC1), **pitch bend**, **channel pressure** and the four **CC** sources come from the part's MIDI channel. Set *CC A* to *CC D* in the host to the controller numbers you want (defaults: 2 Breath, 4 Foot, 11 Expression, 16). EG13's own interface will also let you learn them by moving a control; learning waits for a controller that is not on an MPE note channel and not sustain, bank select, data entry, data increment or decrement, RPN/NRPN or a channel-mode message (CC 120 and up).
- **Polyphonic aftertouch** is each note's *Pressure*. **Release velocity** (how fast you let go) is each note's *Release Velocity*; a note-off without one (a note-on with velocity 0) counts as 64.
- **MPE**: set the part's *MPE* to *Lower zone* (master channel 1, notes on channels 2 to 16) or *Upper zone* (master channel 16, notes on channels 1 to 15). If Part A uses one zone and Part B the other, the lower zone gets channels 2 to 8 and the upper zone 9 to 15. With MPE on, the part's *MIDI Channel* setting is not used.
  - On a note's own channel, pitch bend bends that note (by the *MPE Bend Range*, default ±48 semitones; it also feeds the *Note Bend* source), pressure is that note's *Pressure*, and CC74 is its *Slide*. Values sent before the note (as MPE controllers do) apply to it from the start. Other controllers on a note's channel are ignored, except that sustain and all-notes-off there act on the whole part.
  - On the master channel, pitch bend bends the whole part (by its *Bend Range*), and pressure and controllers are part-wide.
  - In Single mode Part B's MPE setting is ignored, so Part A keeps its whole zone. If both parts choose the same zone, Part A gets it and Part B hears none of its channels.
  - EG13 does not read the MPE Configuration Message: the zones are set by the *MPE* settings alone. If the same key is played on two channels at once, both share one voice.
- Without MPE, CC74 is a part-wide *Slide*. In CLAP hosts, per-note *Tuning* bends a note, per-note *Pressure* is its Pressure and per-note *Brightness* its Slide (other per-note expressions are not used yet). An expression the host sends without a key (to every note) reaches every note on its channel, or every note if it names no channel.

## Tempo

EG13 follows the host's tempo and song position. While the transport plays, tempo-synced engines (the LFO and Random / S&H with Sync on) and the Transport Phase source line up with the song; when it starts, or when you jump to another place in the song, they jump into line. While the transport is stopped they keep running at the last tempo the host gave (120 BPM until the host gives one), so a synced LFO never stops.

- A synced LFO lines up with the song to within about one step of its own clock (62.5 kHz, a few hundredths of a millisecond), not always to the exact sample: at most sample rates the two clocks' steps don't fall on the same instants.
- Small differences between EG13's count and the host's position (less than a 64th note) are corrected in one step without restarting the engines, so a synced LFO's phase can step by up to that much. A bigger difference counts as a jump.
- EG13 counts the song position in samples, so the timing of tempo-synced modulation is the same whatever buffer size the host uses (as long as the host reports its position consistently). What a route does with it can still differ by a tiny amount at buffers shorter than 16 samples (see [Smoothing](smoothing.md)).
- A host tempo above 1000 BPM plays as 1000 BPM, and one below 1 BPM as 1 BPM. A tempo of zero or below, or a tempo or song position that is not a number, is ignored: the last good value stays.
