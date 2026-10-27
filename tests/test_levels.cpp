#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/engine_registry.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "synth_helpers.h"
#include "test_helpers.h"
#include "voice/processor_factory.h"
#include "voice/processor_slot.h"
#include "voice/source_factory.h"
#include "voice/voice.h"

using namespace eg13;

namespace {

constexpr double kRate = 48000.0;

std::vector<float> saw(size_t n) {
  std::vector<float> x(n);
  for (size_t i = 0; i < n; ++i) x[i] = 0.5f * float(2.0 * std::fmod(110.0 * double(i) / kRate, 1.0) - 1.0);
  return x;
}

struct Level {
  double gainDb;
  float peak;
};

Level measure(ProcessorSlot& slot, const ParamValues& p) {
  const auto in = saw(48000);
  std::vector<float> l(in.size()), r(in.size());
  slot.noteOn(45.0f, 1.0f);  // A2, the saw's pitch, for the Ring Mod oscillator
  for (size_t done = 0; done < in.size(); done += 16) {
    const float* i2[2] = {in.data() + done, in.data() + done};
    float* o2[2] = {l.data() + done, r.data() + done};
    slot.process(i2, nullptr, o2, p, 16);
  }
  float peak = 0.0f;
  for (size_t i = 9600; i < l.size(); ++i) peak = std::max(peak, std::fabs(l[i]));
  return {20.0 * std::log10(test::rms(l, 9600, l.size()) / test::rms(in, 9600, in.size())), peak};
}

}  // namespace

TEST_CASE("Output levels of every Filter and Shaper at its defaults", "[levels]") {
  std::printf("Level of a 110 Hz saw (peak 0.5) through each engine at its defaults, 48 kHz:\n");
  for (int e = 0; e < kNumFilterEngines; ++e) {
    ProcessorSlot slot(makeAllFilters());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    const Level level = measure(slot, test::defaultParams(filterEngineInfo(e)));
    std::printf("  Filter %-22s %+6.2f dB  peak %.3f\n", filterEngineInfo(e).displayName, level.gainDb, double(level.peak));
    REQUIRE(std::fabs(level.gainDb) < 1.5);  // open filters do not jump the level
  }
  for (int e = 0; e < kNumShaperEngines; ++e) {
    ProcessorSlot slot(makeAllShapers());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    const Level level = measure(slot, test::defaultParams(shaperEngineInfo(e)));
    std::printf("  Shaper %-22s %+6.2f dB  peak %.3f\n", shaperEngineInfo(e).displayName, level.gainDb, double(level.peak));
    REQUIRE(level.peak <= 32768.0f / 10240.0f);  // Warps' 16-bit range, read back at EG13 scale
    if (e != int(ShaperEngine::RingMod)) REQUIRE(std::fabs(level.gainDb) < 0.5);  // Off, flat EQ
  }
  std::printf("  Ring Mod / Vocoder by Algorithm (other parameters at their defaults):\n");
  for (int step = 0; step <= 8; ++step) {
    ProcessorSlot slot(makeAllShapers());
    slot.prepare(kRate, 16);
    slot.setEngine(int(ShaperEngine::RingMod));
    slot.reset();
    ParamValues p = test::defaultParams(shaperEngineInfo(int(ShaperEngine::RingMod)));
    p.p[0] = float(step) / 8.0f;
    const Level level = measure(slot, p);
    std::printf("    %d/8  %+6.2f dB  peak %.3f\n", step, level.gainDb, double(level.peak));
    REQUIRE(level.peak <= 32768.0f / 10240.0f);
  }
}

