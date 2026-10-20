// Copyright 2012 Emilie Gillet. MIT licence (see random.h).
//
// EG13 patch: thread_local definition of the generator state.

#include "stmlib/utils/random.h"

namespace stmlib {

/* static */
thread_local uint32_t Random::rng_state_ = 0x21;

}  // namespace stmlib
