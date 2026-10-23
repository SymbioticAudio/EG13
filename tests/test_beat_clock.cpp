#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

#include "mod/beat_clock.h"

using namespace eg13;

TEST_CASE("The beat clock free-runs at 120 BPM before the host gives a tempo") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  REQUIRE(clock.tempo() == 120.0);
  clock.advance(48000);
  REQUIRE(std::fabs(clock.beat() - 2.0) < 1e-9);
  REQUIRE(std::fabs(clock.barPhase() - 0.5) < 1e-9);
}

TEST_CASE("The beat clock locks to the playing transport and keeps the last tempo when stopped") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  Transport t;
  t.playing = true;
  t.hasTempo = true;
  t.tempo = 90.0;
  t.hasBeats = true;
  t.beats = 17.25;
  t.barStart = 16.0;
  t.hasTimeSignature = true;
  t.numerator = 3;
  t.denominator = 4;
  clock.setTransport(t);
  REQUIRE(clock.jumped());  // the transport started
  REQUIRE(clock.beat() == 17.25);
  REQUIRE(std::fabs(clock.barPhase() - 1.25 / 3.0) < 1e-12);
  clock.advance(480);
  REQUIRE_FALSE(clock.jumped());
  t.beats = 17.25 + 480.0 * 1.5 / 48000.0;  // where the host says it is next: no jump
  clock.setTransport(t);
  REQUIRE_FALSE(clock.jumped());
  t.beats = 40.0;  // the host jumped (loop, locate)
  clock.setTransport(t);
  REQUIRE(clock.jumped());
  clock.advance(1);
  Transport stopped;  // no tempo, not playing: keep 90 BPM and run on
  clock.setTransport(stopped);
  const double before = clock.beat();
  clock.advance(32000);
  REQUIRE(std::fabs(clock.beat() - (before + 1.0)) < 1e-9);  // 90 BPM: 1.5 beats per second
  REQUIRE(clock.tempo() == 90.0);
  t.beats = 50.0;  // playing again: a start
  clock.setTransport(t);
  REQUIRE(clock.jumped());
  REQUIRE(clock.beat() == 50.0);
}

TEST_CASE("The beat clock ignores nonsense from the host") {
  BeatClock clock;
  clock.prepare(44100.0);
  clock.reset();
  Transport t;
  t.hasTempo = true;
  t.tempo = 0.0;
  t.hasTimeSignature = true;
  t.numerator = 0;
  clock.setTransport(t);
  REQUIRE(clock.tempo() == 120.0);
  REQUIRE(clock.beatsPerBar() == 4.0);
  t.tempo = NAN;
  t.playing = true;
  t.hasBeats = true;
  t.beats = NAN;
  clock.setTransport(t);
  REQUIRE(clock.tempo() == 120.0);
  REQUIRE(std::isfinite(clock.beat()));
}

TEST_CASE("The beat clock's beat and bar phase stay finite whatever the host sends") {
  const double inf = std::numeric_limits<double>::infinity();
  const double nan = std::numeric_limits<double>::quiet_NaN();
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  Transport t;
  t.playing = true;
  t.hasTempo = true;
  t.hasBeats = true;
  t.hasTimeSignature = true;
  t.beats = 8.0;
  clock.setTransport(t);
  for (const double tempo : {nan, inf, -inf, -90.0, 1e300}) {
    t.tempo = tempo;
    clock.setTransport(t);
    clock.advance(4800);
    REQUIRE(std::isfinite(clock.tempo()));
    REQUIRE(std::isfinite(clock.beatsPerSample()));
    REQUIRE(std::isfinite(clock.beat()));
    REQUIRE(std::isfinite(clock.barPhase()));
  }
  t.tempo = 120.0;
  for (const double beats : {nan, inf, -inf, 1e300, -1e300}) {
    t.beats = beats;
    t.barStart = -beats;
    clock.setTransport(t);
    clock.advance(4800);
    REQUIRE(std::isfinite(clock.beat()));
    REQUIRE(std::isfinite(clock.barPhase()));
    REQUIRE(clock.barPhase() >= 0.0);
    REQUIRE(clock.barPhase() < 1.0);
  }
  t.beats = 8.0;
  t.barStart = nan;
  t.numerator = 7;
  t.denominator = 0;  // no meter: the last one stays
  clock.setTransport(t);
  REQUIRE(clock.beat() == 8.0);
  REQUIRE(clock.beatsPerBar() == 4.0);
  REQUIRE(std::isfinite(clock.barPhase()));
  t.numerator = 1 << 30;  // absurd meters are capped, not trusted
  t.denominator = 1;
  clock.setTransport(t);
  REQUIRE(std::isfinite(clock.beatsPerBar()));
  REQUIRE(clock.beatsPerBar() > 0.0);
  BeatClock unprepared;  // prepare() with a nonsense rate keeps a usable one
  unprepared.prepare(NAN);
  unprepared.advance(1000);
  REQUIRE(std::isfinite(unprepared.beat()));
}

