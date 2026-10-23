#pragma once

#include <cmath>
#include <cstdint>

namespace eg13 {

// What the host says about its transport at the start of a block (CLAP's clap_event_transport).
struct Transport {
  bool playing = false;
  bool hasTempo = false;
  double tempo = 120.0;  // BPM
  bool hasBeats = false;
  double beats = 0.0;     // song position in quarter notes
  double barStart = 0.0;  // quarter notes
  bool hasTimeSignature = false;
  int numerator = 4;
  int denominator = 4;
};

// Musical time for tempo-synced modulation (owner decision 4, spec §6): while the transport
// plays, the beat position is the host's (phase-locked); while it is stopped, it keeps running at
// the last tempo the host gave (120 BPM if none). A start, or a jump in the song position, is
// reported once as jumped() so engines can restart in line with the song.
//
// The beat is origin() + elapsed() x beatsPerSample(): counted in whole samples from the last
// point the clock was set (a lock, a jump or a tempo change), never summed per call, so it is the
// same at every sample whatever the host's block sizes. A playing host that reports the position
// the clock already has (within kSameBeat) leaves the count alone. Whatever the host sends, the
// beat, tempo and bar phase stay finite: values that are not finite or out of range are ignored.
class BeatClock {
 public:
  static constexpr double kDefaultTempo = 120.0;
  static constexpr double kMinTempo = 1.0;     // BPM; a faster or slower host tempo is clamped
  static constexpr double kMaxTempo = 1000.0;
  static constexpr double kMaxBeats = 1e12;    // a host position beyond this is ignored
  static constexpr double kJump = 1.0 / 64.0;  // further than a 64th note from the clock: a jump
  // Closer than this (about 0.024 samples at 120 BPM and 48 kHz): the clock already agrees and
  // keeps its count. A host whose reported positions are less consistent than this (rounded,
  // say, from seconds) re-anchors the clock every block: it still follows the host, but the beat
  // is then only as independent of the block size as the host's own reports.
  static constexpr double kSameBeat = 1e-6;

  void prepare(double sampleRate) { rate_ = std::isfinite(sampleRate) && sampleRate > 0.0 ? sampleRate : 48000.0; }
  void reset() {  // position 0; keeps the last tempo and time signature
    origin_ = 0.0;
    elapsed_ = 0;
    barStart_ = 0.0;
    locked_ = false;
    jump_ = false;
  }
  // Audio thread, before the block's audio.
  void setTransport(const Transport& t) {
    if (t.hasTempo && std::isfinite(t.tempo) && t.tempo > 0.0) {
      const double tempo = t.tempo < kMinTempo ? kMinTempo : t.tempo > kMaxTempo ? kMaxTempo : t.tempo;
      if (tempo != tempo_) {
        restart(beat());  // the position so far, then the new tempo
        tempo_ = tempo;
      }
    }
    if (t.hasTimeSignature && t.numerator > 0 && t.numerator <= 256 && t.denominator > 0 && t.denominator <= 256) {
      beatsPerBar_ = double(t.numerator) * 4.0 / double(t.denominator);
    }
    const bool lock = t.playing && t.hasBeats && validBeats(t.beats);
    if (lock) {
      const double distance = std::fabs(t.beats - beat());
      if (!locked_ || distance > kJump) jump_ = true;  // a start, or a jump in the song
      if (!locked_ || distance > kSameBeat) restart(t.beats);
      if (validBeats(t.barStart)) barStart_ = t.barStart;
    }
    locked_ = lock;
  }
  double beat() const { return origin_ + double(elapsed_) * beatsPerSample(); }
  double origin() const { return origin_; }
  int64_t elapsed() const { return elapsed_; }  // samples since origin()
  double barStart() const { return barStart_; }
  double tempo() const { return tempo_; }
  double beatsPerSample() const { return tempo_ / 60.0 / rate_; }
  double beatsPerBar() const { return beatsPerBar_; }
  double barPhase() const { return barPhaseAt(beat(), barStart_, beatsPerBar_); }
  bool jumped() const { return jump_; }
  void advance(int n) {  // after n samples
    if (n > 0) elapsed_ += n;
    jump_ = false;
  }

  // 0..1 within the bar that starts at barStart (also used per tick by the Part).
  static double barPhaseAt(double beat, double barStart, double beatsPerBar) {
    const double bars = (beat - barStart) / beatsPerBar;
    const double phase = bars - std::floor(bars);
    return std::isfinite(phase) && phase >= 0.0 && phase < 1.0 ? phase : 0.0;
  }

 private:
  static bool validBeats(double beats) { return std::isfinite(beats) && std::fabs(beats) <= kMaxBeats; }
  void restart(double origin) {
    origin_ = origin;
    elapsed_ = 0;
  }

  double rate_ = 48000.0;
  double tempo_ = kDefaultTempo;
  double beatsPerBar_ = 4.0;
  double origin_ = 0.0;
  int64_t elapsed_ = 0;
  double barStart_ = 0.0;
  bool locked_ = false;  // following the playing host's position
  bool jump_ = false;
};

}  // namespace eg13
