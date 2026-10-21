// Built without Rack's flags: the one definition of the shared rack::random state.
#include "rack_prelude.h"

namespace eg13_rack_shim {

uint64_t& randomState() {
  thread_local uint64_t state = kRandomSeed;
  return state;
}

}  // namespace eg13_rack_shim
