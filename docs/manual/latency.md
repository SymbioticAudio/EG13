# Latency

Each engine runs at its module's own sample rate and is converted to and from the host's rate. Conversion takes time, and EG13 reports that time to the host so the host can compensate.

EG13 reports **one fixed latency**, and every engine is delayed to match the slowest one. That way changing engines never moves your sound in time. The cost is that a patch pays for the slowest engine even when that engine isn't in use.

## Latency Mode: Live and Studio

The master setting **Latency Mode** chooses how the rate converters work. Every converter in EG13 follows it: Sources, Shaper and Part FX.

- **Studio** uses the full-fidelity converters. Choose it for mixing, rendering and anything played back from the host's timeline, where the host compensates the latency.
- **Live** uses the lowest-latency converters that keep Studio's sound quality: images and aliases at least 96 dB down, and a frequency response within 0.1 dB of Studio's. Choose it for playing EG13 live or tracking through it.

The instrument starts in **Live**; EG13 FX starts in **Studio**. The setting is saved with your project. Hosts show it with the other parameters but can't automate it.

In this version Live and Studio sound the same, and Live saves up to about 1 ms (the table below). Converters with lower latency were measured, and each one lost some top end (up to 0.35 dB) or let more aliasing through than the 96 dB floor allows, so none qualified.

<!--
Live candidate report (tests/test_converter_quality.cpp, `eg13_tests "[.converter]"`, 2026-10-23).
r8brain, transition band 30 %, worst case over hosts 44.1 / 48 / 96 kHz and natives 24 / 32 / 47.87 / 48 / 96 kHz.
Floors (M5 plan ruling 1): stop band >= 96 dB; passband within 0.1 dB of 1 kHz up to Studio's -0.1 dB edge.
  phase    atten   stop band  passband  worst 48 kHz input path (prime 2 / 1)
  linear    96.00     92.40    0.3488     315 / 267
  linear   108.00    104.51    0.2856     311 / 263
  linear   120.00    116.63    0.2332     474 / 462
  linear   136.00    130.25    0.1825     466 / 454
  linear   150.00    144.06    0.1493     460 / 448
  linear   180.15    149.65    0.0955     539 / 491   <- selected with prime 1
  minimum   96.00     92.39    0.3510     285 / 237   fails ruling 3 (impulse +3/+6 samples, Dry + wet notch -38 dB)
  minimum  108.00    104.51    0.2870     277 / 229   fails ruling 3
  minimum  120.00    116.77    0.2339     422 / 410   fails ruling 3
  minimum  136.00    130.34    0.1826     406 / 394   fails ruling 3
  minimum  150.00    143.98    0.1495     396 / 384   fails ruling 3
  minimum  180.15    149.36    0.0959     482 / 434   fails ruling 3 (notch -57 dB)
The prime of one native block has no under- or overruns at any host rate with 1-, 7-, 129- and
maxBlock-sample host blocks. 149.7 dB is the measurement floor (float buffers).
-->

| Mode | Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|---|
| Live | EG13 (note onsets aligned) | 418 samples (9.48 ms) | 491 samples (10.23 ms) | 501 samples (5.22 ms) |
| Live | EG13 FX (input aligned with Dry) | 672 samples (15.24 ms) | 737 samples (15.35 ms) | 993 samples (10.34 ms) |
| Studio | EG13 (note onsets aligned) | 446 samples (10.11 ms) | 521 samples (10.85 ms) | 501 samples (5.22 ms) |
| Studio | EG13 FX (input aligned with Dry) | 722 samples (16.37 ms) | 791 samples (16.48 ms) | 1041 samples (10.84 ms) |

Two engines set these figures: the Modal Synth's 32 kHz engine and the Ring Mod / Vocoder's 96 kHz engine, which runs in 60-sample blocks. Every Shaper, Off included, is delayed to match the Ring Mod / Vocoder, so changing the Shaper never moves the sound in time. This latency is the accepted cost for version 1 (owner decision, 2026-10-20). Filters add no latency. Each mode has its own fixed latency: within a mode, changing engines never moves the sound.

- **EG13** aligns note onsets. Sound that the Input excites through the Modal Synth or Resonator therefore arrives up to about 6 ms after the Dry signal.
- **EG13 FX** aligns the processed Input exactly with Dry.

### Changing the mode

A new Latency Mode takes effect when the host restarts the plugin, because every converter is rebuilt and the reported latency changes. Until then EG13 keeps playing in the mode it started with, and the latency it reports always matches what you hear. This also applies when you load a project saved in the other mode.

- **CLAP and VST3 hosts** restart the plugin straight away when you change the setting.
- **AU hosts** apply the change only when they next start the plugin, for example when you reopen the project or change the sample rate. (The AU wrapper EG13 uses can't ask the host for a restart.)

Modulation adds no latency: a key reaches the modulation engines within about half a millisecond, and their outputs reach the sound every 16 samples.
