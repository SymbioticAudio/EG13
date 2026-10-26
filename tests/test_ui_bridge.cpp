#include <catch2/catch_test_macros.hpp>

#include <clap/clap.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "alloc_counter.h"
#include "params/param_ids.h"
#include "synth/param_table.h"
#include "synth/synth.h"
#include "ui_bridge.h"

using eg13::UiBridge;

namespace {

struct Recorded {
  uint16_t type;
  uint32_t id;
  double value;
};

// A fake clap_output_events_t that records the events pushed to it.
struct FakeOut {
  FakeOut() {
    events.reserve(8192);
    out.ctx = this;
    out.try_push = [](const clap_output_events_t* o, const clap_event_header_t* h) {
      auto* self = static_cast<FakeOut*>(o->ctx);
      if (h->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* e = reinterpret_cast<const clap_event_param_value_t*>(h);
        self->events.push_back({h->type, e->param_id, e->value});
      } else {
        const auto* e = reinterpret_cast<const clap_event_param_gesture_t*>(h);
        self->events.push_back({h->type, e->param_id, 0.0});
      }
      return true;
    };
  }
  clap_output_events_t out{};
  std::vector<Recorded> events;
};

// Two real parameter IDs: the first is continuous with a range wider than 0.
uint32_t idAt(size_t index) { return eg13::paramSpec(index).id; }

}  // namespace

TEST_CASE("Edits reach the synth and the host in order") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int flushes = 0;
  UiBridge bridge(synth, [&flushes] { ++flushes; });
  const uint32_t a = idAt(0);
  const uint32_t b = idAt(1);
  const double va = double(eg13::paramSpec(0).maxValue);
  const double vb = double(eg13::paramSpec(1).minValue);

  bridge.beginEdit(a);
  bridge.edit(a, va);
  bridge.edit(b, vb);
  bridge.endEdit(a);
  REQUIRE(flushes == 1);  // coalesced until the next drain
  FakeOut fake;
  bridge.drain(&fake.out);

  REQUIRE(fake.events.size() == 4);
  REQUIRE(fake.events[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  REQUIRE(fake.events[0].id == a);
  REQUIRE(fake.events[1].type == CLAP_EVENT_PARAM_VALUE);
  REQUIRE(fake.events[1].id == a);
  REQUIRE(fake.events[1].value == va);
  REQUIRE(fake.events[2].type == CLAP_EVENT_PARAM_VALUE);
  REQUIRE(fake.events[2].id == b);
  REQUIRE(fake.events[2].value == vb);
  REQUIRE(fake.events[3].type == CLAP_EVENT_PARAM_GESTURE_END);
  REQUIRE(fake.events[3].id == a);
  REQUIRE(synth.param(a) == va);
  REQUIRE(synth.param(b) == vb);
}

TEST_CASE("Edits are clamped and unknown ids are ignored") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  const double max = double(eg13::paramSpec(0).maxValue);
  bridge.edit(a, max + 1000.0);
  bridge.edit(0xFFFFFFFFu, 1.0);
  FakeOut fake;
  bridge.drain(&fake.out);
  REQUIRE(fake.events.size() == 1);
  REQUIRE(fake.events[0].value == max);
}

TEST_CASE("Overflow is retried, not dropped") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  const double lo = double(eg13::paramSpec(0).minValue);
  const double hi = double(eg13::paramSpec(0).maxValue);
  constexpr int kEdits = 2000;
  for (int i = 0; i < kEdits; ++i) bridge.edit(a, i % 2 == 0 ? lo : hi);
  REQUIRE(bridge.overflowSize() > 0);

  FakeOut fake;
  for (int guard = 0; guard < 10 && fake.events.size() < size_t(kEdits); ++guard) {
    bridge.drain(&fake.out);
    bridge.pump();
  }
  REQUIRE(fake.events.size() == size_t(kEdits));
  bool inOrder = true;
  for (int i = 0; i < kEdits; ++i) inOrder = inOrder && fake.events[size_t(i)].value == (i % 2 == 0 ? lo : hi);
  REQUIRE(inOrder);
  REQUIRE(bridge.overflowSize() == 0);
}

TEST_CASE("Destroying the bridge mid-gesture emits the end gesture") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  const uint32_t a = idAt(0);
  FakeOut fake;
  std::unique_ptr<UiBridge> bridge;
  bool destroying = false;
  // The destructor closes the gesture, then asks for a flush. This host flushes at once, while
  // the bridge is still alive, as a host that flushes synchronously on request would.
  UiBridge* raw = nullptr;  // unique_ptr::reset nulls its pointer before it destroys the object
  bridge = std::make_unique<UiBridge>(synth, [&] {
    if (destroying) raw->drain(&fake.out);
  });
  raw = bridge.get();
  bridge->beginEdit(a);
  bridge->edit(a, double(eg13::paramSpec(0).maxValue));
  fake.events.clear();
  bridge->drain(&fake.out);  // the begin and the value reach the host
  fake.events.clear();

  destroying = true;
  bridge.reset();
  REQUIRE(fake.events.size() == 1);
  REQUIRE(fake.events[0].type == CLAP_EVENT_PARAM_GESTURE_END);
  REQUIRE(fake.events[0].id == a);
}

TEST_CASE("A closed gesture is not closed again") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  bridge.beginEdit(a);
  bridge.endEdit(a);
  bridge.closeAllGestures();
  FakeOut fake;
  bridge.drain(&fake.out);
  REQUIRE(fake.events.size() == 2);  // begin, end: nothing extra
}

