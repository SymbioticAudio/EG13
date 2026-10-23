# Latency

Each engine runs at its module's own sample rate and is converted to and from the host's rate. Conversion takes time, and EG13 reports that time to the host so the host can compensate.

EG13 reports **one fixed latency**, and every engine is delayed to match the slowest one. That way changing engines never moves your sound in time. The cost is that a patch pays for the slowest engine even when that engine isn't in use.

| Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| EG13 (note onsets aligned) | 446 samples (10.11 ms) | 521 samples (10.85 ms) | 501 samples (5.22 ms) |
| EG13 FX (input aligned with Dry) | 722 samples (16.37 ms) | 791 samples (16.48 ms) | 1041 samples (10.84 ms) |

Two engines set these figures: the Modal Synth's 32 kHz engine and the Ring Mod / Vocoder's 96 kHz engine, which runs in 60-sample blocks. Every Shaper, Off included, is delayed to match the Ring Mod / Vocoder, so changing the Shaper never moves the sound in time. This latency is the accepted cost for version 1 (owner decision, 2026-10-20). Filters add no latency.

- **EG13** aligns note onsets. Sound that the Input excites through the Modal Synth or Resonator therefore arrives up to about 6 ms after the Dry signal.
- **EG13 FX** aligns the processed Input exactly with Dry.

Modulation adds no latency: a key reaches the modulation engines within a quarter of a millisecond, and their outputs reach the sound every 16 samples.
