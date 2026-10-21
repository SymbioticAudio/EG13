// Built without Rack's flags: the one definition of the shared rack::random state.
#include "rack_prelude.h"

namespace eg13_rack_shim {

std::atomic<uint64_t>& randomState() {
  static std::atomic<uint64_t> state{kRandomSeed};  // constant-initialised: no guard, no allocation
  static_assert(std::atomic<uint64_t>::is_always_lock_free, "the audio thread must not lock");
  return state;
}

}  // namespace eg13_rack_shim
