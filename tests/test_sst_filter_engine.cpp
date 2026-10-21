#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "alloc_counter.h"
#include "engine/sst_filter_engine.h"
#include "sst/filters++.h"
#include "test_helpers.h"

using namespace eg13;
namespace sfpp = sst::filtersplusplus;

namespace {

ParamValues defaults() { return test::defaultParams(SstFilterEngine::staticInfo()); }

constexpr size_t kLine = size_t(sst::filters::utilities::MAX_FB_COMB) +
                         size_t(sst::filters::utilities::SincTable::FIRipol_N);

std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> upstreamTypes() {
  std::vector<std::pair<sfpp::FilterModel, sfpp::ModelConfig>> all;
  for (auto model : sfpp::Filter::availableModels()) {
    for (const auto& config : sfpp::Filter::availableModelConfigurations(model, true)) all.push_back({model, config});
  }
  return all;
}

int typeNamed(const std::string& name) {
  for (int i = 0; i < SstFilterEngine::typeCount(); ++i) {
    if (name == SstFilterEngine::typeName(i)) return i;
  }
  FAIL("no Classic Filters type named " << name);
  return -1;
}

// sst-filters driven directly, the way the adapter drives it.
struct Reference {
  sfpp::Filter f;
  std::vector<float> delay;
  Reference(int type, double rate) : delay(4 * kLine, 0.0f) {
    const auto all = upstreamTypes();
    f.setSampleRateAndBlockSize(rate, 16);
    for (int lane = 0; lane < 4; ++lane) f.provideDelayLine(lane, delay.data() + size_t(lane) * kLine);
    f.setFilterModel(all[size_t(type)].first);
    f.setModelConfiguration(all[size_t(type)].second);
    REQUIRE(f.prepareInstance());
    f.setStereo();
  }
  void block(const float* in, float* outL, float* outR, float cutoff, float reso, float extra, bool constant) {
    if (constant) {
      f.makeConstantCoefficients(0, cutoff, reso, extra);
    } else {
      f.makeCoefficients(0, cutoff, reso, extra);
    }
    f.copyCoefficientsFromVoiceToVoice(0, 1);
    f.prepareBlock();
    for (int i = 0; i < 16; ++i) f.processStereoSample(in[i], in[i], outL[i], outR[i]);
    f.concludeBlock();
  }
};

std::vector<float> saw(size_t n, double hz, double rate) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(hz * double(i) / rate, 1.0) - 1.0);
  return x;
}

void runBlock(SstFilterEngine& e, const float* l, const float* r, float* ol, float* orr, const ParamValues& p) {
  const float* in[2] = {l, r};
  float* out[2] = {ol, orr};
  e.process(in, out, p, 16);
}

}  // namespace

TEST_CASE("Classic Filters describes itself") {
  const auto& info = SstFilterEngine::staticInfo();
  REQUIRE(std::string(info.id) == "sst");
  REQUIRE(std::string(info.displayName) == "Classic Filters");
  REQUIRE(info.paramCount == 4);
  REQUIRE(info.usesInput);
  const char* const names[4] = {"Cutoff", "Resonance", "Type", "Character"};
  for (int i = 0; i < 4; ++i) REQUIRE(std::string(info.params[i].name) == names[i]);
  REQUIRE(info.params[0].defaultValue == 1.0f);
  REQUIRE(SstFilterEngine::typeIndex(info.params[2].defaultValue) == 0);
  char buf[64];
  info.params[2].format(info.params[2].defaultValue, buf, sizeof(buf));
  REQUIRE(std::string(buf) == "Vember Classic Lowpass 12 dB Standard");
  REQUIRE(std::fabs(SstFilterEngine::cutoffNote(1.0f) - 12.0f * std::log2(20000.0f / 440.0f)) < 1e-3f);
  REQUIRE(std::fabs(SstFilterEngine::cutoffNote(0.0f) - 12.0f * std::log2(20.0f / 440.0f)) < 1e-3f);
}

TEST_CASE("Classic Filters keep their 205 types in a permanent order") {
  std::ifstream file(std::string(EG13_TEST_DATA_DIR) + "/classic_filter_types.txt");
  REQUIRE(file.good());
  std::vector<std::string> frozen;
  for (std::string line; std::getline(file, line);) {
    if (!line.empty()) frozen.push_back(line);
  }
  REQUIRE(frozen.size() == 205);
  REQUIRE(SstFilterEngine::typeCount() == 205);
  for (int i = 0; i < 205; ++i) {
    INFO("type " << i);
    REQUIRE(std::string(SstFilterEngine::typeName(i)) == frozen[size_t(i)]);
  }
}

