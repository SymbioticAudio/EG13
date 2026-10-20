#include <catch2/catch_test_macros.hpp>

#include "util/denormal_guard.h"

TEST_CASE("ScopedFlushDenormals flushes inside scope and restores after", "[denormal]") {
#if defined(__x86_64__) || defined(__SSE__) || defined(__aarch64__)
  volatile float tiny = 1e-40f;  // subnormal
  {
    eg13::ScopedFlushDenormals guard;
    volatile float r = tiny * 0.5f;
    REQUIRE(r == 0.0f);
  }
  volatile float r2 = tiny * 1.0f;
  REQUIRE(r2 != 0.0f);
#else
  SUCCEED();
#endif
}
