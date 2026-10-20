#pragma once
// RAII: flush denormals to zero (FTZ/DAZ) for the current thread, restore on exit.
#include <cstdint>

#if defined(__SSE__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

namespace eg13 {

class ScopedFlushDenormals {
 public:
#if defined(__SSE__) || defined(__x86_64__)
  ScopedFlushDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8000u | 0x0040u); }
  ~ScopedFlushDenormals() { _mm_setcsr(saved_); }

 private:
  unsigned int saved_;
#elif defined(__aarch64__)
  ScopedFlushDenormals() {
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(saved_));
    const uint64_t fz = saved_ | (uint64_t{1} << 24);
    __asm__ __volatile__("msr fpcr, %0" : : "r"(fz));
  }
  ~ScopedFlushDenormals() { __asm__ __volatile__("msr fpcr, %0" : : "r"(saved_)); }

 private:
  uint64_t saved_;
#else
  ScopedFlushDenormals() {}
  ~ScopedFlushDenormals() {}
#endif

 public:
  ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
  ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
};

}  // namespace eg13
