# Latency

Each engine runs at its module's own sample rate and is converted to and from the host's rate. Conversion takes time, and EG13 reports that time to the host so the host can compensate.

EG13 reports **one fixed latency**, and every engine is delayed to match the slowest one. That way changing engines never moves your sound in time. The cost is that a patch pays for the slowest engine even when that engine isn't in use.

## Latency Mode: Live and Studio

The master setting **Latency Mode** chooses how the rate converters work. Every converter in EG13 follows it: Sources, Shaper and Part FX.

- **Studio** uses the full-fidelity converters. Choose it for mixing, rendering and anything played back from the host's timeline, where the host compensates the latency.
- **Live** uses lower-latency converters, which roughly halve EG13's latency. Images and aliases stay at least 96 dB down. Choose it for playing EG13 live or tracking through it.

The instrument starts in **Live**; EG13 FX starts in **Studio**. The setting is saved with your project. Hosts show it with the other parameters but can't automate it.

Live can be up to about 0.5 dB softer at the very top of each engine's range (measured against Studio, a sine through the converters and back, worst over 44.1, 48 and 96 kHz hosts). That is about 0.2 dB at 7 kHz for engines that run at 24 kHz, about 0.5 dB around 10 kHz at 32 kHz and about 0.5 dB at 14 kHz at 48 kHz. Below those frequencies Live and Studio sound the same.

<!--
Live candidate report (tests/test_converter_quality.cpp, `eg13_tests "[.converter]"`, 2026-10-23).
r8brain, transition band 30 %, worst case over hosts 44.1 / 48 / 96 kHz and natives 24 / 32 / 47.87 / 48 / 96 kHz.
Floors (owner decisions of 2026-10-23): stop band >= 96 dB; passband within 0.3 dB of 1 kHz up to
Studio's -0.1 dB edge; ruling 3's Dry + wet notch no deeper than -1.1 dB (Studio -0.99, Live -1.08).
  phase    atten   stop band  passband  notch     worst 48 kHz input path (prime 2 / 1)
  linear    96.00     92.40    0.3488   -1.11      315 / 267   fails stop band, notch
  linear   108.00    104.51    0.2856   -1.08      311 / 263   <- selected with prime 1
  linear   120.00    116.63    0.2332   -1.05      474 / 462
  linear   136.00    130.25    0.1825   -1.03      466 / 454
  linear   150.00    144.06    0.1493   -1.01      460 / 448
  linear   180.15    149.65    0.0955   -0.99      539 / 491   (Studio, prime 2)
  minimum   96 .. 180.15: 30-60 samples faster; all fail ruling 3 (impulse 2-6 samples off,
                          Dry + wet notch -35 to -57 dB)
Live droop at Studio's -0.1 dB edge (worst over hosts): 24 kHz -0.27 dB, 32 kHz -0.25 dB, 48 kHz -0.29 dB.
The prime of one native block has no under- or overruns at any host rate with 1-, 7-, 129- and
maxBlock-sample host blocks. 149.7 dB is the measurement floor (float buffers).
-->

| Mode | Plugin | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|---|
| Live | EG13 (note onsets aligned) | 462 samples (10.48 ms) | 531 samples (11.06 ms) | 798 samples (8.31 ms) |
| Live | EG13 FX (input aligned with Dry) | 598 samples (13.56 ms) | 663 samples (13.81 ms) | 1062 samples (11.06 ms) |
| Studio | EG13 (note onsets aligned) | 918 samples (20.82 ms) | 1060 samples (22.08 ms) | 1578 samples (16.44 ms) |
| Studio | EG13 FX (input aligned with Dry) | 1194 samples (27.07 ms) | 1330 samples (27.71 ms) | 2118 samples (22.06 ms) |

Three engines set these figures: the Modal Synth's 32 kHz engine, the Ring Mod / Vocoder's 96 kHz engine, which runs in 60-sample blocks, and the Part FX Granular's 32 kHz engine. Every Shaper, Off included, is delayed to match the Ring Mod / Vocoder, so changing the Shaper never moves the sound in time.

### Part FX

Every part has a Part FX slot, and every Part FX, **Off included**, is delayed to match the slowest one (Granular; at 44.1 kHz in Live, Granular Delay by one sample). That is why the slot adds its delay even when you never use it: if Off were faster, switching a part's effect on or off would move its sound in time against the other part and against Dry. The delay includes the wait for whole blocks: the Part FX engines run in whole blocks of their own (Granular 32 samples at 32 kHz, Reverb 16 at 32 kHz, the Resonator 24 at 48 kHz, Granular Delay 12 at 48 kHz in its default Quality), so each one holds its input until a block is full, which is part of the figures below.

| Mode | 44.1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Live | 232 samples (5.26 ms) | 263 samples (5.48 ms) | 525 samples (5.47 ms) |
| Studio | 472 samples (10.70 ms) | 539 samples (11.23 ms) | 1077 samples (11.22 ms) |

Alone, the other engines add less at 48 kHz (Live / Studio): Granular Delay 243 / 483 samples, Reverb 239 / 491 and the Resonator 24 / 24, and each is padded up to Granular's figure. This delay is part of the totals in the table above (EG13: Source + Shaper + Part FX; for example Live at 48 kHz: 107 + 161 + 263 = 531 samples; Studio: 221 + 300 + 539 = 1060). It is the accepted cost for version 1 (owner decision, 2026-10-20; the Part FX share, about 5.5 ms in Live and 11 ms in Studio, accepted 2026-10-23, to be tested by playing). Filters add no latency. Each mode has its own fixed latency: within a mode, changing engines never moves the sound.

- **EG13** aligns note onsets. Sound that the Input excites through the Modal Synth or Resonator therefore arrives up to about 6 ms after the Dry signal.
- **EG13 FX** aligns the processed Input exactly with Dry.

### Changing the mode

A new Latency Mode takes effect when the plugin restarts, because every converter is rebuilt and the reported latency changes. Until then EG13 keeps playing in the mode it started with, and the latency it reports always matches what you hear. This also applies when you load a project saved in the other mode.

- **CLAP and VST3 hosts** restart the plugin straight away when you change the setting.
- **AU hosts** restart it within a moment of the change too, and the new latency is reported to the host.

Modulation adds no latency: a key reaches the modulation engines within about half a millisecond, and their outputs reach the sound every 16 samples.
