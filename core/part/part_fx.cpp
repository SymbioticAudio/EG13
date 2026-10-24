#include "part/part_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace eg13 {

namespace {

constexpr int kOff = 0;  // PartFxEngine::Off

// Reads the delayed value at pos and stores x there (a line of length 0 passes x through).
inline float delayed(std::vector<float>& line, int pos, float x) {
  if (line.empty()) return x;
  const float out = line[size_t(pos)];
  line[size_t(pos)] = x;
  return out;
}

inline int advance(int pos, size_t length) {
  if (length == 0) return 0;
  return ++pos == int(length) ? 0 : pos;
}

}  // namespace

float PartFx::Ramp::next(float step) {
  if (value < target) {
    value = std::min(target, value + step);
  } else if (value > target) {
    value = std::max(target, value - step);
  }
  return value;
}

PartFx::PartFx(std::vector<SourceSpec> engines) : slot_(std::move(engines)) {
  // Off's own output is muted: the bypass line carries the part mix, whatever the selector.
  slot_.setOutputGain(kOff, 0.0f);
}

void PartFx::prepare(double hostRate, int maxBlock, int voiceLatency, ConverterQuality q) {
  slot_.prepare(hostRate, maxBlock, q);
  voiceLatency_ = std::max(0, voiceLatency);
  step_ = float(1.0 / std::max(1.0, hostRate * kSlotFadeSeconds));  // the slot's fade step
  ticks_.assign(size_t(voiceLatency_ / 16 + 2), Tick{});
  const size_t latency = size_t(slot_.latencySamples());
  for (int c = 0; c < 2; ++c) {
    inputLine_[c].assign(size_t(voiceLatency_), 0.0f);
    bypassLine_[c].assign(latency, 0.0f);
    in_[c].assign(size_t(maxBlock), 0.0f);
    wet_[c].assign(size_t(maxBlock), 0.0f);
    bypass_[c].assign(size_t(maxBlock), 0.0f);
  }
  mixGainLine_.assign(latency, 0.0f);
  bypassGain_.assign(size_t(maxBlock), 0.0f);
  reset();
}

void PartFx::reset() {
  slot_.reset();
  engine_ = slot_.engine();
  previous_ = -1;
  fadeHold_ = 0;
  fadeWeight_ = 1.0f;
  tickOldest_ = 0;
  tickCount_ = 0;
  time_ = 0;
  fresh_ = true;
  retarget_ = false;
  params_ = ParamValues{};
  note_ = 60.0f;
  slot_.setNote(note_);
  for (int c = 0; c < 2; ++c) {
    std::fill(inputLine_[c].begin(), inputLine_[c].end(), 0.0f);
    std::fill(bypassLine_[c].begin(), bypassLine_[c].end(), 0.0f);
  }
  inputLinePos_ = 0;
  bypassLinePos_ = 0;
  idle_ = true;
  setInput(mode_);  // snaps
}

void PartFx::setEngine(int engine) {
  slot_.setEngine(engine);
  followSlot();
}

void PartFx::setInput(PartFxInput mode) {
  mode_ = mode;
  mixGain_.target = mode == PartFxInput::Input ? 0.0f : 1.0f;
  inputGain_.target = mode == PartFxInput::PartMix ? 0.0f : 1.0f;
  if (idle_) {  // nothing has played since reset(): nothing to ramp from
    mixGain_.value = mixGain_.target;
    inputGain_.value = inputGain_.target;
    std::fill(mixGainLine_.begin(), mixGainLine_.end(), mixGain_.value);
  }
}

void PartFx::followSlot() {
  if (slot_.engine() != engine_) {
    if (slot_.fading()) {  // the slot's crossfade: the slot latency's hold, then kSlotFadeSeconds
      previous_ = engine_;
      fadeHold_ = slot_.latencySamples();
      fadeWeight_ = 0.0f;
    } else {  // switched at once (nothing playing, or a restart)
      previous_ = -1;
      fadeWeight_ = 1.0f;
    }
    engine_ = slot_.engine();
    retarget_ = true;  // the next tick's P's belong to the new engine (see beginTick)
  } else if (!slot_.fading()) {
    previous_ = -1;
    fadeWeight_ = 1.0f;
  }
}

