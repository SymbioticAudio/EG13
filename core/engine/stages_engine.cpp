#include "engine/stages_engine.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "stages/resources.h"
#include "stages/segment_generator.h"
#include "stmlib/utils/gate_flags.h"

namespace eg13 {

static_assert(int(kGateHigh) == int(stmlib::GATE_FLAG_HIGH) && int(kGateRising) == int(stmlib::GATE_FLAG_RISING) &&
                  int(kGateFalling) == int(stmlib::GATE_FLAG_FALLING),
              "EG13's gate flags are stmlib's");

namespace {

// Append-only (kModSelectorSteps): never reorder, only add at the end.
const char* const kModeNames[StagesEngine::kNumModes] = {"ADSR", "AD", "Looping", "Custom"};

void formatMode(float v, char* buf, size_t size) { formatOption(kModeNames, StagesEngine::kNumModes, v, buf, size); }

void formatTime(float v, char* buf, size_t size) {
  const float seconds = StagesEngine::segmentSeconds(v);
  if (seconds < 1.0f) {
    std::snprintf(buf, size, "%d ms", int(std::lround(seconds * 1000.0f)));
  } else {
    std::snprintf(buf, size, "%.2f s", double(seconds));
  }
}

// Defaults are the mod slots' shared host defaults (param_table.cpp, kModHostDefaults): an ADSR
// with a 1 ms attack, 0.5 s decay, full sustain and 0.5 s release, linear shapes (0.5).
const EngineParamInfo kParams[13] = {
    {"Mode", 0.0f, formatMode},
    {"Attack", 0.0f, formatTime},
    {"Attack Shape", 0.5f, formatPercent01},
    {"Decay", 0.5f, formatTime},
    {"Decay Shape", 0.5f, formatPercent01},
    {"Sustain", 1.0f, formatPercent01},
    {"Sustain Time", 0.5f, formatTime},
    {"Release", 0.5f, formatTime},
    {"Release Shape", 0.5f, formatPercent01},
    {"Seg 5 A", 0.0f, formatPercent01},
    {"Seg 5 B", 0.0f, formatPercent01},
    {"Seg 6 A", 0.0f, formatPercent01},
    {"Seg 6 B", 0.0f, formatPercent01},
};

const ModOutputInfo kOutputs[1] = {{"Envelope", false}};

const ModEngineInfo kInfo = {{"stages", "Envelope", 13, kParams, false}, 1, kOutputs, false};

stages::segment::Type segmentType(uint8_t type) {
  return type == EnvelopeShape::Step   ? stages::segment::TYPE_STEP
         : type == EnvelopeShape::Hold ? stages::segment::TYPE_HOLD
                                       : stages::segment::TYPE_RAMP;
}

EnvelopeShape preset(int count, std::initializer_list<uint8_t> types, std::initializer_list<uint8_t> loops) {
  EnvelopeShape s;
  s.count = uint8_t(count);
  int i = 0;
  for (uint8_t t : types) s.type[i++] = t;
  i = 0;
  for (uint8_t l : loops) s.loop[i++] = l;
  return s;
}

}  // namespace

struct StagesEngine::Impl {
  stages::SegmentGenerator generator;
  stages::SegmentGenerator::Output out[kNativeBlock];
};

StagesEngine::StagesEngine() : NativeModEngine(kNativeRate, kNativeBlock), impl_(std::make_unique<Impl>()) {
  impl_->generator.Init();
}

StagesEngine::~StagesEngine() = default;

const ModEngineInfo& StagesEngine::staticInfo() { return kInfo; }

const ModEngineInfo& StagesEngine::info() const { return kInfo; }

int StagesEngine::modeIndex(float normalized) { return modOption(normalized, kNumModes); }

float StagesEngine::segmentSeconds(float primary) {
  // SegmentGenerator::RateToFrequency (segment_generator.cc:132-136).
  const int i = int(clamp01(primary) * 2048.0f);
  return 1.0f / (stages::lut_env_frequency[i] * float(kNativeRate));
}

EnvelopeShape StagesEngine::modeShape(int mode, const EnvelopeShape& custom) {
  using S = EnvelopeShape;
  switch (mode) {
    case Adsr: return preset(4, {S::Ramp, S::Ramp, S::Hold, S::Ramp}, {0, 0, 1, 0});
    case Ad: return preset(2, {S::Ramp, S::Ramp}, {0, 0});
    case Looping: return preset(2, {S::Ramp, S::Ramp}, {1, 1});
    default: return sanitized(custom);
  }
}

void StagesEngine::resetEngine() {
  impl_->generator.Init();
  configure_ = true;
}

void StagesEngine::renderBlock(const ParamValues& p, const uint8_t* gate, const double*, double, bool,
                               float (*block)[kMaxNativeBlock]) {
  Impl& s = *impl_;
  const EnvelopeShape shape = modeShape(modeIndex(p.p[0]), custom_);
  if (configure_ || shape != configured_) {
    // As the module does when its segment types, loops or groups change (VCV Stages.cpp
    // stepBlock). Configure() leaves the generator idle (segment_generator.cc, end of Configure):
    // a held envelope falls to zero within about 1 ms and stays there until the next note, except
    // Looping, which starts cycling again.
    stages::segment::Configuration config[EnvelopeShape::kMaxSegments];
    for (int i = 0; i < shape.count; ++i) config[i] = {segmentType(shape.type[i]), shape.loop[i] != 0};
    s.generator.Configure(true, config, shape.count);
    configured_ = shape;
    configure_ = false;
  }
  for (int i = 0; i < shape.count; ++i) {
    s.generator.set_segment_parameters(i, clamp01(p.p[1 + 2 * i]), clamp01(p.p[2 + 2 * i]));
  }
  s.generator.Process(gate, s.out, kNativeBlock);
  for (int i = 0; i < kNativeBlock; ++i) block[0][i] = s.out[i].value;
}

}  // namespace eg13
