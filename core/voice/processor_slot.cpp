#include "voice/processor_slot.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace eg13 {

namespace {

const float* sideSignal(const SideSignals& sides, SideSource source) {
  switch (source) {
    case SideSource::SourceAux: return sides.sourceAux;
    case SideSource::Input: return sides.input;
    case SideSource::OtherPart: return sides.otherPart;
    case SideSource::Voice:  // the engine uses its own input, channel by channel
    case SideSource::None:   // the engine makes the signal (Warps' internal oscillator)
      break;
  }
  return nullptr;
}

}  // namespace

ProcessorSlot::ProcessorSlot(std::vector<SourceSpec> engines) {
  for (auto& e : engines) {
    engines_.push_back(std::make_unique<NativeRateWrapper>(std::move(e.engine), e.nativeRate, e.nativeBlock));
  }
  assert(!engines_.empty() && "a slot needs at least one engine");
}

void ProcessorSlot::prepare(double hostRate, int maxBlock) {
  latency_ = 0;
  for (auto& e : engines_) {
    e->prepare(hostRate, maxBlock);
    latency_ = std::max(latency_, e->inputPathLatencySamples());
  }
  for (auto& e : engines_) e->setExtraDelay(latency_ - e->inputPathLatencySamples());
  detector_.prepare(hostRate);
  fadeStep_ = float(1.0 / std::max(1.0, hostRate * kSlotFadeSeconds));
  for (auto& buffer : fade_) buffer.assign(size_t(maxBlock), 0.0f);
  reset();
}

void ProcessorSlot::reset() {
  if (pending_ >= 0) current_ = pending_;
  for (auto& e : engines_) e->reset();
  detector_.reset();
  previous_ = -1;
  pending_ = -1;
  fadeHold_ = 0;
  fadeWeight_ = 1.0f;
  gate_ = false;
  last_ = ParamValues{};
}

void ProcessorSlot::restart() {
  if (pending_ >= 0) current_ = pending_;
  at(current_).reset();
  detector_.reset();
  previous_ = -1;
  pending_ = -1;
  fadeHold_ = 0;
  fadeWeight_ = 1.0f;
}

void ProcessorSlot::setEngine(int engine) {
  engine = std::clamp(engine, 0, engineCount() - 1);
  if (previous_ >= 0) {  // a fade is under way: wait for it to end
    if (engine == current_) {
      pending_ = -1;
    } else {
      if (pending_ < 0) held_ = last_;
      pending_ = engine;
    }
    return;
  }
  if (engine != current_) startFade(engine, last_);
}

void ProcessorSlot::startFade(int engine, const ParamValues& outgoing) {
  previous_ = current_;
  fadeParams_ = outgoing;
  current_ = engine;
  NativeRateWrapper& next = at(current_);
  next.reset();
  next.setNote(note_);
  if (gate_) next.noteOn(note_, velocity_);
  fadeHold_ = latency_;
  fadeWeight_ = 0.0f;
}

void ProcessorSlot::noteOn(float note, float velocity) {
  note_ = note;
  velocity_ = velocity;
  gate_ = true;
  at(current_).noteOn(note, velocity);
  if (previous_ >= 0) at(previous_).noteOn(note, velocity);
}

void ProcessorSlot::noteOff() {
  gate_ = false;
  at(current_).noteOff();
  if (previous_ >= 0) at(previous_).noteOff();
}

void ProcessorSlot::setNote(float note) {
  note_ = note;
  at(current_).setNote(note);
  if (previous_ >= 0) at(previous_).setNote(note);
}

void ProcessorSlot::render(int engine, const float* const* in, const SideSignals* sides, float* const* out,
                           const ParamValues& p, bool stereo, int n) {
  NativeRateWrapper& e = at(engine);
  const EngineInfo& info = e.info();
  const float* channels[NativeRateWrapper::kMaxChannels] = {in[0], in[1], nullptr, nullptr};
  if (sides != nullptr && info.sideSource != nullptr) {
    // Each engine's own selectors, with its own parameters (spec §2.3).
    for (int k = 0; k < std::min(info.sideInputs, 2); ++k) channels[2 + k] = sideSignal(*sides, info.sideSource(k, p));
  }
  e.setStereo(stereo);
  e.process(channels, out, p, n);
}

void ProcessorSlot::process(const float* const* in, const SideSignals* sides, float* const* out,
                            const ParamValues& p, int n) {
  const bool stereo = detector_.update(in[0], in[1], n);
  const ParamValues& own = pending_ >= 0 ? held_ : p;
  render(current_, in, sides, out, own, stereo, n);
  last_ = own;
  if (previous_ < 0) return;
  float* old[2] = {fade_[0].data(), fade_[1].data()};
  render(previous_, in, sides, old, fadeParams_, stereo, n);
  for (int i = 0; i < n; ++i) {
    if (fadeHold_ > 0) {
      --fadeHold_;
    } else {
      fadeWeight_ = std::min(1.0f, fadeWeight_ + fadeStep_);
    }
    out[0][i] = old[0][i] + fadeWeight_ * (out[0][i] - old[0][i]);
    out[1][i] = old[1][i] + fadeWeight_ * (out[1][i] - old[1][i]);
  }
  if (fadeWeight_ < 1.0f) return;
  previous_ = -1;
  if (pending_ >= 0) {  // the request that waited for this fade
    const int next = pending_;
    pending_ = -1;
    startFade(next, held_);
  }
}

}  // namespace eg13