TEST_CASE("drain allocates nothing") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  const double hi = double(eg13::paramSpec(0).maxValue);
  bridge.beginEdit(a);
  for (int i = 0; i < 200; ++i) bridge.edit(a, hi);
  bridge.endEdit(a);
  FakeOut fake;
  size_t allocations = 0;
  {
    eg13::test::ScopedAllocationCounter counter;
    bridge.drain(&fake.out);
    allocations = counter.count();
  }
  REQUIRE(allocations == 0);
  REQUIRE(fake.events.size() == 202);
}

TEST_CASE("pump requests a flush when it moves overflowed events") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int flushes = 0;
  UiBridge bridge(synth, [&flushes] { ++flushes; });
  const uint32_t a = idAt(0);
  const double hi = double(eg13::paramSpec(0).maxValue);
  for (int i = 0; i < 1500; ++i) bridge.edit(a, hi);
  REQUIRE(bridge.overflowSize() > 0);
  REQUIRE(flushes == 1);  // coalesced
  FakeOut fake;
  bridge.drain(&fake.out);
  bridge.pump();
  REQUIRE(bridge.overflowSize() == 0);
  REQUIRE(flushes == 2);  // the pump asked, once
  bridge.pump();
  REQUIRE(flushes == 2);  // nothing moved, nothing asked
}

TEST_CASE("A gesture that overflows mid-stream stays balanced and in order") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  const double hi = double(eg13::paramSpec(0).maxValue);
  bridge.beginEdit(a);
  for (int i = 0; i < 2000; ++i) bridge.edit(a, hi);
  bridge.endEdit(a);
  REQUIRE(bridge.overflowSize() > 0);
  FakeOut fake;
  for (int guard = 0; guard < 10 && bridge.overflowSize() > 0; ++guard) {
    bridge.drain(&fake.out);
    bridge.pump();
  }
  bridge.drain(&fake.out);
  REQUIRE(fake.events.size() == 2002);
  REQUIRE(fake.events.front().type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  REQUIRE(fake.events.back().type == CLAP_EVENT_PARAM_GESTURE_END);
  size_t values = 0;
  for (size_t i = 1; i + 1 < fake.events.size(); ++i) values += fake.events[i].type == CLAP_EVENT_PARAM_VALUE;
  REQUIRE(values == 2000);
}

TEST_CASE("Nested begins and unmatched ends are ignored") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  const uint32_t a = idAt(0);
  bridge.endEdit(a);    // no open gesture
  bridge.beginEdit(a);
  bridge.beginEdit(a);  // already open
  bridge.endEdit(a);
  bridge.endEdit(a);    // already closed
  FakeOut fake;
  bridge.drain(&fake.out);
  REQUIRE(fake.events.size() == 2);
  REQUIRE(fake.events[0].type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  REQUIRE(fake.events[1].type == CLAP_EVENT_PARAM_GESTURE_END);
}

TEST_CASE("Refused host events are counted") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  UiBridge bridge(synth, {});
  bridge.edit(idAt(0), 0.0);
  clap_output_events_t refuse{};
  refuse.try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return false; };
  bridge.drain(&refuse);
  REQUIRE(bridge.droppedHostEvents() == 1);
}

TEST_CASE("A never-draining host gets no repeated pump requests") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int pumpRequests = 0;
  UiBridge bridge(synth, {}, [&pumpRequests] { ++pumpRequests; });
  const uint32_t a = idAt(0);
  for (int i = 0; i < 2000; ++i) bridge.edit(a, 0.0);
  REQUIRE(bridge.overflowSize() > 0);
  CHECK(pumpRequests == 1);  // coalesced: not one per event
  for (int i = 0; i < 10; ++i) bridge.pump();
  CHECK(pumpRequests == 1);
}

TEST_CASE("Drain asks for one pump while a backlog remains") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int pumpRequests = 0;
  UiBridge bridge(synth, {}, [&pumpRequests] { ++pumpRequests; });
  const uint32_t a = idAt(0);
  for (int i = 0; i < 3000; ++i) bridge.edit(a, 0.0);
  REQUIRE(pumpRequests == 1);
  FakeOut fake;
  bridge.drain(&fake.out);
  CHECK(pumpRequests == 2);  // one request for the drain
  bridge.drain(&fake.out);
  CHECK(pumpRequests == 2);  // flag already consumed: no repeat
  bridge.pump();
  CHECK(pumpRequests == 2);  // pump never asks
  bridge.drain(&fake.out);
  CHECK(pumpRequests == 3);  // pump left a backlog (3000 > 2 queues), so this drain asks again
  bridge.pump();
  bridge.drain(&fake.out);
  bridge.pump();
  CHECK(bridge.overflowSize() == 0);
  const int settled = pumpRequests;
  bridge.drain(&fake.out);
  bridge.pump();
  CHECK(pumpRequests == settled);  // no backlog: nothing asks
}

TEST_CASE("A detached bridge's drain does not call the pump hook") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int pumpRequests = 0;
  UiBridge bridge(synth, {}, [&pumpRequests] { ++pumpRequests; });
  const uint32_t a = idAt(0);
  for (int i = 0; i < 2000; ++i) bridge.edit(a, 0.0);
  bridge.detach();
  FakeOut fake;
  bridge.drain(&fake.out);
  CHECK(pumpRequests == 1);
}

TEST_CASE("A detached bridge asks the host for nothing, even when gestures close") {
  eg13::Synth synth(eg13::PluginKind::Instrument);
  int flushes = 0, pumps = 0;
  UiBridge bridge(synth, [&flushes] { ++flushes; }, [&pumps] { ++pumps; });
  const uint32_t a = idAt(0);
  bridge.beginEdit(a);
  const int before = flushes;
  bridge.detach();
  bridge.closeAllGestures();  // what destroying the editor does
  for (int i = 0; i < 2000; ++i) bridge.edit(a, 0.0);  // overflow would ask for a pump
  CHECK(flushes == before);
  CHECK(pumps == 0);
}
