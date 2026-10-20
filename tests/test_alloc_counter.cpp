#include <catch2/catch_test_macros.hpp>

#include "alloc_counter.h"

TEST_CASE("ScopedAllocationCounter counts allocations on this thread") {
  eg13::test::ScopedAllocationCounter counter;
  void* a = ::operator new(16);
  void* b = ::operator new[](32);
  ::operator delete(a);
  ::operator delete[](b);
  REQUIRE(counter.count() == 2);
}

TEST_CASE("ScopedAllocationCounter sees nothing when nothing allocates") {
  int stack[4] = {1, 2, 3, 4};
  eg13::test::ScopedAllocationCounter counter;
  int sum = 0;
  for (int x : stack) sum += x;
  REQUIRE(sum == 10);
  REQUIRE(counter.count() == 0);
}
