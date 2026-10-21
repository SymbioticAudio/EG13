#include "engine/native_rate_wrapper.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <utility>

#include "CDSPResampler.h"

namespace eg13 {

NativeRateWrapper::NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate,
                                     int nativeBlock)
    : inner_(std::move(inner)), nativeRate_(nativeRate), nativeBlock_(nativeBlock) {}

NativeRateWrapper::~NativeRateWrapper() = default;

void NativeRateWrapper::setAuxEnabled(bool on) {
  auxEnabled_ = on;
  inner_->setAuxEnabled(on);
}

void NativeRateWrapper::setStereo(bool stereo) { inner_->setStereo(stereo); }

void NativeRateWrapper::prepare(double hostRate, int maxBlock) {
  const double native = nativeRate_ > 0.0 ? nativeRate_ : hostRate;
  bypass_ = std::fabs(hostRate - native) < 1e-6;
  inner_->prepare(native, bypass_ ? std::max(maxBlock, nativeBlock_) : nativeBlock_);
  const EngineInfo& info = inner_->info();
  usesInput_ = info.usesInput;
  inChannels_ = usesInput_ ? 2 + std::clamp(info.sideInputs, 0, 2) : 0;
  outChannels_ = info.hasAux && auxEnabled_ ? 3 : 2;
  latency_ = 0;
  inputLatency_ = 0;
  inputPrime_ = 0;
  inPath_ = Path{};
  outPath_ = Path{};
  scratch_.assign(size_t(nativeBlock_), 0.0);
  inputScratch_.assign(size_t(maxBlock), 0.0);
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    resampler_[ch].reset();
    inputResampler_[ch].reset();
    native_[ch].assign(size_t(nativeBlock_), 0.0f);
    nativeIn_[ch].assign(size_t(nativeBlock_), 0.0f);
    fifo_[ch].clear();
    inputFifo_[ch].clear();
  }
  if (!bypass_) {
    for (int ch = 0; ch < outChannels_; ++ch) {
      resampler_[ch] = std::make_unique<r8b::CDSPResampler24>(native, hostRate, nativeBlock_,
                                                              kTransitionBandPercent);
      fifo_[ch].assign(size_t(resampler_[ch]->getMaxOutLen(nativeBlock_)), 0.0f);
    }
    latency_ = int(std::lround(double(resampler_[0]->getInLenBeforeOutPos(0)) * hostRate / native));
    if (usesInput_) {
      for (int ch = 0; ch < inChannels_; ++ch) {
        inputResampler_[ch] = std::make_unique<r8b::CDSPResampler24>(hostRate, native, maxBlock,
                                                                     kTransitionBandPercent);
      }
      const int withheld = inputResampler_[0]->getInLenBeforeOutPos(0);
      inputPrime_ = size_t(2 * nativeBlock_ + int(std::ceil(double(withheld) * native / hostRate)) + 1);
      const size_t capacity = inputPrime_ + size_t(inputResampler_[0]->getMaxOutLen(maxBlock)) +
                              size_t(std::ceil(double(latency_) * native / hostRate)) +
                              size_t(2 * nativeBlock_);
      for (int ch = 0; ch < inChannels_; ++ch) inputFifo_[ch].assign(capacity, 0.0f);
      inputLatency_ = latency_ + int(std::lround(double(inputPrime_) * hostRate / native));
      setUpPath(inPath_, hostRate, native, withheld, maxBlock);
      silence_.assign(size_t(inputResampler_[0]->getMaxOutLen(maxBlock)), 0.0);
    }
    setUpPath(outPath_, native, hostRate, resampler_[0]->getInLenBeforeOutPos(0), nativeBlock_);
    preroll_.assign(size_t(std::max(maxBlock, nativeBlock_)), 0.0);
  }
  // The engine's own latency (a fixed-block FIFO) delays both paths.
  engineLatency_ = int(std::lround(double(inner_->nativeLatency()) * hostRate / native));
  latency_ += engineLatency_;
  if (usesInput_) inputLatency_ += engineLatency_;
  extra_ = 0;
  for (auto& line : delay_) line.clear();
  reset();
}

void NativeRateWrapper::setExtraDelay(int samples) {
  extra_ = std::max(0, samples);
  for (int ch = 0; ch < kMaxChannels; ++ch) {
    delay_[ch].assign(ch < outChannels_ ? size_t(extra_) : 0, 0.0f);
  }
  delayPos_ = 0;
}

void NativeRateWrapper::reset() {
  inner_->reset();
  for (auto& resampler : resampler_) {
    if (resampler) resampler->clear();
  }
  for (auto& resampler : inputResampler_) {
    if (resampler) resampler->clear();
  }
  fifoRead_ = 0;
  fifoSize_ = 0;
  // r8brain skips its own latency after clear(); pad so the onset lands at
  // latencySamples() like it does in steady state. The engine's own latency is
  // real delay inside the engine, so only the converter's part is padded.
  zeroPad_ = bypass_ ? 0 : size_t(latency_ - engineLatency_);
  inputRead_ = 0;
  inputSize_ = inputPrime_;
  for (auto& ring : inputFifo_) std::fill(ring.begin(), ring.end(), 0.0f);
  underruns_ = 0;
  overruns_ = 0;
  for (auto& skip : inSkip_) skip = Skip{};
  for (auto& skip : outSkip_) skip = Skip{};
  inPath_.reset();
  outPath_.reset();
  lastInConverted_ = bypass_ ? 0 : inChannels_;
  lastOutConverted_ = bypass_ ? 0 : outChannels_;
  for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
  delayPos_ = 0;
}

