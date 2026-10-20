#include "alloc_counter.h"

#include <cstdlib>
#include <new>

namespace {
thread_local bool tCounting = false;
thread_local size_t tCount = 0;
}  // namespace

namespace eg13::test {

ScopedAllocationCounter::ScopedAllocationCounter() {
  tCount = 0;
  tCounting = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() { tCounting = false; }

size_t ScopedAllocationCounter::count() const { return tCount; }

}  // namespace eg13::test

void* operator new(std::size_t size) {
  if (tCounting) ++tCount;
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
