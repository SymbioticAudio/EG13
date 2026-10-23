#include "engine/beads_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "beads/dsp/processor.h"
#include "engine/native_rate_wrapper.h"
#include "engine/param_format.h"
#include "engine/part_fx_engine.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"

namespace eg13 {

namespace {

const char* const kQualityNames[BeadsEngine::kNumQualities] = {"Digital", "Vintage digital", "Clean tape",
                                                               "Dirty tape"};
const char* const kSeedNames[BeadsEngine::kNumSeeds] = {"Free", "Notes"};

void formatQuality(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kQualityNames[BeadsEngine::qualityIndex(v)]);
}

void formatSeed(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", kSeedNames[BeadsEngine::seedIndex(v)]);
}

void formatOffOn(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%s", partFxSwitch(v) ? "On" : "Off");
}

void formatPitch(float v, char* buf, size_t size) { formatSemitones(BeadsEngine::pitchSemitones(v), buf, size); }

void formatAmount(float v, char* buf, size_t size) {
  std::snprintf(buf, size, "%+.0f%%", double(BeadsEngine::attenuverter(v) * 100.0f));
}

// M5 plan ruling 8: the P layout; every default equals the host default of its position.
const EngineParamInfo kParams[kPartFxParamCount] = {
    {"Dry/Wet", kPartFxHostDefaults[0], formatPercent01},
    {"Time", kPartFxHostDefaults[1], formatPercent01},
    {"Size", kPartFxHostDefaults[2], formatPercent01},
    {"Pitch", kPartFxHostDefaults[3], formatPitch},
    {"Density", kPartFxHostDefaults[4], formatPercent01},  // 75 %: Beads makes no grains near 50 % (ruling 9)
    {"Shape", kPartFxHostDefaults[5], formatPercent01},
    {"Feedback", kPartFxHostDefaults[6], formatPercent01},
    {"Reverb", kPartFxHostDefaults[7], formatPercent01},
    {"Freeze", kPartFxHostDefaults[8], formatOffOn},     // switch: on at 50 % or more
    {"Quality", kPartFxHostDefaults[9], formatQuality},  // stepped, append-only: 16 fixed steps
    {"Seed", kPartFxHostDefaults[10], formatSeed},       // stepped, append-only: 16 fixed steps
    {"(unused)", kPartFxHostDefaults[11], nullptr},
    {"Time Random", kPartFxHostDefaults[12], formatAmount},
    {"Size Random", kPartFxHostDefaults[13], formatAmount},
    {"Shape Random", kPartFxHostDefaults[14], formatAmount},
    {"Pitch Random", kPartFxHostDefaults[15], formatAmount},
};

const EngineInfo kInfo = {"beads", "Granular Delay", kPartFxParamCount, kParams, true, false, 0, nullptr};

// The firmware's memories (beads.cc:54-56).
constexpr size_t kDelayBuffer1Size = 512 * 1024;
constexpr size_t kDelayBuffer2Size = 256 * 1024;
constexpr size_t kReverbBufferSize = 32768;

constexpr float kPotLp = 0.05f;  // cv_reader.cc:50-54, once per 4 kHz block
constexpr int kMaxNativeBlock = 48000 / BeadsEngine::kBlockRate;
constexpr double kChangeFadeSeconds = 0.01;  // ruling 15
constexpr int kMaxQueuedSeeds = 16;          // notes on their way to the processor (about 10 ms)

// The inner paths, slowest rate first.
constexpr int kNumPaths = 3;
constexpr double kPathRates[kNumPaths] = {24000.0, 32000.0, 48000.0};

int pathFor(BeadsEngine::Quality q) {
  switch (q) {
    case BeadsEngine::DirtyTape: return 0;
    case BeadsEngine::VintageDigital: return 1;
    case BeadsEngine::Digital:
    case BeadsEngine::CleanTape:
    default: return 2;
  }
}

// The smoothed pots, in the order the P's hold them.
enum Pot {
  kPotDryWet,
  kPotTime,
  kPotSize,
  kPotPitch,
  kPotDensity,
  kPotShape,
  kPotFeedback,
  kPotReverb,
  kPotTimeAmount,
  kPotSizeAmount,
  kPotShapeAmount,
  kPotPitchAmount,
  kNumPots
};

// The P index of each pot.
constexpr int kPotParam[kNumPots] = {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 14, 15};

// A mix pot as the firmware passes it (beads.cc:221-235, with no assignable CV).
float mixParameter(float pot) { return std::min(std::max(pot * 1.005f, 0.0f), 1.0f); }

}  // namespace

