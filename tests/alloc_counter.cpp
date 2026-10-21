#include "alloc_counter.h"

#include <algorithm>
#include <cstdlib>
#include <new>

namespace {
thread_local bool tCounting = false;
thread_local size_t tCount = 0;
thread_local size_t tBytes = 0;

void note(std::size_t size) {
  if (tCounting) {
    ++tCount;
    tBytes += size;
  }
}

void* alignedAlloc(std::size_t size, std::align_val_t al) {
  void* p = nullptr;
  const std::size_t alignment = std::max(sizeof(void*), std::size_t(al));
  if (posix_memalign(&p, alignment, size ? size : 1) != 0) throw std::bad_alloc();
  return p;
}
}  // namespace

namespace eg13::test {

ScopedAllocationCounter::ScopedAllocationCounter() {
  tCount = 0;
  tBytes = 0;
  tCounting = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter() { tCounting = false; }

size_t ScopedAllocationCounter::count() const { return tCount; }

size_t ScopedAllocationCounter::bytes() const { return tBytes; }

}  // namespace eg13::test

void* operator new(std::size_t size) {
  note(size);
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size) { return operator new(size); }

void* operator new(std::size_t size, std::align_val_t al) {
  note(size);
  return alignedAlloc(size, al);
}

void* operator new[](std::size_t size, std::align_val_t al) { return operator new(size, al); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