TEST_CASE("Reported latency per plugin and host rate", "[levels]") {
  for (ConverterQuality q : {ConverterQuality::Live, ConverterQuality::Studio}) {
    for (PluginKind kind : {PluginKind::Instrument, PluginKind::Effect}) {
      for (double rate : {44100.0, 48000.0, 88200.0, 96000.0}) {
        Synth synth(kind);
        synth.setParam(paramSpec(masterParamIndex(MasterParam::LatencyMode)).id, double(int(q)));
        synth.prepare(rate, 512);
        REQUIRE(synth.preparedQuality() == q);
        // The parts of the total: the Source path as the plugin aligns it, and the Shaper slot.
        Voice sourceOnly(makeAllSources());
        sourceOnly.prepare(rate, 512, kind == PluginKind::Effect ? Voice::Alignment::Input : Voice::Alignment::Notes, q);
        ProcessorSlot shapers(makeAllShapers());
        shapers.prepare(rate, Voice::kControlBlock, q);
        ProcessorSlot partFx(makeAllPartFx());  // M5: every part has a Part FX slot, Off included
        partFx.prepare(rate, 512, q);
        std::printf("%-6s %-10s %6.0f Hz: %4d samples (%.2f ms) = Source %d + Shaper %d + Part FX %d\n",
                    q == ConverterQuality::Live ? "Live" : "Studio", kind == PluginKind::Effect ? "EG13 FX" : "EG13",
                    rate, synth.latencySamples(), 1000.0 * synth.latencySamples() / rate,
                    sourceOnly.latencySamples(), shapers.latencySamples(), partFx.latencySamples());
        if (kind == PluginKind::Instrument) {  // which Part FX engine sets the slot's delay (G23)
          std::string each;
          for (int e = 1; e < kNumPartFxEngines; ++e) {
            std::vector<SourceSpec> only;
            only.push_back(makePartFx(e));
            ProcessorSlot one(std::move(only));
            one.prepare(rate, 512, q);
            each += std::string(partFxEngineInfo(e).displayName) + " " + std::to_string(one.latencySamples()) + "  ";
            REQUIRE(one.latencySamples() <= partFx.latencySamples());
          }
          std::printf("        Part FX alone: %s\n", each.c_str());
        }
        REQUIRE(synth.latencySamples() > 0);
        REQUIRE(synth.latencySamples() ==
                sourceOnly.latencySamples() + shapers.latencySamples() + partFx.latencySamples());
      }
    }
  }
}

// A host's reset-to-default sets each Filter/Shaper P to one fixed value whatever engine is
// selected (CLAP cannot change a default while active). Those values must suit every engine of
// the slot together: audible, finite, and for the filters and the EQ near the input's level.
TEST_CASE("Host defaults of the Filter and Shaper P's suit every engine of the slot", "[levels]") {
  for (SlotKind kind : {SlotKind::Filter, SlotKind::Shaper}) {
    ParamValues hostDefaults;
    for (int p = 0; p < int(slotParamCount(kind)); ++p) {
      hostDefaults.p[p] = paramDefault(slotParamIndex(kind, 0, p), PluginKind::Effect);
      // One default per P position: the same for both parts and both plugins, except the
      // instrument's Filter P1, the init patch's cutoff (M4, provisional ruling 28).
      for (int part = 0; part < kNumParts; ++part) {
        for (PluginKind plugin : {PluginKind::Instrument, PluginKind::Effect}) {
          if (kind == SlotKind::Filter && p == 0 && plugin == PluginKind::Instrument) {
            REQUIRE(paramDefault(slotParamIndex(kind, part, p), plugin) == 0.7f);
            continue;
          }
          REQUIRE(paramDefault(slotParamIndex(kind, part, p), plugin) == hostDefaults.p[p]);
        }
      }
    }
    const int engines = kind == SlotKind::Filter ? kNumFilterEngines : kNumShaperEngines;
    for (int e = 0; e < engines; ++e) {
      ProcessorSlot slot(kind == SlotKind::Filter ? makeAllFilters() : makeAllShapers());
      slot.prepare(kRate, 16);
      slot.setEngine(e);
      slot.reset();
      const Level level = measure(slot, hostDefaults);
      const EngineInfo& info = kind == SlotKind::Filter ? filterEngineInfo(e) : shaperEngineInfo(e);
      std::printf("Host defaults: %-22s %+6.2f dB  peak %.3f\n", info.displayName, level.gainDb,
                  double(level.peak));
      REQUIRE(std::isfinite(level.gainDb));
      REQUIRE(std::isfinite(level.peak));
      if (kind == SlotKind::Shaper && e == int(ShaperEngine::RingMod)) {
        REQUIRE(level.gainDb > -20.0);  // audible
        REQUIRE(level.peak <= 32768.0f / 10240.0f);
      } else {
        REQUIRE(level.gainDb > -12.0);
        REQUIRE(level.gainDb < 6.0);
      }
    }
  }
}

TEST_CASE("The instrument's init cutoff suits every filter", "[levels]") {
  for (int e = 0; e < kNumFilterEngines; ++e) {
    ProcessorSlot slot(makeAllFilters());
    slot.prepare(kRate, 16);
    slot.setEngine(e);
    slot.reset();
    ParamValues p;
    for (int i = 0; i < int(kFilterParamCount); ++i) {
      p.p[i] = paramDefault(filterParamIndex(0, i), PluginKind::Instrument);
    }
    const Level level = measure(slot, p);
    std::printf("Init cutoff: %-22s %+6.2f dB  peak %.3f\n", filterEngineInfo(e).displayName, level.gainDb,
                double(level.peak));
    REQUIRE(level.gainDb > -6.0);
    REQUIRE(level.gainDb < 3.0);
  }
}