// State shared by the three inner paths: the one processor and its memories (as the hardware
// shares them across Qualities), the pots and the seed gate (both run at 4 kHz whatever the rate).
struct BeadsEngine::Impl {
  std::vector<uint8_t> delay1 = std::vector<uint8_t>(kDelayBuffer1Size);
  std::vector<uint8_t> delay2 = std::vector<uint8_t>(kDelayBuffer2Size);
  std::vector<uint16_t> reverb = std::vector<uint16_t>(kReverbBufferSize);
  beads::Processor processor;
  float pots[kNumPots] = {};
  bool snap = true;  // the first block after reset() takes the pots as they are
  bool seedPending = false;
  int seedRemaining = 0;  // samples the SEED gate stays high
  stmlib::GateFlags previousSeed = stmlib::GATE_FLAG_LOW;

  std::unique_ptr<NativeRateWrapper> paths[kNumPaths];
  int running = -1;  // the Quality the processor runs (-1: none yet; the first block applies P10 at once)
  int active = 2;    // the path that runs it
  int latency = 0;   // host samples, every path padded to it
  int fadeSamples = 1;
  int fadePos = 1;   // the change fade's gain is fadePos / fadeSamples
  int hold = 0;      // host samples of silence after a change, while the new path fills
  int maxBlock = 0;
  // Note-ons wait, in host samples, for the input path's head start over the output path, so a
  // seed reaches the processor with the audio that was playing when the note came.
  int seedDelay[kNumPaths] = {};
  int seedQueue[kMaxQueuedSeeds] = {};
  int seedQueued = 0;
};

// One inner path's engine: drives the shared processor at `rate` in blocks of rate / 4000, as
// the firmware's codec callback does (beads.cc:107-271). reset() leaves the shared processor
// alone (preflight ruling G17): a path restarts its conversion without touching the sound.
class BeadsEngine::Core final : public VoiceEngine {
 public:
  Core(Impl* s, double rate)
      : s_(s), block_(int(rate) / kBlockRate), seedSamples_(int(std::ceil(0.001 * rate))) {}

  void prepare(double, int) override {}
  void reset() override {}
  void noteOn(float, float) override {}
  void noteOff() override {}
  void setNote(float) override {}
  const EngineInfo& info() const override { return kInfo; }

  void process(const float* const* in, float* const* out, const ParamValues& p, int n) override {
    // Whole blocks: the wrapper always gives them (setWholeBlocks). A shorter tail, outside that
    // contract, runs as a whole block padded with silence (Clean tape's 2:1 converter needs even
    // blocks) and the extra output is dropped.
    for (int done = 0; done < n;) {
      const int count = std::min(block_, n - done);
      renderBlock(in, out, p, done, count);
      done += count;
    }
  }

 private:
  void renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset, int count);

  Impl* s_;
  int block_;
  int seedSamples_;
};

