#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "beads/dsp/processor.h"
#include "clouds/dsp/granular_processor.h"
#include "elements/dsp/fx/reverb.h"
#include "stmlib/utils/gate_flags.h"

// Bare upstream classes only: these prove the third-party build links and the objects run. No
// EG13 adapter code is involved.

namespace {

bool anyNonZero(const std::vector<float>& x) {
  for (float v : x)
    if (v != 0.0f) return true;
  return false;
}

bool allFinite(const std::vector<float>& x) {
  for (float v : x)
    if (!std::isfinite(v)) return false;
  return true;
}

constexpr float kTwoPi = 6.283185307179586f;

}  // namespace

TEST_CASE("Bare Clouds processes a block") {
  static clouds::GranularProcessor processor;
  static uint8_t large[118784];
  static uint8_t small[65536 - 128];
  std::memset(static_cast<void*>(&processor), 0, sizeof(processor));  // Init() leaves members uninitialised
  std::memset(large, 0, sizeof(large));
  std::memset(small, 0, sizeof(small));
  processor.Init(large, sizeof(large), small, sizeof(small));

  clouds::Parameters* p = processor.mutable_parameters();
  p->position = 0.5f;
  p->size = 0.5f;
  p->pitch = 0.0f;
  p->density = 0.5f;
  p->texture = 0.5f;
  p->dry_wet = 0.5f;
  p->stereo_spread = 0.0f;
  p->feedback = 0.0f;
  p->reverb = 0.0f;
  p->freeze = false;
  p->trigger = false;
  p->gate = false;
  p->granular.overlap = 0.5f;
  p->granular.window_shape = 0.0f;
  p->granular.stereo_spread = 0.0f;
  p->granular.use_deterministic_seed = false;
  p->spectral.quantization = 0.0f;
  p->spectral.refresh_rate = 0.0f;
  p->spectral.phase_randomization = 0.0f;
  p->spectral.warp = 0.0f;

  clouds::ShortFrame in[32];
  clouds::ShortFrame out[32];
  for (int i = 0; i < 32; ++i) {
    const short s = static_cast<short>(16000.0f * std::sin(kTwoPi * 440.0f * float(i) / 32000.0f));
    in[i].l = s;
    in[i].r = s;
  }
  processor.Prepare();
  processor.Process(in, out, 32);

  std::vector<float> result;
  for (const auto& f : out) {
    result.push_back(float(f.l));
    result.push_back(float(f.r));
  }
  REQUIRE(anyNonZero(result));
}

TEST_CASE("Bare Beads processes a block at every media") {
  static beads::Processor processor;
  static uint8_t delayBuffer1[512 * 1024];
  static uint8_t delayBuffer2[256 * 1024];
  static uint16_t reverbBuffer[32768];
  std::memset(static_cast<void*>(&processor), 0, sizeof(processor));
  std::memset(delayBuffer1, 0, sizeof(delayBuffer1));
  std::memset(delayBuffer2, 0, sizeof(delayBuffer2));
  std::memset(reverbBuffer, 0, sizeof(reverbBuffer));
  uint8_t* buffers[3] = {delayBuffer1, delayBuffer1 + 256 * 1024, delayBuffer2};  // beads.cc:307-311
  processor.Init(buffers, reverbBuffer);

  beads::Parameters params;
  std::memset(static_cast<void*>(&params), 0, sizeof(params));
  for (beads::RandomizedParameter* r : {&params.time, &params.size, &params.shape, &params.pitch}) {
    r->base = 0.5f;
    r->attenuverter = 0.5f;
    r->has_cv = false;
    r->cv = 0.0f;
  }
  params.monophonic_mode = false;
  beads::ModulatedParameter density;
  density.base = 0.5f;
  density.cv = 0.0f;
  beads::Processor::MixParameters mix;
  mix.feedback = 0.3f;
  mix.dry_wet = 0.5f;
  mix.reverb_amount = 0.3f;
  mix.output_gate = false;

  for (int media = 0; media < 4; ++media) {
    processor.Configure(beads::Processor::Media(media), false);
    const size_t blocks = 40;
    const size_t rate = size_t(processor.sample_rate());  // the module's own table
    const size_t size = rate / 4000;
    std::vector<float> result;
    for (size_t b = 0; b < blocks; ++b) {
      std::vector<beads::FloatFrame> in(size), out(size);
      bool freeze[64] = {};
      for (size_t i = 0; i < size; ++i) {
        const float s = 0.5f * std::sin(kTwoPi * 440.0f * float(b * size + i) / float(rate));
        in[i].l = s;
        in[i].r = s;
      }
      processor.Process(beads::SEED_BUTTON_STATE_OFF, nullptr, freeze, density, params, mix,
                        in.data(), out.data(), size);
      for (const auto& f : out) {
        result.push_back(f.l);
        result.push_back(f.r);
      }
    }
    REQUIRE(allFinite(result));
    REQUIRE(anyNonZero(result));  // dry_wet 0.5 passes some of the non-silent input
  }
}

TEST_CASE("Bare Elements reverb processes a block") {
  static elements::Reverb reverb;
  static uint16_t buffer[32768];
  std::memset(static_cast<void*>(&reverb), 0, sizeof(reverb));  // Init() leaves the decay and gain members unset
  std::memset(buffer, 0, sizeof(buffer));
  reverb.Init(buffer);
  reverb.set_amount(0.5f);
  reverb.set_input_gain(0.2f);
  reverb.set_time(0.5f);

  std::vector<float> left(16), right(16);
  for (int i = 0; i < 16; ++i) {
    left[i] = right[i] = 0.5f * std::sin(kTwoPi * 440.0f * float(i) / 32000.0f);
  }
  reverb.Process(left.data(), right.data(), 16);
  REQUIRE(allFinite(left));
  REQUIRE(allFinite(right));
}
