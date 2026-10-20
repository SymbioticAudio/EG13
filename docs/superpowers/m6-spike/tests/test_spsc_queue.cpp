#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

#include "alloc_counter.h"
#include "util/spsc_queue.h"

using eg13::SpscQueue;

TEST_CASE("SpscQueue is FIFO and reports full and empty") {
  SpscQueue<int, 4> queue;
  int out = -1;
  REQUIRE(queue.empty());
  REQUIRE_FALSE(queue.pop(out));
  for (int i = 0; i < 4; ++i) REQUIRE(queue.push(i));
  REQUIRE_FALSE(queue.push(99));  // full: the value is not stored
  for (int i = 0; i < 4; ++i) {
    REQUIRE(queue.pop(out));
    REQUIRE(out == i);
  }
  REQUIRE(queue.empty());
}

TEST_CASE("SpscQueue wraps around many times") {
  SpscQueue<int, 8> queue;
  int out = 0;
  for (int i = 0; i < 1000; ++i) {
    REQUIRE(queue.push(i));
    REQUIRE(queue.push(i + 1000000));
    REQUIRE(queue.pop(out));
    REQUIRE(out == i);
    REQUIRE(queue.pop(out));
    REQUIRE(out == i + 1000000);
  }
}

TEST_CASE("SpscQueue push and pop do not allocate") {
  SpscQueue<int, 16> queue;
  int out = 0;
  eg13::test::ScopedAllocationCounter counter;
  for (int i = 0; i < 100; ++i) {
    queue.push(i);
    queue.pop(out);
  }
  const size_t allocations = counter.count();
  REQUIRE(allocations == 0);
}

TEST_CASE("SpscQueue delivers every item in order across threads") {
  constexpr uint32_t kItems = 200000;
  SpscQueue<uint32_t, 64> queue;
  std::thread producer([&queue] {
    for (uint32_t i = 0; i < kItems;) {
      if (queue.push(i)) ++i;
    }
  });
  uint32_t expected = 0;
  bool inOrder = true;
  while (expected < kItems) {
    uint32_t value = 0;
    if (queue.pop(value)) {
      inOrder = inOrder && value == expected;
      ++expected;
    }
  }
  producer.join();
  REQUIRE(inOrder);
  REQUIRE(queue.empty());
}
