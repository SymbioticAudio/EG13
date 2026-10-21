#pragma once

#include <cstddef>

namespace eg13::test {

// Counts global operator new calls made on the current thread while alive.
// Used to prove audio-thread code paths never allocate.
class ScopedAllocationCounter {
 public:
  ScopedAllocationCounter();
  ~ScopedAllocationCounter();
  ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
  ScopedAllocationCounter& operator=(const ScopedAllocationCounter&) = delete;

  size_t count() const;
  size_t bytes() const;  // bytes requested by those allocations
};

}  // namespace eg13::test