void BeadsEngine::Core::renderBlock(const float* const* in, float* const* out, const ParamValues& p, int offset,
                                    int count) {
  Impl& s = *s_;
  const Quality quality = Quality(s.running < 0 ? 0 : s.running);
  beads::FloatFrame input[kMaxNativeBlock];
  beads::FloatFrame output[kMaxNativeBlock];
  bool freeze[kMaxNativeBlock];
  stmlib::GateFlags seed[kMaxNativeBlock];
  const int block = std::clamp(block_, 0, kMaxNativeBlock);  // always rate / 4000 (6 to 12)

  for (int i = 0; i < block; ++i) {
    const float l = in != nullptr && i < count ? in[0][offset + i] : 0.0f;
    const float r = in != nullptr && i < count ? in[1][offset + i] : 0.0f;
    input[i].l = inputStage(l, quality);
    input[i].r = inputStage(r, quality);
  }

  // The pots: law, then the one-pole low-pass, once per block (CvReader::Read, cv_reader.cc:106-108).
  for (int k = 0; k < kNumPots; ++k) {
    const float v = clamp01(p.p[kPotParam[k]]);
    float target = v;
    if (k == kPotPitch) target = pitchSemitones(v);
    if (k >= kPotTimeAmount) target = attenuverter(v);
    if (s.snap) {
      s.pots[k] = target;
    } else {
      ONE_POLE(s.pots[k], target, kPotLp);
    }
  }
  s.snap = false;

  beads::Parameters parameters;
  beads::RandomizedParameter* const randomized[4] = {&parameters.time, &parameters.size, &parameters.shape,
                                                     &parameters.pitch};
  const int bases[4] = {kPotTime, kPotSize, kPotShape, kPotPitch};
  for (int k = 0; k < 4; ++k) {
    randomized[k]->base = s.pots[bases[k]];
    randomized[k]->attenuverter = s.pots[kPotTimeAmount + k];
    randomized[k]->has_cv = false;
    randomized[k]->cv = 0.0f;
  }
  parameters.monophonic_mode = false;

  beads::ModulatedParameter density;
  density.base = s.pots[kPotDensity];
  density.cv = 0.0f;

  beads::Processor::MixParameters mix;
  mix.feedback = mixParameter(s.pots[kPotFeedback]);
  mix.dry_wet = mixParameter(s.pots[kPotDryWet]);
  mix.reverb_amount = mixParameter(s.pots[kPotReverb]);
  mix.output_gate = false;

  const bool frozen = partFxSwitch(p.p[8]);
  std::fill(freeze, freeze + block, frozen);

  // SEED (ruling 14). Free: the input unpatched (beads.cc:136-153) and the SEED button latched,
  // as the module starts up (ui.cc:78), so Density alone makes grains (trigger_generator.cc:
  // 255-286; with the button released and nothing patched the pulse train never runs).
  // Notes: a note-on raises the SEED gate for 1 ms from this block, with the button released, so
  // each note seeds a grain at any Density, the 48-52 % dead zone included (manual taps,
  // trigger_generator.cc:287-310); latched, the dead zone would ignore the notes (:132-137).
  const stmlib::GateFlags* seedInput = nullptr;
  beads::SeedButtonState button = beads::SEED_BUTTON_STATE_LOCKED;
  if (seedIndex(p.p[10]) == SeedNotes) {
    button = beads::SEED_BUTTON_STATE_OFF;
    if (s.seedPending) s.seedRemaining = seedSamples_;
    s.seedPending = false;
    for (int i = 0; i < block; ++i) {
      const bool high = s.seedRemaining > 0;
      if (high) --s.seedRemaining;
      s.previousSeed = seed[i] = stmlib::ExtractGateFlags(s.previousSeed, high);
    }
    seedInput = seed;
  } else {
    s.seedPending = false;
    s.seedRemaining = 0;
    s.previousSeed = stmlib::GATE_FLAG_LOW;
  }

  s.processor.Process(button, seedInput, freeze, density, parameters, mix, input, output,
                      size_t(block));

  // The firmware's conversion to the codec's integers clamps (beads.cc:265-269).
  for (int i = 0; i < count; ++i) {
    out[0][offset + i] = std::min(std::max(output[i].l, -1.0f), 0.999999f);
    out[1][offset + i] = std::min(std::max(output[i].r, -1.0f), 0.999999f);
  }
}

BeadsEngine::BeadsEngine() : impl_(std::make_unique<Impl>()) {
  for (int k = 0; k < kNumPaths; ++k) {
    impl_->paths[k] = std::make_unique<NativeRateWrapper>(std::make_unique<Core>(impl_.get(), kPathRates[k]),
                                                          kPathRates[k], int(kPathRates[k]) / kBlockRate);
    impl_->paths[k]->setWholeBlocks(true);  // preflight ruling G8
  }
  reset();
}

