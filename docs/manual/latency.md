# Latency

Each engine runs at its module's own sample rate and is converted to and from the host's rate. Conversion takes time, and EG13 reports that time to the host so the host can compensate.

EG13 reports **one fixed latency**, and every engine is delayed to match the slowest one. That way changing engines never moves your sound in time. The cost is that a patch pays for the slowest engine even when that engine isn't in use.

| Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| EG13 (note onsets aligned) | 152 samples | 221 samples (4.6 ms) | 441 samples |
| EG13 FX (input aligned with Dry) | 428 samples | 491 samples (10.2 ms) | 981 samples |

The Modal Synth's 32 kHz engine sets these figures.

These figures cover the Source alone. Each voice's Filter and Shaper add their own latency on top: the Filters add none, but every Shaper is delayed to match the Ring Mod / Vocoder's conversion to 96 kHz and its 60-sample blocks, so the latency EG13 reports is higher than this table shows.

- **EG13** aligns note onsets. Sound that the Input excites through the Modal Synth or Resonator therefore arrives up to about 6 ms after the Dry signal.
- **EG13 FX** aligns the processed Input exactly with Dry.