namespace {

// Peak of Part A's output for `notes` held for one second at velocity 1 (C3 upward in thirds),
// at the given Source engine, after the first 0.1 s. m3: no modulation, Voice Gain 0 dB, Soft
// Clip off (the trims alone); otherwise the instrument's init patch.
float sourcePeak(int engine, int notes, bool m3, bool softClip) {
  Synth synth(PluginKind::Instrument);
  if (m3) test::m3Patch(synth);
  synth.setParam(paramSpec(sourceEngineParamIndex(0)).id, double(engine));
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Level)).id, 1.0);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::SoftClip)).id, softClip ? 1.0 : 0.0);
  synth.prepare(kRate, 128);
  std::vector<float> in(128), l(128), r(128);
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  for (int k = 0; k < notes; ++k) synth.noteOn(0, 48 + 4 * k, 1.0f);
  float peak = 0.0f;
  for (int b = 0; b < int(kRate) / 128; ++b) {
    for (int i = 0; i < 128; ++i) in[size_t(i)] = 0.5f * float(std::sin(6.283185307179586 * 220.0 * double(b * 128 + i) / kRate));
    synth.process(input, out, nullptr, 128);
    if (b * 128 < int(kRate) / 10) continue;
    for (int i = 0; i < 128; ++i) peak = std::max(peak, std::max(std::fabs(l[size_t(i)]), std::fabs(r[size_t(i)])));
  }
  return peak;
}

}  // namespace

TEST_CASE("Source trims bring every Source near the Macro Oscillator's level", "[levels]") {
  const float macro = sourcePeak(int(SourceEngine::Macro), 1, true, false);
  std::printf("One held C3 at velocity 1, defaults, after the trims (Voice Gain 0 dB, Soft Clip off):\n");
  for (int e = 0; e < kNumSourceEngines; ++e) {
    const float peak = sourcePeak(e, 1, true, false);
    const double db = 20.0 * std::log10(double(peak) / double(macro));
    std::printf("  %-20s trim %+5.1f dB  peak %.3f (%+5.1f dB against the Macro Oscillator)\n",
                sourceEngineInfo(e).displayName, double(sourceTrimDb(e)), double(peak), db);
    if (e != int(SourceEngine::AudioInput)) REQUIRE(std::fabs(db) < 1.5);  // the Input sets its own level
  }
}

TEST_CASE("Eight-note chords with the init patch stay within full scale", "[levels]") {
  std::printf("Eight-note chord, init patch (Voice Gain -12 dB), master 100%%: peak before / after Soft Clip\n");
  for (int e = 0; e < kNumSourceEngines; ++e) {
    const float raw = sourcePeak(e, 8, false, false);
    const float clipped = sourcePeak(e, 8, false, true);
    std::printf("  %-20s %.3f / %.3f\n", sourceEngineInfo(e).displayName, double(raw), double(clipped));
    REQUIRE(clipped <= 1.0f);
    REQUIRE(std::isfinite(raw));
  }
}

namespace {

struct PartFxLevel {
  float peak;
  double rms;
};

// Level of Part A's output for one held C3 at velocity 1 (Macro Oscillator, m3 patch: no
// modulation, Voice Gain 0 dB, Soft Clip off) through the given Part FX at its host defaults,
// over the second after the first 0.5 s (past the engines' latency and onset).
PartFxLevel partFxLevel(int engine, ConverterQuality quality = ConverterQuality::Live) {
  Synth synth(PluginKind::Instrument);
  test::m3Patch(synth);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::LatencyMode)).id, double(int(quality)));
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Level)).id, 1.0);
  synth.setParam(paramSpec(partFxEngineParamIndex(0)).id, double(engine));
  for (int p = 0; p < int(kPartFxParamCount); ++p) {
    synth.setParam(paramSpec(partFxParamIndex(0, p)).id, double(kPartFxHostDefaults[p]));
  }
  synth.prepare(kRate, 128);
  std::vector<float> in(128, 0.0f), l(128), r(128);
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  synth.noteOn(0, 48, 1.0f);
  float peak = 0.0f;
  double sum = 0.0;
  size_t n = 0;
  for (int b = 0; b < int(1.5 * kRate) / 128; ++b) {
    synth.process(input, out, nullptr, 128);
    if (b * 128 < int(kRate) / 2) continue;
    for (int i = 0; i < 128; ++i) {
      peak = std::max(peak, std::max(std::fabs(l[size_t(i)]), std::fabs(r[size_t(i)])));
      sum += double(l[size_t(i)]) * double(l[size_t(i)]) + double(r[size_t(i)]) * double(r[size_t(i)]);
      n += 2;
    }
  }
  return {peak, std::sqrt(sum / double(n))};
}

}  // namespace

