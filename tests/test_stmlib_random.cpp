#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

#include "stmlib/utils/random.h"

TEST_CASE("stmlib::Random state is per thread") {
  stmlib::Random::Seed(1234);
  const uint32_t expected = stmlib::Random::GetWord();

  stmlib::Random::Seed(1234);
  std::thread other([] {
    stmlib::Random::Seed(999);
    for (int i = 0; i < 100; ++i) stmlib::Random::GetWord();
  });
  other.join();
  REQUIRE(stmlib::Random::GetWord() == expected);
}

TEST_CASE("stmlib::Random starts from the upstream seed on every thread") {
  uint32_t initial = 0;
  std::thread fresh([&initial] { initial = stmlib::Random::state(); });
  fresh.join();
  REQUIRE(initial == 0x21u);
}
