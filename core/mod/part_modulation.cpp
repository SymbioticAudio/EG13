#include "mod/part_modulation.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>

namespace eg13 {

namespace {

const float kNoOutputs[kMaxModOutputs] = {};

// murmur3's 32-bit finalizer: every input bit moves about half the output bits.
uint32_t fmix32(uint32_t h) {
  h ^= h >> 16;
  h *= 0x85ebca6bu;
  h ^= h >> 13;
  h *= 0xc2b2ae35u;
  h ^= h >> 16;
  return h;
}

// Marbles' generator is a bare LCG, so neighbouring seeds would give related sequences: each
// (base, slot, instance) is hashed instead. instance: the voice, or kVoices for the part's.
uint32_t instanceSeed(uint32_t base, int slot, int instance) {
  return fmix32(fmix32(base) + uint32_t(slot * (PartModulation::kVoices + 1) + instance));
}

}  // namespace

PartModulation::PartModulation(ModEngineFactory factory) {
  voiceSlots_.reserve(size_t(kVoices * kNumModSlots));
  for (int i = 0; i < kVoices * kNumModSlots; ++i) voiceSlots_.emplace_back(factory());
  partSlots_.reserve(size_t(kNumModSlots));
  for (int s = 0; s < kNumModSlots; ++s) partSlots_.emplace_back(factory());
}

void PartModulation::setSeedBase(uint32_t base) {
  for (int s = 0; s < kNumModSlots; ++s) {
    for (int v = 0; v < kVoices; ++v) voiceSlot(v, s).setSeed(instanceSeed(base, s, v));
    partSlots_[size_t(s)].setSeed(instanceSeed(base, s, kVoices));
  }
}

void PartModulation::prepare(double hostRate) {
  for (auto& slot : voiceSlots_) slot.prepare(hostRate);
  for (auto& slot : partSlots_) slot.prepare(hostRate);
  reset();
}

void PartModulation::reset() {
  for (auto& slot : voiceSlots_) slot.reset();
  for (auto& slot : partSlots_) slot.reset();
  for (auto& outputs : partPrevious_) std::fill(std::begin(outputs), std::end(outputs), 0.0f);
  std::fill(std::begin(partSource_), std::end(partSource_), 0.0f);
}

void PartModulation::configure(int slot, int engine, bool perPart) {
  if (engine != engine_[slot]) {
    for (int v = 0; v < kVoices; ++v) voiceSlot(v, slot).setEngine(engine);
    partSlots_[size_t(slot)].setEngine(engine);
    engine_[slot] = partSlots_[size_t(slot)].engine();
  }
  if (perPart != perPart_[slot]) {
    if (perPart) {
      partSlots_[size_t(slot)].restart();
    } else {
      for (int v = 0; v < kVoices; ++v) voiceSlot(v, slot).restart();
    }
    perPart_[slot] = perPart;
  }
}

void PartModulation::startVoice(int v) {
  assert(v >= 0 && v < kVoices);
  for (int s = 0; s < kNumModSlots; ++s) voiceSlot(v, s).restart();
}

const float* PartModulation::outputs(int slot, int voice) const {
  return voice < 0 ? partSlots_[size_t(slot)].outputs() : voiceSlot(voice, slot).outputs();
}

void PartModulation::fillFixed(float* source, const ModPartInputs& part, const ModVoiceInputs* voice) const {
  std::fill(source, source + kNumSources, 0.0f);
  source[kSrcModWheel] = part.modWheel;
  source[kSrcPitchBend] = part.pitchBend;
  source[kSrcChannelPressure] = part.channelPressure;
  for (int i = 0; i < 4; ++i) source[kSrcCcA + i] = part.cc[i];
  source[kSrcTransportPhase] = part.transportPhase;
  float slide = part.slide;
  if (voice != nullptr) {
    source[kSrcVelocity] = voice->velocity;
    source[kSrcReleaseVelocity] = voice->releaseVelocity;
    source[kSrcNote] = (voice->note - 60.0f) / 120.0f;  // provisional ruling 17
    source[kSrcGate] = voice->gate ? 1.0f : 0.0f;
    source[kSrcRandom] = voice->random;
    source[kSrcPressure] = voice->pressure;
    slide = std::max(slide, voice->slide);
    source[kSrcNoteBend] =
        part.noteBendRange > 0.0f ? std::clamp(voice->bend / part.noteBendRange, -1.0f, 1.0f) : 0.0f;
  }
  source[kSrcSlide] = slide;
}

void PartModulation::fillMods(float* source, int voice, bool partCurrent) const {
  for (int s = 0; s < kNumModSlots; ++s) {
    const float* outs = perPart_[s] ? (partCurrent ? partSlots_[size_t(s)].outputs() : partPrevious_[s])
                        : voice >= 0 ? voiceSlot(voice, s).outputs()
                                     : kNoOutputs;
    std::copy(outs, outs + kMaxModOutputs, source + modOutputSource(s, 0));
  }
}

namespace {

ModContext contextFor(const ModPartInputs& part, bool gate, bool retrigger) {
  ModContext ctx;
  ctx.gate = gate;
  ctx.retrigger = retrigger;
  ctx.beat = part.beat;
  ctx.beatsPerSample = part.beatsPerSample;
  ctx.beatJump = part.beatJump;
  return ctx;
}

ParamValues modulatedSlot(const ParamValues& base, const RouteSums& sums, int slot) {
  ParamValues p = base;
  for (int i = 0; i < kModParamCount; ++i) p.p[i] = modulated(base.p[i], sums, modParamDestination(slot, i));
  return p;
}

}  // namespace

void PartModulation::processPart(const ModParams& params, const ModPartInputs& part, const ModVoiceInputs* voices,
                                 int n) {
  assert(part.lastVoice >= -1 && part.lastVoice < kVoices);
  const ModVoiceInputs* last = part.lastVoice >= 0 && voices != nullptr ? &voices[part.lastVoice] : nullptr;
  fillFixed(partSource_, part, last);
  for (int s = 0; s < kNumModSlots; ++s) {
    const float* outs = partSlots_[size_t(s)].outputs();
    std::copy(outs, outs + kMaxModOutputs, partPrevious_[s]);
  }
  fillMods(partSource_, part.lastVoice, false);
  RouteSums sums;
  sumRoutes(params.routes, partSource_, DestinationKind::ModParam, &sums);
  const ModContext ctx = contextFor(part, part.gate, part.retrigger);
  for (int s = 0; s < kNumModSlots; ++s) {
    if (!perPart_[s]) continue;
    partSlots_[size_t(s)].process(ctx, modulatedSlot(params.slot[s], sums, s), params.shapes[s], n);
  }
  fillMods(partSource_, part.lastVoice, true);
}

void PartModulation::processVoice(int v, const ModParams& params, const ModPartInputs& part,
                                  const ModVoiceInputs& voice, const SlotParams& base, VoiceControl* control, int n) {
  assert(v >= 0 && v < kVoices);
  float source[kNumSources];
  fillFixed(source, part, &voice);
  fillMods(source, v, false);
  RouteSums sums;
  sumRoutes(params.routes, source, DestinationKind::ModParam, &sums);
  const ModContext ctx = contextFor(part, voice.gate, voice.retrigger);
  for (int s = 0; s < kNumModSlots; ++s) {
    if (perPart_[s]) continue;
    voiceSlot(v, s).process(ctx, modulatedSlot(params.slot[s], sums, s), params.shapes[s], n);
  }
  fillMods(source, v, true);
  sumRoutes(params.routes, source, DestinationKind::Voice, &sums);

  control->params = base;
  for (int i = 0; i < kMaxSlotParams; ++i) {
    control->params.source.p[i] = modulated(base.source.p[i], sums, uint16_t(kDestSourceP + i));
    control->params.filter.p[i] = modulated(base.filter.p[i], sums, uint16_t(kDestFilterP + i));
    control->params.shaper.p[i] = modulated(base.shaper.p[i], sums, uint16_t(kDestShaperP + i));
  }
  // Pitch: +-100 % is +-24 semitones (provisional ruling 4), the sum limited to +-48.
  control->pitch = std::clamp(sums.value[kDestPitch], -2.0f, 2.0f) * kPitchRangeSemitones + voice.bend;
  control->ampRouted = sums.touched[kDestAmp];
  control->amp = clamp01(sums.value[kDestAmp]);
  control->pan = sums.touched[kDestPan] ? clamp01(0.5f + sums.value[kDestPan]) * 2.0f - 1.0f : 0.0f;
}

float PartModulation::partLevel(const ModParams& params, float base) const {
  RouteSums sums;
  sumRoutes(params.routes, partSource_, DestinationKind::Part, &sums);
  return modulated(base, sums, kDestPartLevel);
}

}  // namespace eg13