void NativeRateWrapper::noteOn(float note, float velocity) { inner_->noteOn(note, velocity); }

void NativeRateWrapper::noteOff() { inner_->noteOff(); }

void NativeRateWrapper::setNote(float note) { inner_->setNote(note); }

const EngineInfo& NativeRateWrapper::info() const { return inner_->info(); }

void NativeRateWrapper::process(const float* const* in, float* const* out, const ParamValues& p,
                                int n) {
  if (bypass_) {
    inner_->process(usesInput_ ? in : nullptr, out, p, n);
  } else {
    if (usesInput_) pushInput(in, n);
    for (int i = 0; i < n; ++i) {
      if (zeroPad_ > 0) {
        for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = 0.0f;
        --zeroPad_;
        continue;
      }
      // The converter withholds its first outputs, so a block can yield nothing.
      while (fifoSize_ == 0) renderNativeBlock(p);
      for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = fifo_[ch][fifoRead_];
      ++fifoRead_;
      --fifoSize_;
    }
  }
  if (extra_ > 0) applyExtraDelay(out, n);
}

namespace {

// x equals ref over n samples; null reads as silence.
bool follows(const float* x, const float* ref, int n) {
  if (x == ref) return true;
  for (int i = 0; i < n; ++i) {
    if ((x != nullptr ? x[i] : 0.0f) != (ref != nullptr ? ref[i] : 0.0f)) return false;
  }
  return true;
}

}  // namespace

void NativeRateWrapper::Path::record(const float* x, int n) {
  const size_t size = history.size();
  if (size > 0) {
    for (int i = 0; i < n; ++i) history[(size_t(fed) + size_t(i)) % size] = x != nullptr ? x[i] : 0.0f;
  }
  fed += n;
}

void NativeRateWrapper::Path::reset() {
  fed = 0;
  std::fill(history.begin(), history.end(), 0.0f);
}

// A converter restarted with clear() and pre-rolled from a sample index that is a multiple of
// `period` (the input length after which the rate ratio's phase repeats) produces, from then
// on, the same output counts as channel 0's converter, and the same samples once the pre-roll
// covers its filter (span = twice the converter's latency plus a margin; r8brain 7.6, measured:
// within 1e-15, 5e-11 when down-sampling). Rates that are not whole numbers, or whose period
// is long, never skip.
void NativeRateWrapper::setUpPath(Path& path, double from, double to, int latencyIn, int maxChunk) {
  path = Path{};
  const int64_t a = std::llround(from);
  const int64_t b = std::llround(to);
  if (std::fabs(from - double(a)) > 1e-6 || std::fabs(to - double(b)) > 1e-6 || a <= 0 || b <= 0) return;
  const int64_t period = a / std::gcd(a, b);
  if (period > 4096) return;
  path.period = period;
  path.span = 2 * int64_t(latencyIn) + 64;
  path.history.assign(size_t(path.span + period + maxChunk), 0.0f);
}

// Why restart and pre-roll rather than crossfade or keep converting silence: a skipped
// converter's state is stale and out of step (its output count per call would differ), and
// converting silence costs as much as converting the signal. The reference's history is exactly
// what the channel carried while skipped, so the restarted converter reaches the state it would
// have had; the cost is one pre-roll of at most span + period samples per resumption.
void NativeRateWrapper::resume(r8b::CDSPResampler24& r, const Path& path, bool silence, int maxChunk,
                               std::vector<double>& scratch) {
  const int64_t end = path.fed;
  int64_t start = end > path.span ? end - path.span : 0;
  start -= start % path.period;
  r.clear();
  const size_t size = path.history.size();
  for (int64_t k = start; k < end;) {
    const int chunk = int(std::min<int64_t>(maxChunk, end - k));
    for (int i = 0; i < chunk; ++i) {
      scratch[size_t(i)] = silence ? 0.0 : double(path.history[size_t(k + i) % size]);
    }
    double* discarded = nullptr;
    r.process(scratch.data(), chunk, discarded);
    k += chunk;
  }
}

int NativeRateWrapper::convertedInputChannels() const { return lastInConverted_; }

int NativeRateWrapper::convertedOutputChannels() const { return lastOutConverted_; }