BeadsEngine::~BeadsEngine() = default;

const EngineInfo& BeadsEngine::staticInfo() { return kInfo; }

const EngineInfo& BeadsEngine::info() const { return kInfo; }

int BeadsEngine::mediaFor(Quality q) {
  switch (q) {
    case Digital: return beads::Processor::MEDIA_DIGITAL;
    case VintageDigital: return beads::Processor::MEDIA_VINTAGE_DIGITAL;
    case CleanTape: return beads::Processor::MEDIA_CLEAN_TAPE;
    case DirtyTape: return beads::Processor::MEDIA_DIRTY_TAPE;
  }
  return beads::Processor::MEDIA_DIGITAL;
}

double BeadsEngine::rateFor(Quality q) { return kPathRates[pathFor(q)]; }

int BeadsEngine::qualityIndex(float p) { return partFxOption(p, kNumQualities); }

int BeadsEngine::seedIndex(float p) { return partFxOption(p, kNumSeeds); }

float BeadsEngine::pitchSemitones(float p) {
  const float v = clamp01(p);
  float detune = 2.0f * v - 1.0f;
  const float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  MAKE_INTEGRAL_FRACTIONAL(detune);
  const float intervals[8] = {0.0f, 1.0f, 3.0f, 4.0f, 7.0f, 12.0f, 24.0f, 24.0f};
  detune_fractional = std::max((detune_fractional - 0.33333333f) * 1.5f, 0.0f);
  const float a = intervals[detune_integral];
  const float b = intervals[detune_integral + 1];
  return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional)) * sign;
}

