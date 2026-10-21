# Known limitations

- **Levels and headroom:** EG13 does not yet manage gain between voices and parts. Sixteen loud voices can peak well above full scale (5 to 13 times, measured on chords). Some Filter and Shaper settings are much louder than their input (measured with a half-scale saw):
  - Several Classic Filters types at full Resonance peak at about 4.5, about +13 dB above full scale.
  - The EQ at its extremes (+18 dB with a loud input) clips inside, and the output can briefly ring to about 4 times full scale.
  - The Ring Mod / Vocoder at its defaults is 4.6 dB louder than its input (peak 1.24 from 0.5), and up to 12.3 dB louder at Algorithm *Fold* (peak 2.0). Its output never exceeds Warps' own 16-bit range (3.2 times full scale).
  - At their defaults the filters and the EQ keep the level (within 0.05 dB).

  Keep the master level down. A later milestone (M4) adds gain staging and a soft clipper on the master output.
- **Resonator velocity:** a new note's velocity rescales the whole Resonator output, including voices still ringing, which can click (fixed in M4).
- **Modal Synth input:** the firmware's noise gate on the external input is not ported.
- **Part B in Single mode** still runs and uses CPU, so it can feed Part A.
- **Ring Mod / Vocoder noise carrier:** with Osc Shape *Saw / Noise* in the vocoder range, the noise comes from a random generator shared by the voices on one audio thread, so two renders of the same notes are not sample-identical.
- **CPU with stereo chains:** sixteen voices of the heaviest chains stay within half of one Apple M1 core when the Source is mono (measured: Macro Oscillator → Analog Filter → Vocoder, 48.9 %). With a stereo Source (Modal Synth, Resonator with Spread, a stereo input), every Filter and Shaper runs twice, and the heaviest stereo chains can use more (Modal Synth → Analog Filter → Vocoder, 87.4 %). Engines that run at their own sample rate save work by converting the Ring Mod / Vocoder's side inputs only while they carry a signal, and the right channel only while it differs from the left; this changes nothing you can hear. See the README for the measured figures.
- **Classic Filters Type** changes restart the filter and can click on a sounding note.
