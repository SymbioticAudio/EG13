#include "engine/native_rate_wrapper.h"

#include <cmath>
#include <utility>

#include "CDSPResampler.h"

namespace eg13 {

NativeRateWrapper::NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate,
                                     int nativeBlock)
    : inner_(std::move(inner)), nativeRate_(nativeRate), nativeBlock_(nativeBlock) {}

NativeRateWrapper::~NativeRateWrapper() = default;

void NativeRateWrapper::prepare(double hostRate, int) {
  inner_->prepare(nativeRate_, nativeBlock_);
  bypass_ = std::fabs(hostRate - nativeRate_) < 1e-6;
  latency_ = 0;
  scratch_.assign(size_t(nativeBlock_), 0.0);
  for (int ch = 0; ch < 2; ++ch) {
    resampler_[ch].reset();
    native_[ch].assign(size_t(nativeBlock_), 0.0f);
    fifo_[ch].clear();
  }
  if (!bypass_) {
    for (int ch = 0; ch < 2; ++ch) {
      resampler_[ch] = std::make_unique<r8b::CDSPResampler24>(nativeRate_, hostRate, nativeBlock_,
                                                              kTransitionBandPercent);
      fifo_[ch].assign(size_t(resampler_[ch]->getMaxOutLen(nativeBlock_)), 0.0f);
    }
    latency_ = int(std::lround(double(resampler_[0]->getInLenBeforeOutPos(0)) * hostRate /
                               nativeRate_));
  }
  reset();
}

void NativeRateWrapper::reset() {
  inner_->reset();
  for (auto& resampler : resampler_) {
    if (resampler) resampler->clear();
  }
  fifoRead_ = 0;
  fifoSize_ = 0;
}

void NativeRateWrapper::noteOn(float note, float velocity) { inner_->noteOn(note, velocity); }

void NativeRateWrapper::noteOff() { inner_->noteOff(); }

void NativeRateWrapper::setNote(float note) { inner_->setNote(note); }

const EngineInfo& NativeRateWrapper::info() const { return inner_->info(); }

void NativeRateWrapper::process(const float* const* in, float* const* out, const ParamValues& p,
                                int n) {
  if (bypass_) {
    inner_->process(in, out, p, n);
    return;
  }
  for (int i = 0; i < n; ++i) {
    // The converter withholds its first outputs, so a block can yield nothing.
    while (fifoSize_ == 0) renderNativeBlock(p);
    out[0][i] = fifo_[0][fifoRead_];
    out[1][i] = fifo_[1][fifoRead_];
    ++fifoRead_;
    --fifoSize_;
  }
}

void NativeRateWrapper::renderNativeBlock(const ParamValues& p) {
  float* native[2] = {native_[0].data(), native_[1].data()};
  inner_->process(nullptr, native, p, nativeBlock_);
  int produced = 0;
  for (int ch = 0; ch < 2; ++ch) {
    for (int j = 0; j < nativeBlock_; ++j) scratch_[size_t(j)] = double(native_[ch][size_t(j)]);
    double* converted = nullptr;
    // Both channels see identical block lengths, so they produce the same count.
    produced = resampler_[ch]->process(scratch_.data(), nativeBlock_, converted);
    for (int j = 0; j < produced; ++j) fifo_[ch][size_t(j)] = float(converted[j]);
  }
  fifoRead_ = 0;
  fifoSize_ = size_t(produced);
}

}  // namespace eg13
