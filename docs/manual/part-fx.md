# Part FX

Each part has one **Part FX** slot after its voices. It holds an effect that processes the part's sound before level, pan and the Mono switch: the sum of the part's voices goes through the Part FX, then the part's Level, Pan and Mono (see [Parts and routing](parts-and-routing.md)). The slot has a *Type* selector and 16 controls, P1 to P16, like the Source slot. Types are stepped parameters with a fixed range of 0 to 31; a value with no engine selects **Off**.

Engines: **Off**, **Granular**, **Granular Delay**, **Resonator** and **Reverb**. Each one is described in its own section below.

## The Part FX slot

- **Off** passes the part's sound unchanged, delayed like every effect (below). While Part FX is Off, the *Input* setting (below) is ignored, so Off never lets the Input leak into the part.
- **DC filter.** Every effect but Off is followed by a DC filter, the same as after the Shaper ([Engines](engines.md)): a high-pass at 8 Hz (second order), one per part, with nothing to set. Some effects (Granular Delay's *Dirty tape* quality, for one) put out a small steady offset, and anything the voices carry ends up there too; the filter takes it out before the part's Level, so it never reaches the master. It changes the sound by less than 0.01 dB above 40 Hz (at 220 Hz it shifts the phase by about 3 degrees, so a dry copy mixed back in, such as EG13 FX's *Dry*, lines up within a fraction of a dB). It fades with the effect when you change the type. With Part FX *Off* there is no filter and the part's sound passes bit for bit.
- **Every effect, Off included, has the same delay**, the slowest effect's (Granular's rate conversion; about 5.5 ms in Live and 11 ms in Studio at 48 kHz, whole-block wait included, see [Latency](latency.md)). The part's sound always leaves the part at the same moment, whichever effect is chosen, so changing the effect never moves the sound in time.
- P1 is the dry/wet **Mix** of every engine, so "Part FX P1" always means dry/wet. The other controls are named by the engine and show "(unused)" and "-" beyond the engine's own controls.
- **Reset to default in the host** sets each P to one value whatever the *Type*: P1 to P4 50 %, P5 75 %, P6 50 %, P7 to P12 0 %, P13 to P16 50 %. Each engine's own defaults are these values, so resetting never changes an effect from its defaults.
- Stepped choices inside an engine (Mode, Quality, Model and so on) are controls too: P's read in 16 fixed steps. Steps past the last option read as the last option, and new options are only ever added at the end. Switches such as Freeze are on at 50 % or more.
- The controls can be modulated: the modulation matrix can target any Part FX P (see [Modulation](modulation.md)).
- The effect keeps running while no voice sounds, so frozen buffers, tails and an Always-on part's Input keep playing.
- Changing *Type* keeps the old effect running for the slot's delay and then crossfades to the new one over 10 ms. The new effect starts empty and the old effect's tail is cut: reverb tails, delay repeats and frozen buffers do not carry over to the new effect. A note that is still held is not replayed into the new effect; it hears the next note.
- If an effect ever produces an invalid signal (a numerical blow-up), the part is muted for that moment (at most 16 samples) and the effect starts afresh, empty.
- Defaults: both parts start with Part FX **Off** in the instrument. In EG13 FX, Part A starts with **Granular** and Part B with **Off**. The *Input* setting starts at *Part mix* everywhere.

## Input

Each part has a *Part FX Input* setting:

- **Part mix:** the sum of the part's voices goes through the effect.
- **Input:** only the part's Input (as its Input source routes it: External, Other part or Both; in the instrument only Other part carries a signal, see [Parts and routing](parts-and-routing.md#input-and-input-source)) goes through the effect. The part's voices skip the effect and are still heard. They are delayed by the effect's delay so they stay in time with it.
- **Part mix + Input:** both go through the effect.

With nothing on the Input (External in the instrument or with nothing connected to EG13 FX, or Other part in Single mode) the effect receives silence in *Input* mode, and the voices are still heard.

The Input is delayed by the voices' own delay before it enters the effect, so the processed Input leaves the part in time with the voices (and, in EG13 FX, with Dry): to the sample for notes, and to within about half a sample where an effect's own rate conversion adds a fraction (measured up to 0.43 of a sample at 44.1 kHz).

Changing the *Input* setting while sound plays is smooth: what goes into the effect fades over 10 ms, and in *Input* mode the voices' path around the effect fades in step with it, after the effect's delay, so the voices are neither dropped nor doubled on the way. While Part FX is Off the setting has no effect; switching from Off to an effect in *Input* mode keeps the voices playing throughout, and only the effect fades in.

## Notes and triggers

Every note the part plays is also sent to the Part FX: a voice trigger, not a legato glide, and on an Always-on part every key press. Notes that start in the same 16-sample step (a chord) count as one trigger; a chord split across a step boundary is two. Engines use it as follows:

- **Granular:** each note is one trigger, as on the module's TRIG input (see [Granular](#granular)).
- **Granular Delay:** with *Seed* set to *Notes*, each note is a seed and starts one grain (see [Granular Delay](#granular-delay)).
- **Resonator:** each note strums it at that note's pitch.
- **Reverb:** ignores notes.

The note, the triggers and the controls (P1 to P16, as you set them and as the modulation matrix moves them) take effect after the voices' delay, on a 16-sample grid, so they line up with the sound they act on. The note is the one the part's most recently started voice plays (on an Always-on part, its voice's note), plus Transpose and the part's pitch bend; before any note it is the Base note.

## Granular

Clouds, the granular texture processor, as the VCV Rack port runs it. It records the last few seconds of its input into a buffer and plays it back as grains, a time-stretched stream, a looping delay or a spectral texture. It runs at the module's own rate of 32 kHz, so everything that passes through it, the dry part included, loses the treble above about 16 kHz, as on the module.

- **Blend** (P1): the module's own dry/wet crossfade. 0 % is the dry sound only, 100 % the processed sound only, 50 % (the default) an equal-power mix. Clouds' crossfade keeps the dry sound about 9 dB quieter than it came in, even at 0 %: its crossfade gives the dry part 71 % at 0 %, and the output stage halves the level before a gentle soft limiter. The processed sound is made 20 % louder before the crossfade. This is the module's own law, kept as it is.
- **Position** (P2): where in the recorded buffer the sound is taken from, from the most recent (0 %) to the oldest (100 %). In *Looping delay* it is the delay time.
- **Size** (P3): the length of each grain (or of the stretch window, or of the pitch shifter's window in *Looping delay*). In *Spectral* it warps the spectrum.
- **Pitch** (P4): transposition from −24 to +24 semitones; 50 % (the default) is no change. It does not follow the notes you play; a modulation route from Note can make it.
- **Density** (P5): how many grains play. Above about 53 % grains start at random, more and more of them towards 100 %; below about 47 % they start at a regular rate, faster towards 0 %. Between 47 % and 53 % no grains start on their own, and only notes start them (see below). That dead zone is why Density defaults to 75 % rather than the module's 50 %: at 50 % the effect would only pass the dry sound. In *Stretch* and *Looping delay* Density sets the amount of diffusion; in *Spectral* how often the spectrum is refreshed and, away from 50 %, how much its phases are randomised.
- **Texture** (P6): in *Granular*, the grains' envelope, from hard-edged (0 %) to smooth (75 %), with diffusion added above 75 %. In *Stretch* and *Looping delay* it is a filter: low-pass below 50 %, high-pass above. In *Spectral* it quantises the spectrum.
- **Feedback** (P7): how much of the processed sound goes back into the buffer. It is filtered to keep low frequencies from building up, and it fades away while Freeze is on.
- **Reverb** (P8): Clouds' own reverb on the processed sound (the dry part is not reverberated, as on the module). Feedback also brightens the reverb, and while Freeze is on it adds more reverb.
- **Freeze** (P9): on (50 % or more), the buffer stops recording and the effect keeps playing what it holds.
- **Mode** (P10): *Granular*, *Stretch*, *Looping delay* or *Spectral*, in 16 fixed steps (see above).
  - *Granular*: overlapping grains taken from the buffer.
  - *Stretch*: the buffer played as a continuous stream that can be slowed down, sped up or transposed without the grain texture. (EG13 restores one line of the module code that the newest firmware dropped, which had left this mode silent.)
  - *Looping delay*: a delay with a pitch shifter; with Freeze on it loops what it holds.
  - *Spectral*: the sound's spectrum is captured and resynthesised, smeared, warped and quantised.
- **Quality** (P11): *16-bit stereo* (the default), *16-bit mono*, *8-bit stereo* or *8-bit mono*, in 16 fixed steps. Mono and 8-bit trade fidelity for a longer buffer: about 1, 2, 4 and 7 seconds respectively. The 8-bit qualities record at half the rate (16 kHz) with a grainy, compressed 8-bit sound; mono mixes left and right before recording.
- **Spread** (P12): in *Granular*, how far each grain is panned at random across the stereo field; 0 % (the default) keeps them centred. The other modes ignore it.
- **In Gain** (P13): the input level into the buffer. 50 % (the default) is unity: a full-scale signal (±1.0) just reaches the module's full scale. 100 % is +6 dB; louder signals are clipped hard at full scale, as the VCV port clips its input. 0 % is silence, dry part included.
- P14 to P16 are unused.

**Notes.** Each note the part plays is one trigger, as a pulse into the module's TRIG input (one 32-sample block at 32 kHz). In *Granular* it starts one grain, which is how notes play grains inside the Density dead zone; in *Stretch* it restarts the playback window; in *Looping delay* the time between two notes sets the loop length; in *Spectral* it adds a glitch.

**Changing Mode or Quality** fades the effect's output out over 10 ms, switches, then fades it back in over 10 ms (the module switches at once and briefly drops out). Changing Quality, or switching to or from *Spectral*, clears the buffer, as on the module; switching between *Granular*, *Stretch* and *Looping delay* keeps it.

## Granular Delay

Beads, the granular delay, as its firmware runs it. It records its input into a long buffer and plays it back as grains, or, with Size fully up, as a delay. It runs at the rate of its *Quality* (below), as the module does, in the module's own 4 kHz control blocks.

- **Dry/Wet** (P1): the module's own crossfade. 0 % is the dry sound only, about 2.5 dB quieter than it came in; 100 % is the processed sound only, made 1.5 times louder; in between, both stay up longer than in a straight crossfade, as on the module. Even at 0 % the dry sound passes through the Quality's sound (its rate, and its bit depth in *Vintage digital* and *Dirty tape*) and the output limiter.
- **Time** (P2): how far back in the buffer the grains come from, from the most recent (0 %) to the oldest (100 %). At the default 50 % that is half the buffer back, so after you start playing there are no grains until the buffer has filled that far (2 seconds in *Digital*).
- **Size** (P3): how long each grain lasts. Below a third grains play backwards. Held fully up (above 99 %) for about 1.5 seconds, the effect turns into a delay with repeats, fading over from the grains, as on the module; turning Size down again fades back to grains.
- **Pitch** (P4): transposition from −24 to +24 semitones. 50 % (the default) is no change. The control sticks to the useful intervals on the way: 1, 3, 4, 7, 12 and 24 semitones, up and down, as the module's knob does. It does not follow the notes you play.
- **Density** (P5): how often grains start. With *Seed* on *Free*: below about 48 % at a steady rate, faster towards 0 %; above about 52 % at random moments, more of them towards 100 %; between 48 % and 52 % no grains start. That dead zone is why Density defaults to 75 % rather than the module's 50 %. With *Seed* on *Notes*, see below.
- **Shape** (P6): the grains' envelope, from short, clicky attacks to smooth, overlapping swells.
- **Feedback** (P7): how much of the processed sound goes back into the buffer.
- **Reverb** (P8): Beads' own reverb on the output, dry sound included. In every Quality except *Digital* it rings longer and is darker (a lower brightness inside its loop), as on the module.
- **Freeze** (P9): on (50 % or more), the buffer stops recording and the grains keep playing what it holds.
- **Quality** (P10): the module's four recording qualities, in 16 fixed steps:
  - *Digital* (the default): 48 kHz, 16-bit, clean; a buffer of about 4 seconds.
  - *Vintage digital*: 32 kHz, 12-bit, so treble above about 16 kHz is lost and the dry sound is 12-bit too; about 8 seconds.
  - *Clean tape*: 48 kHz, with the grains recorded at 24 kHz in 12 bits (treble above 12 kHz is lost in the grains, not in the dry sound); about 11 seconds.
  - *Dirty tape*: 24 kHz, 8-bit with the module's companding, so everything, the dry sound included, loses the treble above 12 kHz and gains grit; the whole output wobbles with wow and flutter, and the input is soft-clipped. About 16 seconds.
- **Seed** (P11): where grains get their cue.
  - *Free* (the default): nothing is patched into the module's SEED input and its SEED button is latched, as the module starts up, so Density alone starts grains (see Density).
  - *Notes*: every note the part plays is a 1 ms pulse into SEED, with the button released. Each note starts one grain, at any Density, the dead zone included; Density then plays almost no part. The grain starts with the sound that was playing when the note came, so a short Time catches the note's attack.
- P12 is unused.
- **Time Random**, **Size Random**, **Shape Random** and **Pitch Random** (P13 to P16): the module's four randomisation knobs, one per control, as they work with nothing patched into its CV inputs. 50 % (the default) is none. Above 50 % each grain takes a random value around the control's setting, spreading wider towards 100 %. Below 50 % only some grains are randomised, but by larger amounts, more often towards 0 %.

**Changing Quality** fades the effect's output out over 10 ms, switches the module to the new Quality (which clears the buffer, as on the module), waits in silence while the new rate's sound comes through EG13's rate converters (the effect's delay: about 5.5 ms in Live and 11 ms in Studio at 48 kHz), then fades back in over 10 ms. In all, the output is silent for about 30 ms at 48 kHz in Studio (10 ms out, the delay, 10 ms in), and for about 25 ms in Live. The effect's delay is the same in every Quality: the faster paths are delayed to match the slowest. The module itself mutes for a quarter of a second.

The module's automatic input level, its wavetable synth mode, its assignable CV input, its gate output and its mono buffer setting are left out; see [Known limitations](limitations.md).

## Resonator

Based on Rings, the same resonator as the Resonator Source (see [Engines](engines.md)), with its Exciter fixed to *Input*: the sound going through the effect (the part's mix, or its Input, as the *Input* setting says) excites the resonator, which rings at the pitch of the part's notes. The left and right channels are averaged into one (half their sum) before they reach the resonator, so a sound that is the same in both channels reaches it at its own level and a sound in anti-phase cancels. It runs at the module's own rate of 48 kHz, so at a 48 kHz host nothing is lost and it adds only its 24-sample block (half a millisecond) of delay.

- **Mix** (P1): the dry sound against the resonator. 0 % is the dry sound only, 100 % the resonator only, 50 % (the default) an equal mix of the two. The mix is a straight crossfade and moves smoothly, without a click.
- **Structure** (P2), **Brightness** (P3), **Damping** (P4) and **Position** (P6): as on the Resonator Source, and what they do depends on the Model. Brightness and Damping set how bright the sound is and how fast it dies away; Position is where the resonator is excited and listened to.
- **Spread** (P5): how far apart the resonator's two outputs are placed. 100 % is full stereo, 0 % puts the same sound in both channels. It is EG13's own control, not on the module. It defaults to 75 %.
- **Model** (P10): *Modal*, *Sympathetic*, *String*, *FM Voice*, *Sympathetic Q* or *String + Reverb*, in 16 fixed steps (see above). Changing it switches at once.
- **Polyphony** (P11): 1, 2 or 4 strings, in 16 fixed steps. With more than one, each note rings on its own string until later notes take it over.
- **Tune** (P13): transposes the resonator from -12 to +12 semitones; 50 % (the default) is no change. It glides briefly.
- P7 to P9, P12 and P14 to P16 are unused.

How it is played:

- **Notes tune it.** The resonator rings at the note of the part's most recently started voice, plus Transpose, the part's pitch bend and *Tune*. Before any note has been played, and on an *Always-on* part while no key is held, it rings at the part's Base note; on an *Always-on* part it follows the held key.
- **Each note strums it.** Every note the part plays (on an *Always-on* part, every key press) strikes the resonator at full level, whatever the note's velocity, and with Polyphony above 1 moves on to the next string. With the Exciter on *Input* the strum does not make a sound by itself: the resonator rings from the sound going in, so with nothing on the effect's input a new note only sets the pitch. A glide to a legato note retunes the resonator without a strum.

The module's internal exciter (the strike on a note), its Exciter *Internal* and *Both* settings and its FM input are left out here: use the Resonator Source for those.

## Reverb

The reverb at the end of Elements (the module behind the Modal Synth), used on its own, with its controls brought out separately. It runs at the module's own rate of 32 kHz, so, as on the module, everything that passes through it, the dry part included, loses the very top of the treble.

- **Amount** (P1): the module's own dry/wet crossfade. 0 % is the dry sound only, 100 % the reverb only, 50 % (the default) an equal mix.
- **Time** (P2): how long the tail rings, from short (0 %) to very long (100 %). The range is the one Clouds uses for its reverb of the same design.
- **Diffusion** (P3): how quickly the echoes smear into a smooth wash. Low settings keep more of the separate early echoes. The range is Elements' own, so it is a subtle control.
- **Brightness** (P4): a low-pass inside the reverb's loop. Low settings make the tail darker as it decays; high settings keep it bright. The range is Clouds' too.
- **Freeze** (P9): on (50 % or more), the reverb holds its current tail indefinitely and ignores new input, as Elements does at the top of its Space range. While frozen, Time and Brightness have no effect (the tail neither decays nor darkens); Amount still sets how much of the frozen tail you hear against the dry sound. Turning Freeze off lets the tail decay again with the current Time.
- P5 to P8 and P10 to P16 are unused.

On the module one *Space* knob moves the amount and the time together, and the diffusion and brightness are fixed by each patch. Here they are four separate controls, so any of them can be set or modulated on its own. The sound enters the reverb at the module's fixed input gain (0.2). The reverb's input is the sum of the left and right channels, as on the module, so a stereo sound in anti-phase between the channels cancels and does not reach the reverb (the dry part, which is mixed back in by Amount, is unaffected).

## Levels

Each effect is trimmed so that, at its default settings, it is about as loud as the part without an effect. Measured with one held note (Macro Oscillator, velocity 1, Part FX at its default settings; RMS level against Off, the same in Live and Studio apart from Granular's random grains):

| Effect | Trim | Level against Off |
|---|---|---|
| Granular | +5.0 dB | about -0.3 to -0.8 dB (peaks up to about +3.9 dB; Granular's grains are spiky) |
| Granular Delay | +5.0 dB | -0.03 dB |
| Resonator | +0.5 dB | -0.4 dB |
| Reverb | +2.0 dB | -0.7 dB |

The trims are fixed and apply to the effect's whole output inside the slot, so they follow a Type change's crossfade. The reason for Granular's large trim is the module's own level law: at 0 % Blend it passes the dry sound about 9 dB quieter than it came in. Moving a control changes the level as it does on the module; for example Reverb's level depends on its Amount and Time, and the Resonator's on how much of the input excites it.
