#include "voice/voice.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace eg13 {

namespace {
constexpr float kSilenceThreshold = 3.1623e-5f;  // -90 dBFS
constexpr double kSilenceSeconds = 0.05;
}  // namespace

Voice::Voice(SourceSpec source) {
  sources_.push_back(std::make_unique<NativeRateWrapper>(std::move(source.engine),
                                                         source.nativeRate, source.nativeBlock));
}

Voice::Voice(std::vector<SourceSpec> sources) {
  for (auto& s : sources) {
    sources_.push_back(
        std::make_unique<NativeRateWrapper>(std::move(s.engine), s.nativeRate, s.nativeBlock));
  }
  assert(!sources_.empty() && "a voice needs at least one source engine");
}

void Voice::prepare(double hostRate, int maxBlock, Alignment alignment) {
  hostRate_ = hostRate;
  silenceLimit_ = int(hostRate * kSilenceSeconds);
  auto path = [alignment](const NativeRateWrapper& s) {
    return alignment == Alignment::Input && s.info().usesInput ? s.inputLatencySamples()
                                                               : s.outputLatencySamples();
  };
  latency_ = 0;
  for (auto& s : sources_) {
    s->prepare(hostRate, maxBlock);
    latency_ = std::max(latency_, path(*s));
  }
  for (auto& s : sources_) s->setExtraDelay(latency_ - path(*s));
  reset();
}

void Voice::reset() {
  for (auto& s : sources_) s->reset();
  gate_ = false;
  retrigger_ = false;
  active_ = false;
  silentSamples_ = 0;
}

void Voice::setEngine(int engine) {
  engine = std::clamp(engine, 0, int(sources_.size()) - 1);
  if (engine == current_) return;
  // Hard cut: the old engine is left as is (it is reset when selected again or by
  // reset()), and a held note restarts after latencySamples() of silence.
  current_ = engine;
  source().reset();
  retrigger_ = false;
  silentSamples_ = 0;
  if (gate_) {
    source().noteOn(float(currentNote_) + pitchOffset_, velocity_);
  } else {
    active_ = false;
  }
}

void Voice::trigger(int key, float velocity) {
  targetNote_ = float(key);
  velocity_ = velocity;
  retrigger_ = false;
  if (!active_) {
    source().reset();  // drop stale converter samples from the previous note
    currentNote_ = double(targetNote_);
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = double(targetNote_);
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
  source().noteOn(float(currentNote_) + pitchOffset_, velocity);
}

void Voice::glideTo(int key) {
  targetNote_ = float(key);
  if (glideSeconds_ <= 0.0f) currentNote_ = double(targetNote_);
}

bool Voice::blockIsFinite(int count) const {
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(buffer_[0][i]) || !std::isfinite(buffer_[1][i])) return false;
  }
  return true;
}

void Voice::release() {
  gate_ = false;
  retrigger_ = false;  // a pending NaN re-gate must not outlive the key release
  source().noteOff();
}

void Voice::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  if (!active_) return;
  if (retrigger_ && gate_) {
    source().noteOn(float(currentNote_) + pitchOffset_, velocity_);
    retrigger_ = false;
  }
  for (int done = 0; done < n;) {
    const int count = std::min(kControlBlock, n - done);
    const double coeff =
        glideSeconds_ > 0.0f
            ? std::exp(-double(count) / (double(glideSeconds_) * hostRate_))
            : 0.0;
    currentNote_ = double(targetNote_) + (currentNote_ - double(targetNote_)) * coeff;
    if (std::fabs(currentNote_ - double(targetNote_)) < 1e-4) currentNote_ = double(targetNote_);
    source().setNote(float(currentNote_) + pitchOffset_);

    float* block[2] = {buffer_[0], buffer_[1]};
    const float* inBlock[2] = {in ? in[0] + done : nullptr, in ? in[1] + done : nullptr};
    source().process(in ? inBlock : nullptr, block, p, count);
    if (!blockIsFinite(count)) {
      // Spec §9: a NaN/Inf from an engine resets it and mutes this block; a
      // held note starts again on the next call.
      source().reset();
      retrigger_ = gate_;
      active_ = gate_;
      return;
    }
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
      out[0][done + i] += buffer_[0][i];
      out[1][done + i] += buffer_[1][i];
      peak = std::max(peak, std::max(std::fabs(buffer_[0][i]), std::fabs(buffer_[1][i])));
    }
    done += count;

    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
      if (silentSamples_ >= silenceLimit_) {
        active_ = false;
        return;
      }
    } else {
      silentSamples_ = 0;
    }
  }
}

}  // namespace eg13
