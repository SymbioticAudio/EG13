#include "engine/shared_random.h"

#include "engine/rack_shim/rack_prelude.h"
#include "stmlib/utils/random.h"

namespace eg13 {

namespace {
constexpr uint32_t kStmlibStartSeed = 0x21;  // stmlib's rng_state_ at thread start (random.cc)
}  // namespace

void reseedSharedRandom() {
  stmlib::Random::Seed(kStmlibStartSeed);
  eg13_rack_shim::randomState().store(eg13_rack_shim::kRandomSeed, std::memory_order_relaxed);
}

}  // namespace eg13
