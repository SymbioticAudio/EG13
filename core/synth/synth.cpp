#include "synth/synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eg13 {

Synth::Synth(PluginKind kind) : kind_(kind) { resetParamsToDefaults(); }

void Synth::prepare(double sampleRate, int maxBlock) {
  engine_.prepare(sampleRate, maxBlock);
  notes_.clear();
}

void Synth::reset() {
  engine_.reset();
  notes_.clear();
}

void Synth::noteOn(int key, float velocity) {
  notes_.noteOn(key, velocity);
  engine_.noteOn(float(key), velocity);
}

void Synth::noteOff(int key) {
  const bool wasTop = !notes_.empty() && notes_.topKey() == key;
  notes_.noteOff(key);
  if (notes_.empty()) {
    engine_.noteOff();
  } else if (wasTop) {
    engine_.noteOn(float(notes_.topKey()), notes_.topVelocity());
  }
}

void Synth::allNotesOff() {
  notes_.clear();
  engine_.noteOff();
}

bool Synth::setParam(uint32_t id, double value) {
  const int index = paramIndex(id);
  if (index < 0 || std::isnan(value)) return false;
  values_[size_t(index)].store(float(std::clamp(value, 0.0, 1.0)), std::memory_order_relaxed);
  return true;
}

double Synth::param(uint32_t id) const {
  const int index = paramIndex(id);
  return index < 0 ? 0.0 : double(load(size_t(index)));
}

void Synth::resetParamsToDefaults() {
  for (size_t i = 0; i < kParamCount; ++i) {
    values_[i].store(paramSpec(i).defaultValue, std::memory_order_relaxed);
  }
}

void Synth::process(const float* const* in, float* const* out, int n) {
  ParamValues p;
  for (size_t i = 0; i < kSourceParamCount; ++i) p.p[i] = load(i);
  const float gain = load(kMasterLevelIndex);
  const bool addInput = kind_ == PluginKind::Effect && in != nullptr;

  for (int done = 0; done < n;) {
    const int count = std::min(kChunk, n - done);
    if (addInput) {
      // Copy first: the host may pass the same buffer for input and output.
      std::memcpy(input_[0], in[0] + done, size_t(count) * sizeof(float));
      std::memcpy(input_[1], in[1] + done, size_t(count) * sizeof(float));
    }
    float* r[2] = {render_[0], render_[1]};
    engine_.process(nullptr, r, p, count);
    for (int ch = 0; ch < 2; ++ch) {
      for (int i = 0; i < count; ++i) {
        float v = render_[ch][i] * gain;
        if (addInput) v += input_[ch][i];
        out[ch][done + i] = v;
      }
    }
    done += count;
  }
}

}  // namespace eg13