float BeadsEngine::attenuverter(float p) {
  const float v = clamp01(p) - 0.5f;
  return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

float BeadsEngine::inputStage(float x, Quality q) {
  if (!(x == x)) x = 0.0f;
  if (q == DirtyTape) {
    // The manual-gain path's crossfade at amount 1 is the soft-clipped signal, biased by 0.05 as
    // the firmware does. EG13 guard (M5 T6 stress finding): the bias's constant offset (-0.0025)
    // is removed, as silence in would otherwise play on as a quiet pulsing tail forever.
    const float bias = 0.05f;
    return (1.0f - bias) * (stmlib::SoftClip(x + bias) - stmlib::SoftClip(bias));
  }
  return std::min(std::max(x, -1.0f), 1.0f);
}

BeadsEngine::Quality BeadsEngine::quality() const {
  return Quality(impl_->running < 0 ? 0 : impl_->running);
}

int BeadsEngine::nativeLatency() const { return impl_->latency; }

void BeadsEngine::setConverterQuality(ConverterQuality q) {
  for (auto& path : impl_->paths) path->setQuality(q);
}

void BeadsEngine::prepare(double hostRate, int maxBlock) {
  Impl& s = *impl_;
  s.maxBlock = maxBlock;
  int slowest = 0;
  for (auto& path : s.paths) {
    path->prepare(hostRate, maxBlock);
    slowest = std::max(slowest, path->inputPathLatencySamples());
  }
  for (int k = 0; k < kNumPaths; ++k) {
    s.paths[k]->setExtraDelay(slowest - s.paths[k]->inputPathLatencySamples());
    s.seedDelay[k] = s.paths[k]->inputPathLatencySamples() - s.paths[k]->latencySamples();
  }
  s.latency = slowest;
  s.fadeSamples = std::max(1, int(std::lround(kChangeFadeSeconds * hostRate)));
  reset();
}

// Audio-thread safe: memset + Init, no allocation (preflight rulings G6, G7). The memories are
// cleared too, so a reset engine equals a fresh one whichever Quality it ran.
void BeadsEngine::reset() {
  Impl& s = *impl_;
  std::fill(s.delay1.begin(), s.delay1.end(), uint8_t(0));
  std::fill(s.delay2.begin(), s.delay2.end(), uint8_t(0));
  std::fill(s.reverb.begin(), s.reverb.end(), uint16_t(0));
  std::memset(static_cast<void*>(&s.processor), 0, sizeof(s.processor));
  uint8_t* buffers[3] = {s.delay1.data(), s.delay1.data() + 256 * 1024, s.delay2.data()};  // beads.cc:307-311
  s.processor.Init(buffers, s.reverb.data());
  std::fill(s.pots, s.pots + kNumPots, 0.0f);
  s.snap = true;
  s.seedPending = false;
  s.seedRemaining = 0;
  s.previousSeed = stmlib::GATE_FLAG_LOW;
  s.seedQueued = 0;
  for (auto& path : s.paths) path->reset();
  s.running = -1;
  s.active = 2;
  s.fadePos = s.fadeSamples;
  s.hold = 0;
}

// A seed for the processor once the active path's input has caught up (see Impl::seedDelay).
// More notes than the queue holds within that time (about 10 ms) merge into the gates already
// on their way.
void BeadsEngine::noteOn(float, float) {
  Impl& s = *impl_;
  if (s.seedQueued < kMaxQueuedSeeds) s.seedQueue[s.seedQueued++] = s.seedDelay[s.active];
}

// Configure the processor for Quality q (beads.cc:274-292 without the codec restart) and restart
// its path's conversion, so its first output lands at the latency.
void BeadsEngine::applyQuality(int q) {
  Impl& s = *impl_;
  s.running = q;
  s.processor.Configure(beads::Processor::Media(mediaFor(Quality(q))), false);
  s.active = pathFor(Quality(q));
  s.paths[s.active]->reset();
}

void BeadsEngine::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  Impl& s = *impl_;
  if (processInPieces(in, out, n, s.maxBlock,
                      [&](const float* const* i, float* const* o, int m) { process(i, o, p, m); })) {
    return;
  }
  // Quality (ruling 15): the first block after reset applies P10 at once. Later a change fades the
  // output out over 10 ms, configures the processor, restarts the new path, holds silence while
  // that path fills (the latency), then fades back in over 10 ms.
  const int target = qualityIndex(p.p[9]);
  if (s.running < 0) {
    applyQuality(target);
    s.fadePos = s.fadeSamples;
    s.hold = 0;
  }
  for (int done = 0; done < n;) {
    if (target != s.running && s.fadePos == 0) {
      applyQuality(target);
      s.hold = s.latency;
    }
    // Seeds due now reach the processor's next block; the call is cut at the next one due.
    int count = n - done;
    for (int k = 0; k < s.seedQueued;) {
      if (s.seedQueue[k] <= 0) {
        s.seedPending = true;
        s.seedQueue[k] = s.seedQueue[--s.seedQueued];
      } else {
        count = std::min(count, s.seedQueue[k]);
        ++k;
      }
    }
    const bool fadingOut = target != s.running;
    if (fadingOut) count = std::min(count, s.fadePos);
    const float* pieceIn[2] = {in != nullptr ? in[0] + done : nullptr, in != nullptr ? in[1] + done : nullptr};
    float* pieceOut[2] = {out[0] + done, out[1] + done};
    s.paths[s.active]->process(in != nullptr ? pieceIn : nullptr, pieceOut, p, count);
    if (fadingOut || s.hold > 0 || s.fadePos < s.fadeSamples) {
      const float scale = 1.0f / float(s.fadeSamples);
      for (int i = 0; i < count; ++i) {
        if (fadingOut) {
          --s.fadePos;
        } else if (s.hold > 0) {
          --s.hold;
        } else if (s.fadePos < s.fadeSamples) {
          ++s.fadePos;
        }
        const float gain = float(s.fadePos) * scale;
        pieceOut[0][i] *= gain;
        pieceOut[1][i] *= gain;
      }
    }
    for (int k = 0; k < s.seedQueued; ++k) s.seedQueue[k] -= count;
    done += count;
  }
}

}  // namespace eg13
