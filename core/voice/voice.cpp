#include "voice/voice.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

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
  } else if (glideSeconds_ <= 0.0f) {
    currentNote_ = double(targetNote_);
  }
  gate_ = true;
  active_ = true;
  silentSamples_ = 0;
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
  if (!active_) return;
  if (retrigger_ != 0 && gate_) {  // the held note, to the stages a NaN reset (restart)
    noteOn(retrigger_);
    retrigger_ = 0;
  }
  for (int done = 0; done < n;) {
    const int count = std::min(kControlBlock, n - done);
    const double coeff =
        glideSeconds_ > 0.0f
            ? std::exp(-double(count) / (double(glideSeconds_) * hostRate_))
            : 0.0;
    currentNote_ = double(targetNote_) + (currentNote_ - double(targetNote_)) * coeff;
    if (std::fabs(currentNote_ - double(targetNote_)) < 1e-4) currentNote_ = double(targetNote_);
    const float note = float(currentNote_) + pitchOffset_;
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
    const float* filterIn[2] = {source_[0], source_[1]};
    float* filterOut[2] = {filtered_[0], filtered_[1]};
    filter_.process(filterIn, nullptr, filterOut, p.filter, count);
    const float* shaperIn[2] = {filtered_[0], filtered_[1]};
    float* shaperOut[2] = {buffer_[0], buffer_[1]};
    const SideSignals sides = sideSignals(in, done, count);
    shaper_.process(shaperIn, &sides, shaperOut, p.shaper, count);

    if (const unsigned failed = failedStages(count)) {
      // Spec §9: a NaN/Inf from an engine resets that engine (every stage whose output is not
      // finite) and mutes the voice for this block; a held note starts again on the next call.
      restart(failed);
      retrigger_ = gate_ ? failed : 0;
      active_ = gate_;
      return;
    }
    float peak = 0.0f;
    for (int i = 0; i < count; ++i) {
      out[0][done + i] += buffer_[0][i];
      out[1][done + i] += buffer_[1][i];
      peak = std::max(peak, std::max(std::fabs(filtered_[0][i]), std::fabs(filtered_[1][i])));
    }
    done += count;

    // Silence is measured at the Shaper input: the Ring Mod's internal oscillator keeps the
    // Shaper's output alive after the note has died. The Shaper latency lets its output flush.
    if (!gate_ && peak < kSilenceThreshold) {
      silentSamples_ += count;
      if (silentSamples_ >= silenceLimit_ + shaper_.latencySamples()) {
        active_ = false;
        return;
      }
    } else {
      silentSamples_ = 0;
    }
  }
}

}  // namespace eg13
