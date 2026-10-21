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

#include <new>

TEST_CASE("ScopedAllocationCounter counts bytes and aligned allocations") {
  size_t count = 0;
  size_t bytes = 0;
  {
    eg13::test::ScopedAllocationCounter counter;
    void* a = ::operator new(100);
    void* b = ::operator new(64, std::align_val_t(64));
    ::operator delete(a);
    ::operator delete(b, std::align_val_t(64));
    count = counter.count();
    bytes = counter.bytes();
  }
  REQUIRE(count == 2);
  REQUIRE(bytes == 164);
}
