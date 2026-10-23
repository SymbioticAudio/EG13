#include "engine/native_rate_wrapper.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <tuple>
#include <utility>

#include "CDSPResampler.h"

namespace eg13 {

// Live: chosen by `eg13_tests "[.converter]"` (tests/test_converter_quality.cpp; M5 plan rulings 1-3)
// over r8brain at 96 / 108 / 120 / 136 / 150 / 180.15 dB, linear and minimum phase, transition
// band 30 %, input prime 2 or 1 native blocks. Measured 2026-10-23 (worst over hosts 44.1 / 48 /
// 96 kHz and natives 24 / 32 / 47.87 / 48 / 96 kHz):
//   attenuation   stop band   passband dev.   Dry + wet notch   worst 48 kHz input path (prime 2 / 1)
//    96 dB         92.4 dB     0.35 dB         -1.11 dB          315 / 267
//   108 dB        104.5 dB     0.29 dB         -1.08 dB          311 / 263   <- Live, prime 1
//   120 dB        116.6 dB     0.23 dB         -1.05 dB          474 / 462  (r8brain adds a stage at 24 kHz)
//   136 dB        130.3 dB     0.18 dB         -1.03 dB          466 / 454
//   150 dB        144.1 dB     0.15 dB         -1.01 dB          460 / 448
//   180.15 dB     149.7 dB     0.10 dB         -0.99 dB          539 / 491  (149.7: the float measurement floor)
// Floors (owner decisions of 2026-10-23): stop band >= 96 dB; passband within 0.3 dB of 1 kHz up
// to Studio's -0.1 dB edge; Dry + wet notch no deeper than -1.1 dB (Studio -0.99, Live -1.08).
// Minimum phase is 30-60 samples faster at each attenuation but fails ruling 3: impulses land 2-6
// samples off the reported latency and Dry plus the engine notches by 35-57 dB below f_pass.
// The one-block input prime has no under- or overruns at any host rate and block.
ConverterSpec converterSpec(ConverterQuality q) {
  switch (q) {
    case ConverterQuality::Live:
      return {ConverterPhase::Linear, 108.0, NativeRateWrapper::kTransitionBandPercent, 1};
    case ConverterQuality::Studio:
      break;
  }
  // r8b::CDSPResampler24 (CDSPResampler.h:804-807), as shipped before the Latency Mode.
  return {ConverterPhase::Linear, 180.15, NativeRateWrapper::kTransitionBandPercent, 2};
}

namespace {

std::unique_ptr<r8b::CDSPResampler> makeConverter(const ConverterSpec& spec, double from, double to,
                                                  int maxIn) {
  const auto phase = spec.phase == ConverterPhase::Minimum ? r8b::fprMinPhase : r8b::fprLinearPhase;
  return std::make_unique<r8b::CDSPResampler>(from, to, maxIn, spec.transitionBandPercent,
                                              spec.attenuationDb, phase);
}

}  // namespace

NativeRateWrapper::NativeRateWrapper(std::unique_ptr<VoiceEngine> inner, double nativeRate,
                                     int nativeBlock)
    : inner_(std::move(inner)),
      spec_(converterSpec(ConverterQuality::Studio)),
      nativeRate_(nativeRate),
      nativeBlock_(nativeBlock) {}

NativeRateWrapper::~NativeRateWrapper() = default;

void NativeRateWrapper::setQuality(ConverterQuality q) {
  quality_ = q;
  spec_ = converterSpec(q);
  inner_->setConverterQuality(q);
}

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
  fallbacks_ = 0;
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
      resampler_[ch] = makeConverter(spec_, native, hostRate, nativeBlock_);
      fifo_[ch].assign(size_t(resampler_[ch]->getMaxOutLen(nativeBlock_)), 0.0f);
    }
    latency_ = int(std::lround(double(resampler_[0]->getInLenBeforeOutPos(0)) * hostRate / native));
    if (usesInput_) {
      for (int ch = 0; ch < inChannels_; ++ch) {
        inputResampler_[ch] = makeConverter(spec_, hostRate, native, maxBlock);
      }
      const int withheld = inputResampler_[0]->getInLenBeforeOutPos(0);
      inputPrime_ = size_t(spec_.primeBlocks * nativeBlock_ +
                           int(std::ceil(double(withheld) * native / hostRate)) + 1);
      const size_t capacity = inputPrime_ + size_t(inputResampler_[0]->getMaxOutLen(maxBlock)) +
                              size_t(std::ceil(double(latency_) * native / hostRate)) +
                              size_t(2 * nativeBlock_);
      for (int ch = 0; ch < inChannels_; ++ch) inputFifo_[ch].assign(capacity, 0.0f);
      inputLatency_ = latency_ + int(std::lround(double(inputPrime_) * hostRate / native));
      setUpPath(inPath_, spec_, hostRate, native, withheld, maxBlock, 0);
      silence_.assign(size_t(inputResampler_[0]->getMaxOutLen(maxBlock)), 0.0);
    }
    setUpPath(outPath_, spec_, native, hostRate, resampler_[0]->getInLenBeforeOutPos(0), nativeBlock_, nativeBlock_);
    preroll_.assign(size_t(std::max(maxBlock, nativeBlock_)), 0.0);
  }
  // The engine's own latency (a fixed-block FIFO) delays both paths.
  engineLatency_ = int(std::lround(double(inner_->nativeLatency()) * hostRate / native));
  latency_ += engineLatency_;
  if (usesInput_) inputLatency_ += engineLatency_;
  if (bypass_ && wholeBlocks_) {  // the whole-block FIFO: one native block, in host samples
    latency_ += nativeBlock_;
    if (usesInput_) inputLatency_ += nativeBlock_;
  }
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
  restartConversion();
  underruns_ = 0;
  overruns_ = 0;
  for (auto& line : delay_) std::fill(line.begin(), line.end(), 0.0f);
  delayPos_ = 0;
}

