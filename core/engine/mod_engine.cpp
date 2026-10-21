#include "engine/mod_engine.h"

#include <cmath>
#include <iterator>
#include <numeric>

namespace eg13 {

bool operator==(const EnvelopeShape& a, const EnvelopeShape& b) {
  if (a.count != b.count) return false;
  for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
    if (a.type[i] != b.type[i] || a.loop[i] != b.loop[i]) return false;
  }
  return true;
}

EnvelopeShape sanitized(const EnvelopeShape& shape) {
  EnvelopeShape s;
  s.count = uint8_t(std::clamp(int(shape.count), 1, EnvelopeShape::kMaxSegments));
  for (int i = 0; i < EnvelopeShape::kMaxSegments; ++i) {
    s.type[i] = shape.type[i] <= EnvelopeShape::Hold ? shape.type[i] : uint8_t(EnvelopeShape::Ramp);
    s.loop[i] = shape.loop[i] != 0 ? 1 : 0;
  }
  return s;
}

NativeModEngine::NativeModEngine(double nativeRate, int nativeBlock)
    : nativeRate_(nativeRate), nativeBlock_(std::clamp(nativeBlock, 1, kMaxNativeBlock)) {}

void NativeModEngine::prepare(double hostRate) {
  // Rates in mHz: exact for every common rate, and below 2^31 up to 2 MHz, so in process()
  // hostPhase_ * rateNum_ (< (2^31 + n) * 2^31 <= 2^63 for any int n) fits in 64 bits.
  constexpr double kMaxRate = 2.0e6;
  const bool valid = hostRate > 0.0 && hostRate <= kMaxRate && nativeRate_ > 0.0 && nativeRate_ <= kMaxRate;
  rateNum_ = valid ? int64_t(std::llround(nativeRate_ * 1000.0)) : 1;
  rateDen_ = valid ? int64_t(std::llround(hostRate * 1000.0)) : 1;
  const int64_t divisor = std::gcd(rateNum_, rateDen_);
  rateNum_ /= divisor;
  rateDen_ /= divisor;
  ratio_ = double(rateNum_) / double(rateDen_);
  // The grid restarts with the new rate (process() needs hostPhase_ < rateDen_).
  periodNative_ = 0;
  hostPhase_ = 0;
  rendered_ = 0;
}

void NativeModEngine::reset() {
  periodNative_ = 0;
  hostPhase_ = 0;
  rendered_ = 0;
  high_ = false;
  retrigger_ = false;
  fall_ = false;
  jump_ = false;
  for (auto& output : block_) std::fill(std::begin(output), std::end(output), 0.0f);
  std::fill(std::begin(held_), std::end(held_), 0.0f);
  resetEngine();
}

void NativeModEngine::process(const ModContext& ctx, const ParamValues& p, int n, float* out) {
  retrigger_ = retrigger_ || ctx.retrigger;
  jump_ = jump_ || ctx.beatJump;
  // Native time is computed exactly from the host sample count, not accumulated, so it never
  // drifts: due is floor(native time at the end of the tick).
  const double start = double(periodNative_) + double(hostPhase_) * ratio_;
  hostPhase_ += std::max(n, 0);
  if (hostPhase_ >= rateDen_) {
    const int64_t periods = hostPhase_ / rateDen_;
    periodNative_ += periods * rateNum_;
    hostPhase_ -= periods * rateDen_;
  }
  const int64_t due = periodNative_ + hostPhase_ * rateNum_ / rateDen_;
  const double beatsPerNative = ctx.beatsPerSample / ratio_;
  // nativeBlock_ is clamped to kMaxNativeBlock by the constructor; restated so the compiler sees
  // the bound (GCC 13 -Wstringop-overflow).
  const int block = std::min(nativeBlock_, int(kMaxNativeBlock));
  while (rendered_ < due) {
    uint8_t gate[kMaxNativeBlock];
    double beat[kMaxNativeBlock];
    for (int i = 0; i < block; ++i) {
      uint8_t flags;
      if (retrigger_) {  // a new note: rising edge, high
        flags = kGateRising | kGateHigh;
        high_ = true;
        retrigger_ = false;
        fall_ = !ctx.gate;  // already released within the tick: a one-sample trigger
      } else if (ctx.gate && !fall_) {
        flags = high_ ? uint8_t(kGateHigh) : uint8_t(kGateRising | kGateHigh);
        high_ = true;
      } else {
        flags = high_ ? kGateFalling : kGateLow;
        high_ = false;
        fall_ = false;
      }
      gate[i] = flags;
      beat[i] = ctx.beat + (double(rendered_ + i) - start) * beatsPerNative;
    }
    renderBlock(p, gate, beat, beatsPerNative, jump_, block_);
    jump_ = false;
    rendered_ += nativeBlock_;
  }
  const int64_t index = due - 1 - (rendered_ - nativeBlock_);
  if (due > 0 && index >= 0 && index < nativeBlock_) {
    for (int k = 0; k < kMaxModOutputs; ++k) held_[k] = block_[k][index];
  }
  std::copy(std::begin(held_), std::end(held_), out);
}

const ModEngineInfo& OffModEngine::staticInfo() {
  static const ModEngineInfo kInfo = {{"off", "Off", 0, nullptr, false}, 0, nullptr, false};
  return kInfo;
}

}  // namespace eg13
