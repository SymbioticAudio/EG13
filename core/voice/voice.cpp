#include "voice/voice.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <utility>

#include "engine/engine_registry.h"
#include "voice/processor_factory.h"

namespace eg13 {

namespace {
constexpr float kSilenceThreshold = 3.1623e-5f;  // -90 dBFS
constexpr double kSilenceSeconds = 0.05;

std::vector<SourceSpec> only(SourceSpec source) {
  std::vector<SourceSpec> v;
  v.push_back(std::move(source));
  return v;
}

bool finite(const float* a, int count) {
  for (int i = 0; i < count; ++i) {
    if (!std::isfinite(a[i])) return false;
  }
  return true;
}

// (L + R) / 2 of stereo[.][offset..offset + count) into mono; nullptr when stereo is.
const float* monoOf(const float* const* stereo, int offset, int count, float* mono) {
  if (stereo == nullptr) return nullptr;
  for (int i = 0; i < count; ++i) mono[i] = 0.5f * (stereo[0][offset + i] + stereo[1][offset + i]);
  return mono;
}

// One step of a delay line: returns what went in line.size() steps ago (x itself when empty).
template <typename T>
T delayed(std::vector<T>& line, int& pos, const T& x) {
  if (line.empty()) return x;
  const T y = line[size_t(pos)];
  line[size_t(pos)] = x;
  if (++pos == int(line.size())) pos = 0;
  return y;
}
}  // namespace

Voice::Voice(SourceSpec source) : Voice(only(std::move(source))) {}

Voice::Voice(std::vector<SourceSpec> sources) : Voice(std::move(sources), makeOffOnly(), makeOffOnly()) {}

Voice::Voice(std::vector<SourceSpec> sources, std::vector<SourceSpec> filters, std::vector<SourceSpec> shapers)
    : filter_(std::move(filters)), shaper_(std::move(shapers)) {
  for (auto& s : sources) {
    sources_.push_back(std::make_unique<NativeRateWrapper>(std::move(s.engine), s.nativeRate, s.nativeBlock));
    sources_.back()->setAuxEnabled(true);  // Source AUX, for the Ring Mod Carrier and Modulator (spec §4)
  }
  assert(!sources_.empty() && "a voice needs at least one source engine");
}

void Voice::prepare(double hostRate, int maxBlock, Alignment alignment) {
  hostRate_ = hostRate;
  silenceLimit_ = int(hostRate * kSilenceSeconds);
  ampStep_ = float(1.0 / std::max(1.0, hostRate * kAmpRampSeconds));
  auto path = [alignment](const NativeRateWrapper& s) {
    return alignment == Alignment::Input && s.info().usesInput ? s.inputLatencySamples()
                                                               : s.outputLatencySamples();
  };
  sourceLatency_ = 0;
  for (auto& s : sources_) {
    s->prepare(hostRate, maxBlock);
    sourceLatency_ = std::max(sourceLatency_, path(*s));
  }
  for (auto& s : sources_) s->setExtraDelay(sourceLatency_ - path(*s));
  // The Filter and Shaper see one control block per call.
  filter_.prepare(hostRate, kControlBlock);
  shaper_.prepare(hostRate, kControlBlock);
  // Source AUX goes to the Shaper straight from the Source: it is aligned with the voice only
  // while no Filter adds latency.
  assert(filter_.latencySamples() == 0 && "delay Source AUX by the Filter latency");
  latency_ = sourceLatency_ + filter_.latencySamples() + shaper_.latencySamples();
  // Modulation waits for the audio it acts on: the Filter's input is the Source's output, late
  // by the Source path's latency; the Amp acts on the Shaper's output, late by the whole voice's.
  filterDelay_ = sourceLatency_;
  shaperDelay_ = sourceLatency_ + filter_.latencySamples();
  ampLine_.assign(size_t(latency_), AmpState{});
  sourceGainLine_.assign(size_t(sourceLatency_), 1.0f);
  ticks_.assign(size_t(shaperDelay_ / kControlBlock + 4), TickParams{});
  reset();
}

void Voice::reset() {
  for (auto& s : sources_) s->reset();
  filter_.reset();
  shaper_.reset();
  gate_ = false;
  retrigger_ = 0;
  active_ = false;
  silentSamples_ = 0;
  outSilent_ = 0;
  control_ = VoiceControl{};
  tickLength_ = kControlBlock;
  tickPos_ = 0;
  fresh_ = true;
  amp_ = ampFrom_ = 0.0f;
  pan_ = panFrom_ = 0.0f;
  gain_ = gainFrom_ = 1.0f;
  sourceGain_ = sourceGainFrom_ = 1.0f;
  time_ = 0;
  snapDelays(0.0f, 1.0f, 0.0f, 1.0f, SlotParams{});
}

void Voice::snapDelays(float amp, float gain, float pan, float sourceGain, const SlotParams& params) {
  std::fill(ampLine_.begin(), ampLine_.end(), AmpState{amp, gain, pan});
  ampLinePos_ = 0;
  std::fill(sourceGainLine_.begin(), sourceGainLine_.end(), sourceGain);
  sourceGainLinePos_ = 0;
  appliedAmp_ = amp;
  if (ticks_.empty()) return;
  tickNewest_ = 0;
  tickCount_ = 1;
  ticks_[0].start = time_;
  ticks_[0].params = params;
}

void Voice::restart(unsigned stages) {
  if (stages & kSourceStage) source().reset();
  if (stages & kFilterStage) filter_.restart();
  if (stages & kShaperStage) shaper_.restart();
}

void Voice::noteOn(unsigned stages) {
  const float note = float(currentNote_) + pitchOffset_;
  if (stages & kSourceStage) source().noteOn(note, velocity_);
  if (stages & kFilterStage) filter_.noteOn(note, velocity_);
  if (stages & kShaperStage) shaper_.noteOn(note, velocity_);
}

void Voice::setEngine(int engine) {
  engine = std::clamp(engine, 0, int(sources_.size()) - 1);
  if (engine == current_) return;
  // Hard cut: the old engine is left as is (it is reset when selected again or by
  // reset()), and a held note restarts after latencySamples() of silence.
  // The Source gain (trim x chord scaling) snaps to the new engine's trim, in flight too, so the
  // new Source never plays a moment at the old one's trim (the Part's next tick confirms it).
  const float trim = sourceTrim(engine) / sourceTrim(current_);
  sourceGain_ *= trim;
  sourceGainFrom_ *= trim;
  control_.sourceGain *= trim;
  std::fill(sourceGainLine_.begin(), sourceGainLine_.end(), sourceGain_);
  current_ = engine;
  source().reset();
  retrigger_ &= ~kSourceStage;
  silentSamples_ = 0;
  if (gate_) {
    noteOn(kSourceStage);
  } else {
    active_ = false;
  }
}

void Voice::setFilterEngine(int engine) { filter_.setEngine(engine); }

void Voice::setShaperEngine(int engine) { shaper_.setEngine(engine); }

void Voice::trigger(int key, float velocity) {
  targetNote_ = float(key);
  velocity_ = velocity;
  retrigger_ = 0;
  if (!active_) {
    restart(kAllStages);  // drop stale converter samples and filter state from the previous note
    currentNote_ = double(targetNote_);
    amp_ = ampFrom_ = 0.0f;  // the Amp opens from silence
    fresh_ = true;  // the first tick snaps pan and gains
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = double(targetNote_);
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
  outSilent_ = 0;
  noteOn(kAllStages);
}

void Voice::glideTo(int key) {
  targetNote_ = float(key);
  if (glideSeconds_ <= 0.0f) currentNote_ = double(targetNote_);
}

unsigned Voice::failedStages(int count) const {
  unsigned failed = 0;
  if (!finite(source_[0], count) || !finite(source_[1], count) || !finite(source_[2], count)) {
    failed |= kSourceStage;
  }
  // Checked on its own: the Ring Mod reads a NaN input as 0, so a broken Filter can hide
  // behind a finite Shaper output.
  if (!finite(filtered_[0], count) || !finite(filtered_[1], count)) failed |= kFilterStage;
  if (!finite(buffer_[0], count) || !finite(buffer_[1], count)) failed |= kShaperStage;
  return failed;
}

void Voice::release() {
  gate_ = false;
  retrigger_ = 0;  // a pending NaN re-gate must not outlive the key release
  source().noteOff();
  filter_.noteOff();
  shaper_.noteOff();
}

SideSignals Voice::sideSignals(const VoiceInputs& in, int offset, int count) {
  SideSignals sides;
  sides.sourceAux = source_[2];
  sides.input = in.inputSide ? in.inputSide + offset : nullptr;
  sides.otherPart = monoOf(in.other, offset, count, side_);
  return sides;
}

void Voice::process(const float* const* in, float* const* out, const ParamValues& p, int n) {
  SlotParams params;
  params.source = p;
  process(VoiceInputs{in, nullptr}, out, params, n);
}

void Voice::process(const VoiceInputs& in, float* const* out, const SlotParams& p, int n) {
  run(in, out, p, nullptr, n);
}

void Voice::beginTick(const VoiceControl& control, int length) {
  control_ = control;
  tickLength_ = std::max(1, length);
  tickPos_ = 0;
  if (fresh_) {  // a note's first tick from silence: no glide from the previous note's values
    pan_ = control.pan;
    gain_ = control.gain;
    sourceGain_ = control.sourceGain;
    fresh_ = false;
    // What is still in flight is silence from before the note: give it this note's values.
    snapDelays(amp_, gain_, pan_, sourceGain_, control.params);
  } else if (!ticks_.empty()) {
    if (tickCount_ > 0 && ticks_[size_t(tickNewest_)].start == time_) {
      ticks_[size_t(tickNewest_)].params = control.params;  // a second beginTick at the same time
    } else {
      tickNewest_ = (tickNewest_ + 1) % int(ticks_.size());
      tickCount_ = std::min(tickCount_ + 1, int(ticks_.size()));
      ticks_[size_t(tickNewest_)].start = time_;
      ticks_[size_t(tickNewest_)].params = control.params;
    }
  }
  ampFrom_ = amp_;
  panFrom_ = pan_;
  gainFrom_ = gain_;
  sourceGainFrom_ = sourceGain_;
}

void Voice::render(const VoiceInputs& in, float* const* out, int n) { run(in, out, control_.params, &control_, n); }

float Voice::tickPosition(int sample) const {
  return std::min(1.0f, float(tickPos_ + sample) / float(tickLength_));
}

int Voice::delayedParams(int delay, const SlotParams** params) const {
  if (delay == 0 || ticks_.empty()) {
    *params = &control_.params;
    return INT_MAX;
  }
  const int64_t t = time_ - delay;
  const int size = int(ticks_.size());
  // The newest stored tick that had begun by t; the oldest stored one if none had.
  int age = 0;
  while (age + 1 < tickCount_ && ticks_[size_t((tickNewest_ - age + size) % size)].start > t) ++age;
  *params = &ticks_[size_t((tickNewest_ - age + size) % size)].params;
  if (age == 0) return INT_MAX;
  const int64_t next = ticks_[size_t((tickNewest_ - age + 1 + size) % size)].start;
  return int(std::max<int64_t>(1, std::min<int64_t>(INT_MAX, next - t)));
}

void Voice::stepAmp(const VoiceControl& c, float t) {
  // The Amp (spec §5.2): the routes' level, or the gate with 5 ms ramps; then the pan (the part's
  // balance law) and the Voice Gain, the Amp's gain.
  if (c.ampRouted) {
    amp_ = ampFrom_ + (clamp01(c.amp) - ampFrom_) * t;
  } else {
    const float target = gate_ ? 1.0f : 0.0f;
    amp_ = target > amp_ ? std::min(target, amp_ + ampStep_) : std::max(target, amp_ - ampStep_);
  }
  pan_ = panFrom_ + (c.pan - panFrom_) * t;
  gain_ = gainFrom_ + (c.gain - gainFrom_) * t;
}

void Voice::run(const VoiceInputs& in, float* const* out, const SlotParams& p, const VoiceControl* c, int n) {
  if (!active_) return;
  if (retrigger_ != 0 && gate_) {  // the held note, to the stages a NaN reset (restart)
    noteOn(retrigger_);
    retrigger_ = 0;
  }
  for (int done = 0; done < n;) {
    int count = std::min(kControlBlock, n - done);
    // The Filter and Shaper take the parameters of the tick their input was made in; a block ends
    // where that changes, so the change lands on its sample.
    const SlotParams* filterParams = &p;
    const SlotParams* shaperParams = &p;
    if (c != nullptr) {
      count = std::min(count, delayedParams(filterDelay_, &filterParams));
      count = std::min(count, delayedParams(shaperDelay_, &shaperParams));
    }
    const double coeff =
        glideSeconds_ > 0.0f
            ? std::exp(-double(count) / (double(glideSeconds_) * hostRate_))
            : 0.0;
    currentNote_ = double(targetNote_) + (currentNote_ - double(targetNote_)) * coeff;
    if (std::fabs(currentNote_ - double(targetNote_)) < 1e-4) currentNote_ = double(targetNote_);
    const float note = float(currentNote_) + pitchOffset_ + (c != nullptr ? c->pitch : 0.0f);
    source().setNote(note);
    filter_.setNote(note);
    shaper_.setNote(note);

    float* src[3] = {source_[0], source_[1], source_[2]};
    const float* inBlock[2] = {in.input ? in.input[0] + done : nullptr, in.input ? in.input[1] + done : nullptr};
    source().process(in.input ? inBlock : nullptr, src, p.source, count);
    if (!source().info().hasAux) {
      // Sources without an AUX output give their main output instead (spec §4).
      for (int i = 0; i < count; ++i) source_[2][i] = 0.5f * (source_[0][i] + source_[1][i]);
    }
    if (c != nullptr) {  // the Source's trim and chord scaling, before the Filter (M4 gain staging)
      for (int i = 0; i < count; ++i) {
        const float now = sourceGainFrom_ + (c->sourceGain - sourceGainFrom_) * tickPosition(i + 1);
        const float g = delayed(sourceGainLine_, sourceGainLinePos_, now);
        source_[0][i] *= g;
        source_[1][i] *= g;
        source_[2][i] *= g;
      }
    }
    const float* filterIn[2] = {source_[0], source_[1]};
    float* filterOut[2] = {filtered_[0], filtered_[1]};
    filter_.process(filterIn, nullptr, filterOut, filterParams->filter, count);
    const float* shaperIn[2] = {filtered_[0], filtered_[1]};
    float* shaperOut[2] = {buffer_[0], buffer_[1]};
    const SideSignals sides = sideSignals(in, done, count);
    shaper_.process(shaperIn, &sides, shaperOut, shaperParams->shaper, count);
    if (c != nullptr) sourceGain_ = sourceGainFrom_ + (c->sourceGain - sourceGainFrom_) * tickPosition(count);

    if (const unsigned failed = failedStages(count)) {
      // Spec §9: a NaN/Inf from an engine resets that engine (every stage whose output is not
      // finite) and mutes the voice for this block; a held note starts again on the next call.
      restart(failed);
      retrigger_ = gate_ ? failed : 0;
      active_ = gate_;
      if (c != nullptr) {  // the Amp's timeline moves on through the muted block
        for (int i = 0; i < count; ++i) {
          stepAmp(*c, tickPosition(i + 1));
          appliedAmp_ = delayed(ampLine_, ampLinePos_, AmpState{amp_, gain_, pan_}).amp;
        }
        tickPos_ += count;
        time_ += count;
      }
      return;
    }
    float peak = 0.0f;
    float outPeak = 0.0f;
    for (int i = 0; i < count; ++i) {
      float l = buffer_[0][i];
      float r = buffer_[1][i];
      if (c != nullptr) {
        stepAmp(*c, tickPosition(i + 1));
        // Applied latencySamples() later, when the audio made with these controls arrives.
        const AmpState a = delayed(ampLine_, ampLinePos_, AmpState{amp_, gain_, pan_});
        appliedAmp_ = a.amp;
        const float g = a.amp * a.gain;
        l *= g * std::min(1.0f, 1.0f - a.pan);
        r *= g * std::min(1.0f, 1.0f + a.pan);
        outPeak = std::max(outPeak, std::max(std::fabs(l), std::fabs(r)));
      }
      out[0][done + i] += l;
      out[1][done + i] += r;
      peak = std::max(peak, std::max(std::fabs(filtered_[0][i]), std::fabs(filtered_[1][i])));
    }
    if (c != nullptr) {
      tickPos_ += count;
      time_ += count;
    }
    done += count;

    // Silence is measured at the Shaper input: the Ring Mod's internal oscillator keeps the
    // Shaper's output alive after the note has died. The Shaper latency lets its output flush.
    // With the Amp (M4), a silent output frees the voice too (spec §5.2).
    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
    } else {
      silentSamples_ = 0;
    }
    if (c != nullptr && !gate_ && outPeak < kSilenceThreshold) {
      outSilent_ += count;
    } else {
      outSilent_ = 0;
    }
    if (silentSamples_ >= silenceLimit_ + shaper_.latencySamples() || (c != nullptr && outSilent_ >= silenceLimit_)) {
      active_ = false;
      return;
    }
  }
}

}  // namespace eg13