void PartFx::beginTick(const ParamValues& p, float note, bool trigger) {
  if (fresh_) {  // what is in flight was made before any tick: give it this tick's P's and note
    fresh_ = false;
    params_ = p;
    if (note != note_) {
      note_ = note;
      slot_.setNote(note_);
    }
  }
  const int size = int(ticks_.size());
  if (size == 0) return;  // not prepared
  if (retarget_) {
    // A new engine started: the P's waiting in the ring were made for the engine it replaced
    // (they stay queued for the voices' latency). Give the new engine this tick's at once and
    // overwrite every waiting tick's; notes and triggers keep their time. Bounded: the ring.
    retarget_ = false;
    params_ = p;
    for (int i = 0; i < tickCount_; ++i) ticks_[size_t((tickOldest_ + i) % size)].p = p;
  }
  if (tickCount_ > 0) {
    Tick& newest = ticks_[size_t((tickOldest_ + tickCount_ - 1) % size)];
    if (newest.start == time_) {  // a second tick at the same time replaces the first
      newest.p = p;
      newest.note = note;
      newest.trigger = newest.trigger || trigger;
      return;
    }
  }
  if (tickCount_ == size) {  // full (ticks without audio in between): the oldest acts now
    apply(ticks_[size_t(tickOldest_)]);
    tickOldest_ = (tickOldest_ + 1) % size;
    --tickCount_;
  }
  Tick& t = ticks_[size_t((tickOldest_ + tickCount_) % size)];
  t.start = time_;
  t.p = p;
  t.note = note;
  t.trigger = trigger;
  ++tickCount_;
}

void PartFx::apply(const Tick& tick) {
  params_ = tick.p;
  if (tick.note != note_) {
    note_ = tick.note;
    slot_.setNote(note_);
  }
  if (tick.trigger) {
    slot_.noteOn(note_, 1.0f);
    slot_.noteOff();  // G26: a type change while the gate stays high would retrigger the new engine
  }
}

int64_t PartFx::nextDue() const {
  if (tickCount_ == 0) return std::numeric_limits<int64_t>::max();
  return ticks_[size_t(tickOldest_)].start + voiceLatency_;
}

void PartFx::applyDue() {
  const int size = int(ticks_.size());
  while (tickCount_ > 0 && nextDue() <= time_) {
    apply(ticks_[size_t(tickOldest_)]);
    tickOldest_ = (tickOldest_ + 1) % size;
    --tickCount_;
  }
}

float PartFx::bypassOf(int engine, float effectMix) const {
  // Off passes the whole mix; an effect, the share of the mix it did not receive.
  return engine == kOff ? 1.0f : 1.0f - effectMix;
}

void PartFx::process(const float* const* mix, const float* const* input, float* const* out, int n) {
  idle_ = false;
  for (int done = 0; done < n;) {
    applyDue();
    int count = n - done;
    const int64_t due = nextDue();
    if (due != std::numeric_limits<int64_t>::max()) count = int(std::min<int64_t>(count, due - time_));
    render(mix, input, out, done, count);
    time_ += count;
    done += count;
  }
}

void PartFx::render(const float* const* mix, const float* const* input, float* const* out, int offset,
                    int n) {
  // The effect's input, and the bypass: the mix and the effect's mix gain, latencySamples() ago.
  for (int i = 0; i < n; ++i) {
    const float gm = mixGain_.next(step_);
    const float gi = inputGain_.next(step_);
    for (int c = 0; c < 2; ++c) {
      const float m = mix[c][offset + i];
      const float x = delayed(inputLine_[c], inputLinePos_, input != nullptr ? input[c][offset + i] : 0.0f);
      in_[c][size_t(i)] = gm * m + gi * x;
      bypass_[c][size_t(i)] = delayed(bypassLine_[c], bypassLinePos_, m);
    }
    bypassGain_[size_t(i)] = delayed(mixGainLine_, bypassLinePos_, gm);
    inputLinePos_ = advance(inputLinePos_, inputLine_[0].size());
    bypassLinePos_ = advance(bypassLinePos_, bypassLine_[0].size());
  }
  const float* in[2] = {in_[0].data(), in_[1].data()};
  float* wet[2] = {wet_[0].data(), wet_[1].data()};
  slot_.process(in, nullptr, wet, params_, n);

  bool finite = true;
  for (int i = 0; i < n; ++i) finite = finite && std::isfinite(wet_[0][size_t(i)]) && std::isfinite(wet_[1][size_t(i)]);
  if (!finite) {
    // Spec §9 (ruling 25): restart the playing engine and output silence for this slice.
    slot_.restart();
    slot_.setNote(note_);
    followSlot();
    for (int i = 0; i < n; ++i) out[0][offset + i] = out[1][offset + i] = 0.0f;
    return;
  }
  for (int i = 0; i < n; ++i) {
    float b = bypassOf(engine_, bypassGain_[size_t(i)]);
    if (previous_ >= 0) {  // the slot's crossfade, step for step
      if (fadeHold_ > 0) {
        --fadeHold_;
      } else {
        fadeWeight_ = std::min(1.0f, fadeWeight_ + step_);
      }
      const float old = bypassOf(previous_, bypassGain_[size_t(i)]);
      b = old + fadeWeight_ * (b - old);
    }
    out[0][offset + i] = wet_[0][size_t(i)] + b * bypass_[0][size_t(i)];
    out[1][offset + i] = wet_[1][size_t(i)] + b * bypass_[1][size_t(i)];
  }
  followSlot();  // the fade ended, or a waiting request started its own
}

}  // namespace eg13
