#include "engine/native_rate_wrapper.h"

#include <algorithm>
#include <cmath>
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
    }
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

void NativeRateWrapper::pushInput(const float* const* in, int n) {
  const size_t capacity = inputFifo_[0].size();
  int produced = 0;
  double* converted[kMaxChannels] = {nullptr, nullptr, nullptr, nullptr};
  for (int ch = 0; ch < inChannels_; ++ch) {
    const float* src = in != nullptr ? in[ch] : nullptr;  // a side input may be absent
    for (int i = 0; i < n; ++i) inputScratch_[size_t(i)] = src != nullptr ? double(src[i]) : 0.0;
    produced = inputResampler_[ch]->process(inputScratch_.data(), n, converted[ch]);
  }
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
  int produced = 0;
  for (int ch = 0; ch < outChannels_; ++ch) {
    for (int j = 0; j < nativeBlock_; ++j) scratch_[size_t(j)] = double(native_[ch][size_t(j)]);
    double* converted = nullptr;
    // Every channel sees identical block lengths, so they produce the same count.
    produced = resampler_[ch]->process(scratch_.data(), nativeBlock_, converted);
    for (int j = 0; j < produced; ++j) fifo_[ch][size_t(j)] = float(converted[j]);
  }
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
