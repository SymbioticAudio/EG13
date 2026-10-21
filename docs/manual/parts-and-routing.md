# Parts and routing

## Parts and voices

EG13 has two parts, A and B. Each plays up to eight voices. Each voice runs Source → Filter → Shaper. Every engine of every slot is ready in every voice, so changing an engine never loads anything.

The master **Mode** decides which notes reach which part: *Single* (only A plays), *Layer* (both play every note), *Split* (A below the split note, B from it up) or *Multi* (each part on its own MIDI channel). In Single mode Part B is silent but still runs, so it can feed Part A (see below).

## Engine switching

Each engine remembers its own settings in each part. Switching Source from Macro Oscillator to Digital Oscillator and back brings back the Macro Oscillator exactly as you left it. The host's parameters change to the new engine's values; this change is not recorded as automation.

Switching the **Source** engine is a hard cut: the old engine stops at once, and a held note restarts on the new engine after the plugin's latency.

Changing the **Filter** or **Shaper** type is smooth: while a note sounds, the old engine keeps playing with its last settings until the new one's sound arrives, then the two crossfade over 10 ms. The timing never changes, because every Filter and every Shaper (Off included) is delayed to the same latency. A Ring Mod / Vocoder that is fading out keeps its own Carrier and Modulator signals until it is silent.

If you change the type again while a crossfade is under way (for example while the type is automated), the new choice waits until that crossfade has finished, then crossfades in the same way; only the last choice made during the crossfade is played. Meanwhile the host already shows the new choice's settings, and the engine fading in keeps the settings it had. Changing it back to the engine that is fading in cancels the waiting choice. A voice that starts a new note from silence goes straight to the latest choice, with no crossfade.

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

Each part's **Output** goes to *Main*, to *Bus 2* (the instrument's second output) or to *Other part only*. With *Other part only* the part is not heard; it only feeds the other part.

**Dry** mixes the Input into the main output, delayed by the plugin's latency so it lines up with the processed sound.

## Default patches

- **EG13:** Part A with the Macro Oscillator.
- **EG13 FX:**
  - Part A plays the Input through **Audio Input**, with Trigger set to *Always on*, so it processes audio with no MIDI.
  - Dry is 0 and the master level is 100 %.