TEST_CASE("The beat clock counts samples, so the beat does not depend on how they are sliced") {
  const double rate = GENERATE(44100.0, 48000.0, 96000.0);
  CAPTURE(rate);
  for (const bool playing : {false, true}) {
    BeatClock whole, sliced;
    for (BeatClock* c : {&whole, &sliced}) {
      c->prepare(rate);
      c->reset();
    }
    Transport t;
    t.playing = playing;
    t.hasTempo = true;
    t.tempo = 133.7;
    t.hasBeats = true;
    t.beats = 3.3;
    whole.setTransport(t);
    sliced.setTransport(t);
    const int total = int(rate * 3.0);
    whole.advance(total);
    for (int done = 0, i = 0; done < total; ++i) {
      const int n = std::min(1 + (i * 37) % 300, total - done);
      if (playing) {  // a host that reports its own position every block, as hosts do
        t.beats = 3.3 + double(done) * 133.7 / 60.0 / rate;
        sliced.setTransport(t);
        if (done > 0) REQUIRE_FALSE(sliced.jumped());  // the first block still carries the start
      }
      sliced.advance(n);
      done += n;
    }
    INFO("playing " << playing);
    REQUIRE(whole.beat() == sliced.beat());
    REQUIRE(whole.barPhase() == sliced.barPhase());
    REQUIRE(whole.origin() == sliced.origin());
    REQUIRE(whole.elapsed() == sliced.elapsed());
  }
}

TEST_CASE("The beat clock follows a host that drifts slightly without reporting a jump") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  Transport t;
  t.playing = true;
  t.hasTempo = true;
  t.hasBeats = true;
  t.beats = 0.0;
  clock.setTransport(t);
  clock.advance(48000);  // 2 beats at 120 BPM
  t.beats = 2.0 + 1.0 / 128.0;  // within a 64th note: followed silently
  clock.setTransport(t);
  REQUIRE_FALSE(clock.jumped());
  REQUIRE(clock.beat() == t.beats);
  t.tempo = 150.0;  // a tempo change keeps the position and runs on at the new tempo
  clock.setTransport(t);
  REQUIRE_FALSE(clock.jumped());
  REQUIRE(clock.beat() == t.beats);
  clock.advance(48000);
  REQUIRE(std::fabs(clock.beat() - (t.beats + 2.5)) < 1e-9);
}

TEST_CASE("The beat clock changes tempo while stopped without moving the beat") {
  BeatClock clock;
  clock.prepare(48000.0);
  clock.reset();
  clock.advance(24000);  // 1 beat at 120 BPM
  Transport t;
  t.hasTempo = true;
  t.tempo = 60.0;
  t.hasBeats = true;
  t.beats = 99.0;  // stopped: the host's position is not followed
  clock.setTransport(t);
  REQUIRE_FALSE(clock.jumped());
  REQUIRE(std::fabs(clock.beat() - 1.0) < 1e-12);
  clock.advance(48000);  // 1 beat at 60 BPM
  REQUIRE(std::fabs(clock.beat() - 2.0) < 1e-9);
  clock.reset();  // position 0, the last tempo kept
  REQUIRE(clock.beat() == 0.0);
  REQUIRE(clock.tempo() == 60.0);
}
