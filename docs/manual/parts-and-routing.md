# Parts and routing

## Parts and voices

EG13 has two parts, A and B. Each plays up to eight voices. Each voice runs Source → Filter → Shaper → Amp, then its pan. Each part also has four mod slots and a modulation matrix ([Modulation](modulation.md)). Every engine of every slot is ready in every voice (a per-part mod slot's engines once per part), so changing an engine never loads anything.

The master **Mode** decides which notes reach which part: *Single* (only A plays), *Layer* (both play every note), *Split* (A below the split note, B from it up) or *Multi* (each part on its own MIDI channel). In Single mode Part B is silent but still runs, so it can feed Part A (see below).

A voice stops (and becomes free for a new note) when its note is released and either its sound after the Amp, or the sound reaching its Shaper, has been silent (below −90 dBFS) for 50 ms (the second check waits for the Shaper's latency too). The second check exists because the Ring Mod / Vocoder's internal oscillator keeps sounding after the note has died. See [Modulation](modulation.md) for the Amp.

If an engine ever produces an invalid signal (a numerical blow-up), the voice is muted for a moment and only the slots whose output went invalid (Source, Filter or Shaper) start their engine afresh; the others carry on. A held note continues; if the Source itself was restarted, the note starts again from its attack.

## Engine switching

Each engine of each slot (Source, Filter, Shaper) remembers its own settings in each part. Switching Source from Macro Oscillator to Digital Oscillator and back brings back the Macro Oscillator exactly as you left it. The host's parameters change to the new engine's values; this change is not recorded as automation. The same holds for the Filter and Shaper *Type*: switching from Analog Filter to Classic Filters and back brings the Analog Filter back as you left it, and the host shows each slot's P values named after its current engine.

Switching the **Source** engine is a hard cut: the old engine stops at once, and a held note restarts on the new engine after the plugin's latency.

Changing the **Filter** or **Shaper** type is smooth: while a note sounds, the old engine keeps playing with its last settings until the new one's sound arrives, then the two crossfade over 10 ms. The timing never changes, because every Filter and every Shaper (Off included) is delayed to the same latency. A Ring Mod / Vocoder that is fading out keeps its own Carrier and Modulator signals until it is silent.

If you change the type again while a crossfade is under way (for example while the type is automated), the new choice waits until that crossfade has finished, then crossfades in the same way; only the last choice made during the crossfade is played. Meanwhile the host already shows the new choice's settings, and the engine fading in keeps the settings it had; changes you make to those settings while the choice waits, by hand or by automation, are heard when its crossfade starts. Changing it back to the engine that is fading in cancels the waiting choice. A voice that starts a new note from silence goes straight to the latest choice, with no crossfade, and so does a change made right after EG13 resets its voices (for example when the sample rate changes): there is nothing playing to crossfade from.

## Stereo

The voice chain is stereo. A Filter or Shaper runs a second, right-channel copy only while its input is stereo, which saves CPU for mono sounds. (Classic Filters always runs both channels together, at no extra cost.)

- The input counts as stereo from the first moment left and right differ. It goes back to mono after 50 ms in which they are identical; silence counts as identical.
- When stereo starts, the right copy continues exactly where the left one is, so nothing jumps. The Ring Mod / Vocoder can't copy its state, so its right copy fades in over 10 ms instead.
- When stereo stops, the right channel fades back to the left one over 10 ms.

## Input and Input source

The plugin's audio input (the instrument's sidechain, or EG13 FX's main input) is the **Input**. When nothing is connected, the Input is silent. Each part has an **Input source** setting:

- *External*: the plugin's Input.
- *Other part*: the other part's output.
- *Both*: the two summed.

Part A runs before Part B. B therefore hears A with no extra delay, while A hears B 64 samples later (or one host block, if the host's blocks are shorter). Every signal fed from one part to the other passes through a soft limiter: it is untouched up to full scale and never exceeds twice full scale, so a loop between the parts cannot run away.

The Ring Mod / Vocoder can also take the part's Input or the other part's output as its carrier or modulator: the other part's output comes with the same 64-sample feed delay (Part B to Part A) and the same limiter, and the Input is delayed to line up with the voice (see Engines). Its *Other part* signal is the other part's output whether or not that part is heard: in *Single* mode Part B is muted but still runs, so a Part B set to *Always on* still reaches Part A's Ring Mod, while a Part B playing nothing gives silence.

Each part's **Output** goes to *Main*, to *Bus 2* (the instrument's second output) or to *Other part only*. With *Other part only* the part is not heard; it only feeds the other part.

**Dry** mixes the Input into the main output, delayed by the plugin's latency so it lines up with the processed sound.

## Default patches

- **EG13:** both parts play the Macro Oscillator through the **State Variable Filter** (low-pass at 70 %, about 2.5 kHz, resonance Q 0.5) and Shaper *Off*, with modulation (see [Modulation](modulation.md)):
  - Mod 1: Envelope (ADSR), per voice, routed to the **Amp**, scaled by velocity, 100 %.
  - Mod 2: Envelope (ADSR), per voice, routed to the **Filter's cutoff**, +30 % (it opens the filter fully at the envelope's peak).
  - Mod 3: LFO at about 5 Hz, per voice, routed to **pitch** through the **mod wheel**, ±0.5 semitone (vibrato).
  - Mod 4: empty. Pitch bend moves the pitch ±2 semitones (the part's Bend Range).
  - Voice Gain −12 dB, Soft Clip on.
- **EG13 FX:**
  - Part A plays the Input through **Audio Input** → **Analog Filter** (fully open) → Shaper *Off*, with Trigger set to *Always on*, so it processes audio with no MIDI.
  - Part B starts with Filter *Off* and Shaper *Off*.
  - Dry is 0 and the master level is 100 %.
  - Part A's Mod 1 is an LFO at about 0.25 Hz, per part, routed to the Analog Filter's cutoff, +20 %: with the filter fully open it sweeps down to about 5 kHz and back every four seconds.
  - Voice Gain 0 dB (EG13 FX usually plays a single voice), Soft Clip on.

## Saved projects

A project saves every setting, each engine's remembered settings, the routes and the envelopes' custom shapes. Projects saved by pre-release versions of EG13 open as the init patch.
