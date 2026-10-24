#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace eg13 {

// Bounded single-producer, single-consumer queue: lock-free and allocation-free.
// One thread pushes and one thread pops. The threads may change over time (e.g. CLAP
// flush runs on the main thread while the plugin is inactive), but push never runs
// concurrently with push, nor pop with pop.
template <typename T, size_t Capacity>
class SpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
  static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");

 public:
  bool push(const T& value) {
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head - tail_.load(std::memory_order_acquire) == Capacity) return false;
    slots_[head & (Capacity - 1)] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  bool pop(T& out) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    if (head_.load(std::memory_order_acquire) == tail) return false;
    out = slots_[tail & (Capacity - 1)];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
  }

  static constexpr size_t capacity() { return Capacity; }

 private:
  std::array<T, Capacity> slots_{};
  alignas(64) std::atomic<size_t> head_{0};  // written by the producer
  alignas(64) std::atomic<size_t> tail_{0};  // written by the consumer
};

}  // namespace eg13
