#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#include "plaits/dsp/voice.h"

TEST_CASE("Upstream Plaits renders a note") {
  static char ram[16384];
  stmlib::BufferAllocator allocator(ram, sizeof(ram));
  auto voice = std::make_unique<plaits::Voice>();
  voice->Init(&allocator);

  plaits::Patch patch = {};
  patch.note = 69.0f;
  patch.harmonics = 0.5f;
  patch.timbre = 0.5f;
  patch.morph = 0.5f;
  patch.engine = 8;  // virtual analog
  patch.decay = 0.5f;
  patch.lpg_colour = 0.5f;

  plaits::Modulations mods = {};
  mods.trigger_patched = true;
  mods.level_patched = true;
  mods.trigger = 1.0f;
  mods.level = 1.0f;

  plaits::Voice::Frame frames[plaits::kBlockSize];
  double sumSquares = 0.0;
  int count = 0;
  for (int block = 0; block < 4000; ++block) {
    voice->Render(patch, mods, frames, plaits::kBlockSize);
    for (const auto& f : frames) {
      sumSquares += double(f.out) * double(f.out);
      ++count;
    }
  }
  REQUIRE(std::sqrt(sumSquares / count) > 1000.0);
}