TEST_CASE("Classic Filters Type steps map to each type") {
  for (int i = 0; i < SstFilterEngine::typeCount(); ++i) {
    REQUIRE(SstFilterEngine::typeIndex(SstFilterEngine::typeValue(i)) == i);
  }
  REQUIRE(SstFilterEngine::typeIndex(1.0f) == SstFilterEngine::typeCount() - 1);
  REQUIRE(SstFilterEngine::typeIndex(std::numeric_limits<float>::quiet_NaN()) == 0);
  REQUIRE(SstFilterEngine::typeHasCharacter(typeNamed("K35 Lowpass Continuous")));
  REQUIRE_FALSE(SstFilterEngine::typeHasCharacter(0));
}

TEST_CASE("Classic Filters match sst-filters sample for sample") {
  const char* const named[] = {"Vember Classic Lowpass 12 dB Standard",
                               "Vintage Ladder Lowpass Runge-Kutta",
                               "K35 Lowpass Continuous",
                               "Fast SVF Bell",
                               "OB-Xd 4-pole Lowpass Morph",
                               "Comb Positive Feedback, Variable Mix",
                               "Diode Ladder Lowpass 24 dB"};
  const auto in = saw(1600, 110.0, 48000.0);
  for (const char* name : named) {
    const int type = typeNamed(name);
    INFO(name);
    SstFilterEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.6f;
    p.p[1] = 0.4f;
    p.p[2] = SstFilterEngine::typeValue(type);
    p.p[3] = 0.7f;
    Reference ref(type, 48000.0);
    const auto all = upstreamTypes();
    const bool bipolar = sfpp::Filter::coefficientsExtraIsBipolar(all[size_t(type)].first, all[size_t(type)].second, 0);
    const float extra = bipolar ? 2.0f * 0.7f - 1.0f : 0.7f;
    float l[16], r[16], el[16], er[16];
    for (size_t b = 0; b < in.size() / 16; ++b) {
      runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, p);
      ref.block(in.data() + b * 16, el, er, SstFilterEngine::cutoffNote(0.6f), 0.4f, extra, b == 0);
      for (int i = 0; i < 16; ++i) {
        REQUIRE(l[i] == el[i]);
        REQUIRE(r[i] == er[i]);
      }
    }
  }
}

TEST_CASE("Classic Filters interpolate a cutoff step across one block and snap after a reset") {
  const auto in = saw(64, 220.0, 48000.0);
  SstFilterEngine engine;
  engine.prepare(48000.0, 256);
  ParamValues a = defaults();
  a.p[0] = 0.3f;
  ParamValues b = a;
  b.p[0] = 0.8f;
  Reference interpolated(0, 48000.0), stepped(0, 48000.0);
  float l[16], r[16], il[16], ir[16], sl[16], sr[16];
  runBlock(engine, in.data(), in.data(), l, r, a);  // first block: constant coefficients
  interpolated.block(in.data(), il, ir, SstFilterEngine::cutoffNote(0.3f), 0.0f, 0.5f, true);
  stepped.block(in.data(), sl, sr, SstFilterEngine::cutoffNote(0.3f), 0.0f, 0.5f, true);
  for (int i = 0; i < 16; ++i) REQUIRE(l[i] == il[i]);
  runBlock(engine, in.data() + 16, in.data() + 16, l, r, b);  // the step
  interpolated.block(in.data() + 16, il, ir, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, false);
  stepped.block(in.data() + 16, sl, sr, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, true);
  bool differsFromStep = false;
  for (int i = 0; i < 16; ++i) {
    REQUIRE(l[i] == il[i]);
    differsFromStep = differsFromStep || l[i] != sl[i];
  }
  REQUIRE(differsFromStep);
  // Third block, same target: still the same as sst-filters driven directly.
  runBlock(engine, in.data() + 32, in.data() + 32, l, r, b);
  interpolated.block(in.data() + 32, il, ir, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, false);
  for (int i = 0; i < 16; ++i) REQUIRE(l[i] == il[i]);
  engine.reset();
  Reference fresh(0, 48000.0);
  runBlock(engine, in.data() + 48, in.data() + 48, l, r, b);
  fresh.block(in.data() + 48, il, ir, SstFilterEngine::cutoffNote(0.8f), 0.0f, 0.5f, true);
  for (int i = 0; i < 16; ++i) REQUIRE(l[i] == il[i]);
}

