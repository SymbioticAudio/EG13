#include <catch2/catch_test_macros.hpp>

#include "voice/mono_note_stack.h"

using eg13::MonoNoteStack;

TEST_CASE("Most recent note has priority") {
  MonoNoteStack s;
  REQUIRE(s.empty());
  s.noteOn(60, 0.5f);
  s.noteOn(64, 0.7f);
  REQUIRE(s.topKey() == 64);
  REQUIRE(s.topVelocity() == 0.7f);
}

TEST_CASE("Releasing the top note returns to the previous held note") {
  MonoNoteStack s;
  s.noteOn(60, 0.5f);
  s.noteOn(64, 0.7f);
  s.noteOn(67, 0.9f);
  s.noteOff(67);
  REQUIRE(s.topKey() == 64);
  s.noteOff(60);  // releasing a lower note keeps the top
  REQUIRE(s.topKey() == 64);
  s.noteOff(64);
  REQUIRE(s.empty());
}

TEST_CASE("Repeating a held key moves it to the top without duplicating it") {
  MonoNoteStack s;
  s.noteOn(60, 0.5f);
  s.noteOn(64, 0.5f);
  s.noteOn(60, 0.9f);
  REQUIRE(s.size() == 2);
  REQUIRE(s.topKey() == 60);
  REQUIRE(s.topVelocity() == 0.9f);
  s.noteOff(60);
  REQUIRE(s.topKey() == 64);
}

TEST_CASE("Releasing a key that is not held does nothing") {
  MonoNoteStack s;
  s.noteOn(60, 0.5f);
  s.noteOff(61);
  REQUIRE(s.topKey() == 60);
}

TEST_CASE("A full stack drops the oldest note") {
  MonoNoteStack s;
  for (int k = 0; k < MonoNoteStack::kCapacity + 1; ++k) s.noteOn(40 + k, 1.0f);
  REQUIRE(s.size() == MonoNoteStack::kCapacity);
  REQUIRE(s.topKey() == 40 + MonoNoteStack::kCapacity);
  for (int k = 1; k <= MonoNoteStack::kCapacity; ++k) s.noteOff(40 + k);
  REQUIRE(s.empty());  // key 40 was dropped, so nothing is left
}

TEST_CASE("clear empties the stack") {
  MonoNoteStack s;
  s.noteOn(60, 1.0f);
  s.clear();
  REQUIRE(s.empty());
}
