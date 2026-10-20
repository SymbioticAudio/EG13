#include <catch2/catch_test_macros.hpp>

#include "voice/voice_allocator.h"

using namespace eg13;
using Type = VoiceAction::Type;

namespace {

VoiceAction only(const VoiceActions& actions) {
  REQUIRE(actions.count == 1);
  return actions.items[0];
}

}  // namespace

TEST_CASE("Poly gives each new key its own voice") {
  VoiceAllocator a;
  REQUIRE(only(a.noteOn(60, 1.0f)).voice == 0);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 1);
  const VoiceAction third = only(a.noteOn(67, 0.5f));
  REQUIRE(third.type == Type::Trigger);
  REQUIRE(third.voice == 2);
  REQUIRE(third.key == 67);
  REQUIRE(third.velocity == 0.5f);
  REQUIRE(a.soundingCount() == 3);
}

TEST_CASE("Poly note-off releases the voice playing that key") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  const VoiceAction off = only(a.noteOff(64));
  REQUIRE(off.type == Type::Release);
  REQUIRE(off.voice == 1);
  REQUIRE(a.noteOff(61).count == 0);
}

TEST_CASE("Repeating a sounding key retriggers the same voice") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOff(60);
  const VoiceAction again = only(a.noteOn(60, 1.0f));
  REQUIRE(again.type == Type::Trigger);
  REQUIRE(again.voice == 0);
}

TEST_CASE("Idle voices are reused before stealing") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  a.noteOff(60);
  a.voiceIdle(0);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);
}

TEST_CASE("Stealing prefers released voices over held ones") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);  // voice 0, held
  a.noteOn(62, 1.0f);  // voice 1
  a.noteOff(62);       // voice 1 released, still sounding
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 1);
}

TEST_CASE("Stealing takes the oldest held voice when none are released") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);
  REQUIRE(only(a.noteOn(65, 1.0f)).voice == 1);
}

TEST_CASE("Stealing prefers the voice released longest ago") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 3);
  a.noteOn(60, 1.0f);
  a.noteOn(62, 1.0f);
  a.noteOn(64, 1.0f);
  a.noteOff(64);
  a.noteOff(60);
  REQUIRE(only(a.noteOn(65, 1.0f)).voice == 2);
}

TEST_CASE("Sustain pedal holds released notes until pedal up") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  REQUIRE(a.setSustain(true).count == 0);
  REQUIRE(a.noteOff(60).count == 0);
  REQUIRE(a.noteOff(64).count == 0);
  a.noteOn(67, 1.0f);  // still held when the pedal comes up
  const VoiceActions up = a.setSustain(false);
  REQUIRE(up.count == 2);
  REQUIRE(up.items[0].type == Type::Release);
  REQUIRE(up.items[0].voice == 0);
  REQUIRE(up.items[1].voice == 1);
}

TEST_CASE("Stealing treats sustained voices like held ones") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 2);
  a.setSustain(true);
  a.noteOn(60, 1.0f);
  a.noteOff(60);       // voice 0 sustained
  a.noteOn(62, 1.0f);  // voice 1 held
  REQUIRE(only(a.noteOn(64, 1.0f)).voice == 0);  // oldest start, both held/sustained
}

TEST_CASE("Mono retriggers each new note and returns to the previous key") {
  VoiceAllocator a;
  a.configure(VoiceMode::Mono, 8);
  REQUIRE(only(a.noteOn(60, 1.0f)).type == Type::Trigger);
  const VoiceAction second = only(a.noteOn(67, 0.7f));
  REQUIRE(second.type == Type::Trigger);
  REQUIRE(second.voice == 0);
  const VoiceAction back = only(a.noteOff(67));
  REQUIRE(back.type == Type::Trigger);
  REQUIRE(back.key == 60);
  const VoiceAction off = only(a.noteOff(60));
  REQUIRE(off.type == Type::Release);
  REQUIRE(off.voice == 0);
}

TEST_CASE("Legato glides while another key is held") {
  VoiceAllocator a;
  a.configure(VoiceMode::Legato, 8);
  REQUIRE(only(a.noteOn(60, 1.0f)).type == Type::Trigger);
  const VoiceAction glide = only(a.noteOn(67, 1.0f));
  REQUIRE(glide.type == Type::Glide);
  REQUIRE(glide.key == 67);
  const VoiceAction back = only(a.noteOff(67));
  REQUIRE(back.type == Type::Glide);
  REQUIRE(back.key == 60);
  REQUIRE(only(a.noteOff(60)).type == Type::Release);
  REQUIRE(only(a.noteOn(62, 1.0f)).type == Type::Trigger);
}

TEST_CASE("Mono with the sustain pedal releases on pedal up") {
  VoiceAllocator a;
  a.configure(VoiceMode::Mono, 1);
  a.setSustain(true);
  a.noteOn(60, 1.0f);
  REQUIRE(a.noteOff(60).count == 0);
  const VoiceAction up = only(a.setSustain(false));
  REQUIRE(up.type == Type::Release);
  REQUIRE(up.voice == 0);
}

TEST_CASE("configure releases sounding notes when the mode or count changes") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.noteOn(64, 1.0f);
  const VoiceActions changed = a.configure(VoiceMode::Poly, 4);
  REQUIRE(changed.count == 2);
  REQUIRE(changed.items[0].type == Type::Release);
  REQUIRE(a.configure(VoiceMode::Poly, 4).count == 0);  // no change, no actions
  REQUIRE(a.configure(VoiceMode::Legato, 4).count == 0);  // nothing held any more
  REQUIRE(a.mode() == VoiceMode::Legato);
}

TEST_CASE("Voice counts clamp to 1..8") {
  VoiceAllocator a;
  a.configure(VoiceMode::Poly, 0);
  REQUIRE(a.voiceCount() == 1);
  a.configure(VoiceMode::Poly, 99);
  REQUIRE(a.voiceCount() == 8);
}

TEST_CASE("allNotesOff releases held and sustained voices and lifts the pedal") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.setSustain(true);
  a.noteOn(64, 1.0f);
  a.noteOff(64);
  REQUIRE(a.allNotesOff().count == 2);
  a.noteOn(67, 1.0f);
  REQUIRE(a.noteOff(67).count == 1);  // pedal is up again
}

TEST_CASE("voiceIdle frees only released voices") {
  VoiceAllocator a;
  a.noteOn(60, 1.0f);
  a.voiceIdle(0);  // still held: ignored
  REQUIRE(a.sounding(0));
  a.noteOff(60);
  a.voiceIdle(0);
  REQUIRE_FALSE(a.sounding(0));
  REQUIRE(a.soundingCount() == 0);
}
