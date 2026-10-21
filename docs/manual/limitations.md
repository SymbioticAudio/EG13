# Known limitations

- **Levels and headroom:** EG13 does not yet manage gain between voices and parts. Sixteen loud voices can peak well above full scale (5 to 13 times, measured on chords). Keep the master level down until gain staging lands with the modulation milestone (M4).
- **Resonator velocity:** a new note's velocity rescales the whole Resonator output, including voices still ringing, which can click (fixed in M4).
- **Modal Synth input:** the firmware's noise gate on the external input is not ported.
- **Part B in Single mode** still runs and uses CPU, so it can feed Part A.
- **Ring Mod / Vocoder noise carrier:** with Osc Shape *Saw / Noise* in the vocoder range, the noise comes from a random generator shared by the voices on one audio thread, so two renders of the same notes are not sample-identical.