TEST_CASE("Output levels of every Part FX at its defaults", "[levels]") {
  std::printf("One held C3 at velocity 1 through each Part FX at its host defaults (trims applied), against Off:\n");
  for (ConverterQuality q : {ConverterQuality::Live, ConverterQuality::Studio}) {
    const PartFxLevel off = partFxLevel(int(PartFxEngine::Off), q);
    REQUIRE(off.peak > 0.01f);
    for (int e = 0; e < kNumPartFxEngines; ++e) {
      const PartFxLevel level = partFxLevel(e, q);
      const double peakDb = 20.0 * std::log10(double(level.peak) / double(off.peak));
      const double rmsDb = 20.0 * std::log10(level.rms / off.rms);
      std::printf("  %-6s %-14s trim %+5.1f dB  peak %.3f (%+5.2f dB)  rms %.4f (%+5.2f dB)\n",
                  q == ConverterQuality::Live ? "Live" : "Studio", partFxEngineInfo(e).displayName,
                  double(partFxTrimDb(e)), double(level.peak), peakDb, level.rms, rmsDb);
      REQUIRE(std::isfinite(peakDb));
      REQUIRE(std::fabs(peakDb) < 6.0);  // no engine jumps the peak
      REQUIRE(std::fabs(rmsDb) < 1.5);   // the trims bring each engine's loudness within 1.5 dB of Off (G11)
    }
  }
}

// A host's reset-to-default sets each Part FX P to one fixed value whatever engine is selected
// (kPartFxHostDefaults). They must make every engine finite and audible.
TEST_CASE("Host defaults of the Part FX P's suit every engine", "[levels]") {
  for (int part = 0; part < kNumParts; ++part) {
    for (int p = 0; p < int(kPartFxParamCount); ++p) {
      for (PluginKind plugin : {PluginKind::Instrument, PluginKind::Effect}) {
        REQUIRE(paramDefault(partFxParamIndex(part, p), plugin) == kPartFxHostDefaults[p]);
      }
    }
  }
  for (int e = 1; e < kNumPartFxEngines; ++e) {
    const PartFxLevel level = partFxLevel(e);
    std::printf("Host defaults: %-14s peak %.3f rms %.4f\n", partFxEngineInfo(e).displayName, double(level.peak),
                level.rms);
    REQUIRE(std::isfinite(level.peak));
    REQUIRE(std::isfinite(level.rms));
    REQUIRE(level.peak > 0.01f);  // an audible wet signal
    REQUIRE(level.peak < 1.5f);
  }
}

namespace {

std::string partLevelText(double position) {
  char buf[32];
  formatParam(partParamIndex(0, PartParam::Level), position, buf, sizeof buf);
  return buf;
}

// The knob position that gives `db` (the inverse of partLevelGain()).
double partLevelFor(double db) { return double(kPartLevelUnity) * std::pow(10.0, db / 60.0); }

// RMS of the synth's output for one held C3 on the Audio Input (a 220 Hz sine, so every render
// is the same), 0.5 to 1 s, Part A only, master 100 %, Soft Clip off. m3: no modulation (the
// unmodulated path); otherwise the init patch's modulated path, with `route` (when not 0) from
// Gate to Part Level.
double partOutputRms(double level, bool m3, double route = 0.0) {
  Synth synth(PluginKind::Instrument);
  if (m3) test::m3Patch(synth);
  synth.setParam(paramSpec(sourceEngineParamIndex(0)).id, double(SourceEngine::AudioInput));
  synth.setParam(paramSpec(masterParamIndex(MasterParam::Level)).id, 1.0);
  synth.setParam(paramSpec(masterParamIndex(MasterParam::SoftClip)).id, 0.0);
  synth.setParam(paramSpec(partParamIndex(0, PartParam::Level)).id, level);
  synth.setParam(paramSpec(partParamIndex(1, PartParam::Level)).id, 0.0);
  if (route != 0.0) {
    synth.setRoute(0, 5, Route{kSrcGate, kDestPartLevel, kSrcNone});
    synth.setParam(paramSpec(routeAmountParamIndex(0, 5)).id, route);
  }
  synth.prepare(kRate, 128);
  std::vector<float> in(128), l(128), r(128), all;
  const float* input[2] = {in.data(), in.data()};
  float* out[2] = {l.data(), r.data()};
  synth.noteOn(0, 48, 1.0f);
  for (int b = 0; b < int(kRate) / 128; ++b) {
    for (int i = 0; i < 128; ++i) in[size_t(i)] = 0.25f * float(std::sin(6.283185307179586 * 220.0 * double(b * 128 + i) / kRate));
    synth.process(input, out, nullptr, 128);
    all.insert(all.end(), l.begin(), l.end());
  }
  return test::rms(all, all.size() / 2, all.size());
}

}  // namespace