void NativeRateWrapper::pushInput(const float* const* in, int n) {
  const size_t capacity = inputFifo_[0].size();
  const bool canSkip = skipUnused_ && inPath_.period > 0;
  const float* src[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  for (int ch = 0; ch < inChannels_; ++ch) src[ch] = in != nullptr ? in[ch] : nullptr;  // a side input may be absent
  int produced = 0;
  int active = 0;
  double* converted[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  for (int ch = 0; ch < inChannels_; ++ch) {
    Skip& skip = inSkip_[ch];
    // R follows L; a side input follows silence.
    const bool same = ch > 0 && canSkip && follows(src[ch], ch == 1 ? src[0] : nullptr, n);
    if (skip.skipped && !same) {
      resume(*inputResampler_[ch], inPath_, ch != 1, int(inputScratch_.size()), preroll_);
      skip = Skip{};
    }
    if (skip.skipped) {
      converted[ch] = ch == 1 ? converted[0] : silence_.data();
    } else {
      for (int i = 0; i < n; ++i) inputScratch_[size_t(i)] = src[ch] != nullptr ? double(src[ch][i]) : 0.0;
      const int made = inputResampler_[ch]->process(inputScratch_.data(), n, converted[ch]);
      if (ch == 0) produced = made;
      assert(made == produced && "every converted channel stays in step");
      ++active;
    }
    if (ch > 0) {
      skip.run = same ? skip.run + size_t(n) : 0;
      if (skip.run >= size_t(inPath_.span) && canSkip) skip.skipped = true;
    }
  }
  inPath_.record(src[0], n);
  lastInConverted_ = active;
  size_t count = size_t(produced);
  size_t skip = 0;
  if (count > capacity) {  // cannot happen with the capacity set in prepare()
    skip = count - capacity;
    count = capacity;
    ++overruns_;
  }
  if (inputSize_ + count > capacity) {  // drop the oldest samples to fit
    const size_t drop = inputSize_ + count - capacity;
    inputRead_ = (inputRead_ + drop) % capacity;
    inputSize_ -= drop;
    ++overruns_;
  }
  for (int ch = 0; ch < inChannels_; ++ch) {
    for (size_t j = 0; j < count; ++j) {
      inputFifo_[ch][(inputRead_ + inputSize_ + j) % capacity] = float(converted[ch][skip + j]);
    }
  }
  inputSize_ += count;
}

void NativeRateWrapper::renderNativeBlock(const ParamValues& p) {
  const float* nativeIn[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  if (usesInput_) {
    const size_t capacity = inputFifo_[0].size();
    const size_t take = std::min(inputSize_, size_t(nativeBlock_));
    if (take < size_t(nativeBlock_)) ++underruns_;
    for (int ch = 0; ch < inChannels_; ++ch) {
      for (size_t j = 0; j < size_t(nativeBlock_); ++j) {
        nativeIn_[ch][j] = j < take ? inputFifo_[ch][(inputRead_ + j) % capacity] : 0.0f;
      }
      nativeIn[ch] = nativeIn_[ch].data();
    }
    inputRead_ = (inputRead_ + take) % capacity;
    inputSize_ -= take;
  }
  float* native[kMaxChannels] = {native_[0].data(), native_[1].data(), native_[2].data(), native_[3].data()};
  inner_->process(usesInput_ ? nativeIn : nullptr, native, p, nativeBlock_);
  const bool canSkip = skipUnused_ && outPath_.period > 0;
  int produced = 0;
  int active = 0;
  for (int ch = 0; ch < outChannels_; ++ch) {
    Skip& skip = outSkip_[ch];
    // R follows L (an engine running mono copies L to R); AUX is always converted.
    const bool skippable = ch == 1 && canSkip;
    const bool same = skippable && follows(native_[1].data(), native_[0].data(), nativeBlock_);
    if (skip.skipped && !same) {
      resume(*resampler_[ch], outPath_, false, nativeBlock_, preroll_);
      skip = Skip{};
    }
    if (skip.skipped) {
      std::copy(fifo_[0].begin(), fifo_[0].begin() + produced, fifo_[ch].begin());
    } else {
      for (int j = 0; j < nativeBlock_; ++j) scratch_[size_t(j)] = double(native_[ch][size_t(j)]);
      double* out = nullptr;
      // Every channel sees identical block lengths, so they produce the same count.
      const int made = resampler_[ch]->process(scratch_.data(), nativeBlock_, out);
      if (ch == 0) produced = made;
      assert(made == produced && "every converted channel stays in step");
      for (int j = 0; j < made; ++j) fifo_[ch][size_t(j)] = float(out[j]);
      ++active;
    }
    if (skippable) {
      skip.run = same ? skip.run + size_t(nativeBlock_) : 0;
      if (skip.run >= size_t(outPath_.span)) skip.skipped = true;
    }
  }
  outPath_.record(native_[0].data(), nativeBlock_);
  lastOutConverted_ = active;
  fifoRead_ = 0;
  fifoSize_ = size_t(produced);
}

void NativeRateWrapper::applyExtraDelay(float* const* out, int n) {
  for (int i = 0; i < n; ++i) {
    for (int ch = 0; ch < outChannels_; ++ch) {
      const float delayed = delay_[ch][delayPos_];
      delay_[ch][delayPos_] = out[ch][i];
      out[ch][i] = delayed;
    }
    delayPos_ = (delayPos_ + 1) % size_t(extra_);
  }
}

}  // namespace eg13