void NativeRateWrapper::restartConversion() {
  restartPending_ = false;
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
  for (auto& skip : inSkip_) skip = Skip{};
  for (auto& skip : outSkip_) skip = Skip{};
  inPath_.reset();
  outPath_.reset();
  lastInConverted_ = bypass_ ? 0 : inChannels_;
  lastOutConverted_ = bypass_ ? 0 : outChannels_;
  wholePos_ = 0;
  for (auto& block : native_) std::fill(block.begin(), block.end(), 0.0f);
  for (auto& block : nativeIn_) std::fill(block.begin(), block.end(), 0.0f);
}

void NativeRateWrapper::noteOn(float note, float velocity) { inner_->noteOn(note, velocity); }

void NativeRateWrapper::noteOff() { inner_->noteOff(); }

void NativeRateWrapper::setNote(float note) { inner_->setNote(note); }

const EngineInfo& NativeRateWrapper::info() const { return inner_->info(); }

void NativeRateWrapper::process(const float* const* in, float* const* out, const ParamValues& p,
                                int n) {
  if (bypass_ && wholeBlocks_) {
    processWholeBlocks(in, out, p, n);
  } else if (bypass_) {
    inner_->process(usesInput_ ? in : nullptr, out, p, n);
  } else {
    if (restartPending_) restartConversion();  // after an output fallback (fallBackOutput)
    if (usesInput_) pushInput(in, n);
    for (int i = 0; i < n; ++i) {
      // The converter withholds its first outputs, so a block can yield nothing.
      while (fifoSize_ == 0 && zeroPad_ == 0 && !restartPending_) renderNativeBlock(p);
      if (restartPending_) {  // the rest of this block is the fallback's gap
        for (int ch = 0; ch < outChannels_; ++ch) std::fill(out[ch] + i, out[ch] + n, 0.0f);
        break;
      }
      if (zeroPad_ > 0) {
        for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = 0.0f;
        --zeroPad_;
        continue;
      }
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
    for (int i = 0; i < n; ++i) {
      history[write] = x != nullptr ? x[i] : 0.0f;
      if (++write == size) write = 0;
    }
  }
  fed += n;
}

void NativeRateWrapper::Path::reset() {
  fed = 0;
  write = 0;
  std::fill(history.begin(), history.end(), 0.0f);
}

namespace {

int64_t lcm(int64_t a, int64_t b) { return a / std::gcd(a, b) * b; }

// Whole-number rate, or 0.
int64_t wholeRate(double rate) {
  const int64_t r = std::llround(rate);
  return r > 0 && std::fabs(rate - double(r)) <= 1e-6 ? r : 0;
}

// Input samples per whole step of r8brain's fractional interpolator from `from` to `to`, or 0
// when it steps by a floating-point position (no exact period).
int64_t interpolatorStep(double from, double to) {
  int inStep = 0, outStep = 0;
  return r8b::getWholeStepping(from, to, inStep, outStep) ? int64_t(inStep) : 0;
}

}  // namespace

// The input length after which every stage of r8brain 7.6's converter from `from` to `to`
// repeats its phase; 0 when there is none. It follows the stage selection in the converter's
// constructor (third_party/r8brain-free-src/CDSPResampler.h:124-375): the ratio's own period
// (a / gcd(a, b)) covers a single convolver's decimation; the power-of-two half-band
// down-samplers (2:1 each), the decimating convolver and the fractional interpolator's whole
// step add their factors; up-sampling stages decimate nothing. prepare() then checks the
// result on scratch converters (checkResume), so a missed case disables skipping instead of
// skipping wrongly.
int64_t NativeRateWrapper::converterPeriod(double from, double to) {
  const int64_t a = wholeRate(from);
  const int64_t b = wholeRate(to);
  if (a == 0 || b == 0 || a == b) return 0;
  const int64_t ratio = a / std::gcd(a, b);
  static const int kCommon[5][2] = {{1, 2}, {1, 3}, {2, 3}, {3, 2}, {3, 4}};
  for (const auto& r : kCommon) {
    if (from * r[0] == to * r[1]) return ratio;  // one convolver, num up and den down
  }
  for (int i = 2; i <= 3; ++i) {  // whole-number 2^k or 3 * 2^k up-sampling
    double sm = i;
    while (from * sm < to) sm *= 2.0;
    if (from * sm == to) return ratio;
  }
  const double band = kTransitionBandPercent;
  if (to * 2.0 > from) {
    // A 2x up-sampling convolver, then the fractional interpolator, which reads 2N samples
    // for N input samples, then (with intermediate steps) up-samplers only.
    const double threshold = from / (1.0 - 0.0175 * band);
    int c = 0;
    double div = 1.0;
    while (to >= threshold * div * 2.0) {
      div *= 2.0;
      ++c;
    }
    int tmp1 = 0, tmp2 = 0;
    if (c == 1 && r8b::getWholeStepping(from * 2.0, to, tmp1, tmp2)) c = 0;
    const int64_t step = interpolatorStep(c > 0 ? from * 2.0 * div : from * 2.0, to);
    if (step == 0) return 0;
    return lcm(ratio, step / std::gcd(step, int64_t(2)));
  }
  // Down-sampling: c half-band 2:1 stages, a convolver decimating by downf, and an
  // interpolator when no whole factor fits, which reads N / 2^c samples.
  double check = to * 4.0;
  double div = 1.0;
  int64_t halfBands = 1;
  while (check <= from) {
    check *= 2.0;
    div *= 2.0;
    halfBands *= 2;
  }
  for (int downf = 2; downf <= 3; ++downf) {
    if (to * div * downf == from) return lcm(ratio, halfBands * downf);
  }
  const int64_t step = interpolatorStep(from, to * div);
  if (step == 0) return 0;
  return lcm(ratio, halfBands * step);
}

// A converter restarted with clear() and pre-rolled from a sample index that is a multiple of
// the converter period produces, from then on, the same output counts as channel 0's
// converter, and the same samples once the pre-roll covers its filter (span = twice the
// converter's latency plus a margin; r8brain 7.6, measured: within 1e-15, 5e-11 when
// down-sampling). Paths without a period, with a period over 4096, or whose check fails never
// skip.
void NativeRateWrapper::setUpPath(Path& path, const ConverterSpec& spec, double from, double to, int latencyIn,
                                  int maxChunk, int chunk) {
  path = Path{};
  const int64_t period = converterPeriod(from, to);
  if (period == 0 || period > 4096) return;
  path.period = period;
  path.span = 2 * int64_t(latencyIn) + 64;
  path.history.assign(size_t(path.span + period + maxChunk), 0.0f);
  if (!checkResume(spec, from, to, maxChunk, chunk, path)) path = Path{};
}

// Main thread. Runs one converter without a break and, for resumptions at several
// period-aligned starts (odd and even multiples), a second one restarted with resume(), both
// with the chunk sizes the path sees at run time; true if every resumption gives the same
// output counts and samples within 1e-6. Results are cached per configuration.
bool NativeRateWrapper::checkResume(const ConverterSpec& spec, double from, double to, int maxChunk, int chunk,
                                    const Path& setUp) {
  using Key = std::tuple<double, double, int, int, int64_t, int64_t, int, double, double>;
  static std::mutex mutex;
  static std::map<Key, bool> cache;
  const Key key{from, to, maxChunk, chunk, setUp.period, setUp.span,
                int(spec.phase), spec.attenuationDb, spec.transitionBandPercent};
  {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
  }
  auto chunkAt = [&](size_t call) {
    if (chunk > 0) return chunk;
    static const int kPattern[] = {16, 37, 1, 64, 128, 5, 256, 23};
    return std::clamp(kPattern[call % 8], 1, maxChunk);
  };
  auto signal = [](int64_t t) {  // float samples, as the wrapper feeds its converters
    return double(float(0.5 * std::sin(0.031 * double(t)) + 0.3 * std::sin(0.47 * double(t))));
  };
  constexpr int kTrials = 6;
  const int64_t window = 2 * setUp.span + 8 * int64_t(maxChunk) + 512;
  const int64_t length = setUp.span + (kTrials + 1) * setUp.period + window + 4 * int64_t(maxChunk);
  // The uninterrupted converter: every call's input start and outputs.
  const auto whole = makeConverter(spec, from, to, maxChunk);
  std::vector<int64_t> starts;
  std::vector<size_t> offsets{0};
  std::vector<double> outputs;
  std::vector<double> x(static_cast<size_t>(maxChunk));
  for (int64_t t = 0; t < length;) {
    const int n = chunkAt(starts.size());
    for (int i = 0; i < n; ++i) x[size_t(i)] = signal(t + i);
    double* o = nullptr;
    const int made = whole->process(x.data(), n, o);
    outputs.insert(outputs.end(), o, o + made);
    starts.push_back(t);
    offsets.push_back(outputs.size());
    t += n;
  }
  bool ok = true;
  const auto resumed = makeConverter(spec, from, to, maxChunk);
  Path path = setUp;
  std::vector<double> scratch(static_cast<size_t>(maxChunk));
  std::vector<float> history;
  for (int trial = 1; ok && trial <= kTrials; ++trial) {
    // The first call boundary at or after span + trial periods: aligned starts trial-1, trial.
    const int64_t target = setUp.span + trial * setUp.period + trial * 7;
    size_t call = 0;
    while (call < starts.size() && starts[call] < target) ++call;
    const int64_t at = starts[call];
    path.reset();
    for (int64_t t = 0; t < at;) {
      const int n = int(std::min<int64_t>(maxChunk, at - t));
      history.assign(size_t(n), 0.0f);
      for (int i = 0; i < n; ++i) history[size_t(i)] = float(signal(t + i));
      path.record(history.data(), n);
      t += n;
    }
    // resume() replays the float history, as at run time; compare with the converter fed the
    // same float samples throughout.
    resume(*resumed, path, false, maxChunk, scratch);
    for (int64_t t = at; ok && call + 1 < starts.size() && t < at + window; ++call) {
      const int n = int(starts[call + 1] - starts[call]);
      for (int i = 0; i < n; ++i) x[size_t(i)] = signal(t + i);
      double* o = nullptr;
      const int made = resumed->process(x.data(), n, o);
      const size_t expected = offsets[call + 1] - offsets[call];
      if (size_t(made) != expected) {
        ok = false;
        break;
      }
      for (int i = 0; i < made; ++i) {
        if (std::fabs(o[i] - outputs[offsets[call] + size_t(i)]) > 1e-6) ok = false;
      }
      t += n;
    }
  }
  const std::lock_guard<std::mutex> lock(mutex);
  cache[key] = ok;
  return ok;
}

// Why restart and pre-roll rather than crossfade or keep converting silence: a skipped
// converter's state is stale and out of step (its output count per call would differ), and
// converting silence costs as much as converting the signal. The reference's history is exactly
// what the channel carried while skipped, so the restarted converter reaches the state it would
// have had; the cost is one pre-roll of at most span + period samples per resumption.
// Every voice resuming in the same block pays this at once (at 44.1 kHz, about 6 us per Warps
// converter on an M1); M7 could spread resumptions over several blocks.
void NativeRateWrapper::resume(r8b::CDSPResampler& r, const Path& path, bool silence, int maxChunk,
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

// Both fallbacks restart every converter and FIFO of the wrapper together, at a host block
// boundary, as reset() does, so the input-to-output delay is the reported latency again (a
// one-off gap of about that latency). The engine keeps running, so a held note keeps sounding.
void NativeRateWrapper::fallBackInput() {  // at the start of a block, before any output
  ++fallbacks_;
  inPath_.period = 0;
  restartConversion();
}

void NativeRateWrapper::fallBackOutput() {  // mid-block: restart before the next block
  ++fallbacks_;
  outPath_.period = 0;
  fifoRead_ = 0;
  fifoSize_ = 0;
  restartPending_ = true;
}

void NativeRateWrapper::forceSkipPeriodsForTest(int64_t input, int64_t output) {
  for (Path* path : {&inPath_, &outPath_}) {
    const int64_t period = path == &inPath_ ? input : output;
    if (period > 0 && path->span == 0) continue;  // a path that was never set up
    path->period = period;
    if (period > 0) path->history.assign(size_t(path->span + period) + preroll_.size(), 0.0f);
  }
  reset();
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
      if (made != produced) {  // a resumed converter out of step: never in tests or the check
        fallBackInput();
        pushInput(in, n);  // again, converting every channel: no skipping, so no recursion
        return;
      }
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
      if (made != produced) {  // a resumed converter out of step: never in tests or the check
        fallBackOutput();
        return;
      }
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

// Each host sample enters the input block and leaves from the output block at the same
// position, so everything is one native block late; a full input block renders the next output
// block. As on the converted path, absent inputs read as silence, and the engine gets the
// P's of the call in which its block fills.
void NativeRateWrapper::processWholeBlocks(const float* const* in, float* const* out, const ParamValues& p,
                                           int n) {
  const float* nativeIn[kMaxChannels] = {nativeIn_[0].data(), nativeIn_[1].data(), nativeIn_[2].data(),
                                         nativeIn_[3].data()};
  float* native[kMaxChannels] = {native_[0].data(), native_[1].data(), native_[2].data(), native_[3].data()};
  for (int i = 0; i < n; ++i) {
    for (int ch = 0; ch < inChannels_; ++ch) {
      nativeIn_[ch][wholePos_] = in != nullptr && in[ch] != nullptr ? in[ch][i] : 0.0f;
    }
    for (int ch = 0; ch < outChannels_; ++ch) out[ch][i] = native_[ch][wholePos_];
    if (++wholePos_ == size_t(nativeBlock_)) {
      inner_->process(usesInput_ ? nativeIn : nullptr, native, p, nativeBlock_);
      wholePos_ = 0;
    }
  }
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