TEST_CASE("Classic Filters converge on a cutoff step block after block") {
  // 3 kHz at 48 kHz is exactly one cycle per 16-sample block, so a block's RMS is its gain.
  std::vector<float> sine(16);
  for (size_t i = 0; i < 16; ++i) sine[i] = 0.5f * std::sin(2.0f * 3.14159265f * float(i) / 16.0f);
  auto blockRms = [&](SstFilterEngine& e, const ParamValues& p) {
    float l[16], r[16];
    runBlock(e, sine.data(), sine.data(), l, r, p);
    double sum = 0.0;
    for (float v : l) sum += double(v) * double(v);
    return std::sqrt(sum / 16.0);
  };
  ParamValues low = defaults();
  low.p[0] = 0.3f;
  ParamValues high = low;
  high.p[0] = 0.8f;
  SstFilterEngine settled;
  settled.prepare(48000.0, 256);
  for (int i = 0; i < 200; ++i) blockRms(settled, high);
  const double target = blockRms(settled, high);
  SstFilterEngine engine;
  engine.prepare(48000.0, 256);
  for (int i = 0; i < 200; ++i) blockRms(engine, low);
  double previous = std::fabs(blockRms(engine, high) - target);  // the step block
  REQUIRE(previous > 0.05 * target);
  for (int block = 2; block <= 4; ++block) {  // second, third and fourth block after the step
    const double error = std::fabs(blockRms(engine, high) - target);
    INFO("block " << block);
    REQUIRE(error < previous);
    previous = error;
  }
  for (int i = 0; i < 60; ++i) blockRms(engine, high);
  REQUIRE(std::fabs(blockRms(engine, high) - target) < 0.005 * target);
}

TEST_CASE("Classic Filters keep L and R identical for a mono input, for every type") {
  const auto in = saw(320, 110.0, 48000.0);
  for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
    SstFilterEngine engine;
    engine.prepare(48000.0, 256);
    ParamValues p = defaults();
    p.p[0] = 0.5f;
    p.p[1] = 0.5f;
    p.p[2] = SstFilterEngine::typeValue(type);
    float l[16], r[16];
    for (size_t b = 0; b < in.size() / 16; ++b) {
      runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, p);
      for (int i = 0; i < 16; ++i) {
        INFO(SstFilterEngine::typeName(type));
        REQUIRE(l[i] == r[i]);
        REQUIRE(std::isfinite(l[i]));
      }
    }
  }
}

TEST_CASE("Classic Filters survive NaN and full resonance in every type") {
  const auto in = saw(1600, 110.0, 48000.0);
  ParamValues bad;
  for (float& v : bad.p) v = std::numeric_limits<float>::quiet_NaN();
  for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
    for (bool nan : {false, true}) {
      SstFilterEngine engine;
      engine.prepare(48000.0, 256);
      ParamValues p = defaults();
      p.p[0] = 0.5f;
      p.p[1] = 1.0f;
      p.p[2] = SstFilterEngine::typeValue(type);
      float l[16], r[16];
      for (size_t b = 0; b < in.size() / 16; ++b) {
        runBlock(engine, in.data() + b * 16, in.data() + b * 16, l, r, nan ? bad : p);
        for (int i = 0; i < 16; ++i) {
          INFO(SstFilterEngine::typeName(type) << (nan ? " (NaN)" : ""));
          REQUIRE(std::isfinite(l[i]));
        }
      }
    }
  }
}

TEST_CASE("Classic Filters type changes and processing do not allocate") {
  SstFilterEngine engine;
  engine.prepare(48000.0, 256);
  float in[16] = {}, l[16], r[16];
  for (int i = 0; i < 16; ++i) in[i] = 0.1f * float(i % 4);
  ParamValues p = defaults();
  runBlock(engine, in, in, l, r, p);
  size_t allocations = 0;
  {
    test::ScopedAllocationCounter counter;
    for (int type = 0; type < SstFilterEngine::typeCount(); ++type) {
      p.p[2] = SstFilterEngine::typeValue(type);
      p.p[0] = float(type % 10) / 10.0f;
      runBlock(engine, in, in, l, r, p);
      runBlock(engine, in, in, l, r, p);
    }
    const float* i2[2] = {in, in};
    float* o2[2] = {l, r};
    engine.process(i2, o2, p, 7);  // a short block re-times the coefficient ramp
    engine.reset();
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
}