TEST_CASE("Part Level runs from silence to +12 dB, with 0 dB at its default", "[levels]") {
  const ParamSpec& spec = paramSpec(partParamIndex(0, PartParam::Level));
  REQUIRE(spec.minValue == 0.0f);
  REQUIRE(spec.maxValue == 1.0f);
  REQUIRE(spec.defaultValue == kPartLevelUnity);
  REQUIRE(spec.effectDefault == kPartLevelUnity);
  REQUIRE(partLevelGain(0.0f) == 0.0f);
  REQUIRE(partLevelGain(kPartLevelUnity) == 1.0f);  // exactly: Init sounds as before
  REQUIRE(std::fabs(20.0 * std::log10(double(partLevelGain(1.0f))) - 12.0) < 0.01);
  REQUIRE(partLevelGain(-1.0f) == 0.0f);
  REQUIRE(partLevelGain(2.0f) == partLevelGain(1.0f));
  // Fine near 0 dB: one percent of the knob moves less than half a decibel there.
  const double step = 20.0 * std::log10(double(partLevelGain(kPartLevelUnity + 0.01f)));
  REQUIRE(step > 0.0);
  REQUIRE(step < 0.5);
  for (float v = 0.01f; v <= 1.0f; v += 0.01f) REQUIRE(partLevelGain(v) > partLevelGain(v - 0.01f));

  REQUIRE(partLevelText(0.0) == "-inf dB");
  REQUIRE(partLevelText(double(kPartLevelUnity)) == "0.0 dB");
  REQUIRE(partLevelText(1.0) == "+12.0 dB");
  REQUIRE(partLevelText(partLevelFor(-6.0)) == "-6.0 dB");
  REQUIRE(partLevelText(partLevelFor(3.5)) == "+3.5 dB");
  REQUIRE(partLevelText(partLevelFor(-0.04)) == "0.0 dB");  // no "-0.0 dB"
}

TEST_CASE("Part Level is the part's gain in dB on both paths", "[levels]") {
  for (const bool m3 : {true, false}) {
    INFO((m3 ? "unmodulated path" : "modulated path"));
    const double unity = partOutputRms(double(kPartLevelUnity), m3);
    REQUIRE(unity > 1e-3);
    for (const double db : {-12.0, -6.0, 6.0, 12.0}) {
      const double rms = partOutputRms(partLevelFor(db), m3);
      INFO(db << " dB asked, " << 20.0 * std::log10(rms / unity) << " dB measured");
      REQUIRE(std::fabs(20.0 * std::log10(rms / unity) - db) < 0.05);
    }
    REQUIRE(partOutputRms(0.0, m3) == 0.0);
  }
}

TEST_CASE("A Part Level route moves the knob position and cannot pass +12 dB", "[levels]") {
  const double unity = partOutputRms(double(kPartLevelUnity), false);
  const double top = partOutputRms(1.0, false);
  REQUIRE(std::fabs(20.0 * std::log10(top / unity) - 12.0) < 0.05);
  // From the top, a full positive route adds nothing; from 0 dB it reaches +12 dB and no further.
  REQUIRE(std::fabs(20.0 * std::log10(partOutputRms(1.0, false, 1.0) / top)) < 0.01);
  REQUIRE(std::fabs(20.0 * std::log10(partOutputRms(double(kPartLevelUnity), false, 1.0) / top)) < 0.01);
  // A negative route works in the knob's space: -50 % from the top is the knob at 50 %.
  const double half = partOutputRms(0.5, false);
  REQUIRE(std::fabs(20.0 * std::log10(partOutputRms(1.0, false, -0.5) / half)) < 0.01);
}
