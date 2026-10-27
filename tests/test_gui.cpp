// Built only with the Swift UI (EG13_HAS_UI): the editor window the CLAP gui extension hosts.
#include <catch2/catch_test_macros.hpp>

#include <objc/message.h>
#include <objc/runtime.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core_api.h"
#include "eg13/ui_entry.h"
#include "gui.h"
#include "params/param_ids.h"
#include "synth/synth.h"
#include "ui_bridge.h"

extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void*);

namespace {

// The editor reads the app-wide settings file; the tests point it at a path that does not exist,
// so a developer's own settings cannot change the sizes they check.
const bool kSettingsRedirected = [] {
  setenv("EG13_SETTINGS_PATH", "/nonexistent/eg13-test/settings.json", 1);
  return true;
}();

// A plain NSView standing in for the host's parent view (+1, the caller releases it).
void* newView() {
  using New = void* (*)(id, SEL);
  return reinterpret_cast<New>(objc_msgSend)(reinterpret_cast<id>(objc_getClass("NSView")),
                                             sel_registerName("new"));
}
void releaseView(void* view) {
  using Release = void (*)(id, SEL);
  reinterpret_cast<Release>(objc_msgSend)(reinterpret_cast<id>(view), sel_registerName("release"));
}

struct Fixture {
  Fixture() : bridge(synth, {}), ctx{synth, bridge, {}}, api(eg13::makeCoreApi(ctx)) {
    eg13::attachFactoryPresets(api);  // as the plugin does
  }
  eg13::Synth synth{eg13::PluginKind::Instrument};
  eg13::UiBridge bridge;
  eg13::CoreApiContext ctx;
  EG13CoreAPI api;
};

}  // namespace

TEST_CASE("adjust_size keeps the aspect and clamps", "[gui]") {
  uint32_t w = 100, h = 100;
  eg13::adjustGuiSize(w, h);
  CHECK(w == 900);
  CHECK(h == 570);

  w = 5000;
  h = 1000;
  eg13::adjustGuiSize(w, h);
  CHECK(w == 2400);
  CHECK(h == 1520);

  w = 1300;
  h = 900;
  eg13::adjustGuiSize(w, h);
  CHECK(w * 760 == h * 1200);
  CHECK(w >= 900);
  CHECK(w <= 2400);

  w = 1200;
  h = 760;
  eg13::adjustGuiSize(w, h);
  CHECK(w == 1200);
  CHECK(h == 760);
}

TEST_CASE("Editor create/destroy 50 times leaves no live UI objects", "[gui]") {
  Fixture f;
  int flushes = 0;
  eg13::Gui gui(f.api, 0, f.bridge, [&flushes] { ++flushes; });
  REQUIRE(eg13_ui_live_count() == 0);
  for (int i = 0; i < 50; ++i) {
    void* pool = objc_autoreleasePoolPush();
    void* parent = newView();
    REQUIRE(gui.create());
    REQUIRE(gui.setParent(parent));  // attaching shows the editor and starts its display timer
    gui.setSize(1500, 950);
    gui.setVisible(false);
    gui.setVisible(true);
    gui.destroy();
    releaseView(parent);
    objc_autoreleasePoolPop(pool);
    REQUIRE(eg13_ui_live_count() == 0);
  }
  CHECK(flushes == 50);
}

TEST_CASE("Closing the editor ends its open gestures, and the bridge outlives it", "[gui]") {
  Fixture f;
  eg13::Gui gui(f.api, 0, f.bridge, {});
  REQUIRE(gui.create());
  CHECK(gui.width() == 1200);
  CHECK(gui.height() == 760);
  const uint32_t id = eg13::params::id(0, eg13::params::Slot::Source, 0);
  f.api.begin_edit(f.api.context, id);
  gui.destroy();
  CHECK_FALSE(gui.exists());

  std::vector<uint16_t> types;
  clap_output_events_t out{};
  out.ctx = &types;
  out.try_push = [](const clap_output_events_t* o, const clap_event_header_t* e) {
    static_cast<std::vector<uint16_t>*>(o->ctx)->push_back(e->type);
    return true;
  };
  f.bridge.drain(&out);  // the normal process/flush path
  REQUIRE(types.size() == 2);
  CHECK(types[0] == CLAP_EVENT_PARAM_GESTURE_BEGIN);
  CHECK(types[1] == CLAP_EVENT_PARAM_GESTURE_END);
}

TEST_CASE("set_size clamps an out-of-range size", "[gui]") {
  Fixture f;
  eg13::Gui gui(f.api, 0, f.bridge, {});
  REQUIRE(gui.create());
  gui.setSize(0, 0);
  CHECK(gui.width() == 900);
  CHECK(gui.height() == 570);
  gui.setSize(9999, 9999);
  CHECK(gui.width() == 2400);
  CHECK(gui.height() == 1520);
}

TEST_CASE("A host that accepts request_resize without calling set_size still resizes the editor", "[gui]") {
  Fixture f;
  uint32_t askedWidth = 0, askedHeight = 0;
  bool accept = true;
  eg13::Gui gui(f.api, 0, f.bridge, {}, [&](uint32_t w, uint32_t h) {
    askedWidth = w;  // the host records the request and, like a native CLAP host may, never calls set_size
    askedHeight = h;
    return accept;
  });
  REQUIRE(gui.create());

  CHECK(gui.requestResize(1800, 1140));
  CHECK(askedWidth == 1800);
  CHECK(askedHeight == 1140);
  CHECK(gui.width() == 1800);
  CHECK(gui.height() == 1140);

  // An off-aspect request is clamped to the editor's aspect before the host is asked.
  CHECK(gui.requestResize(100, 100));
  CHECK(askedWidth == 900);
  CHECK(gui.width() == 900);
  CHECK(gui.height() == 570);

  // A refusal leaves the size alone.
  accept = false;
  CHECK_FALSE(gui.requestResize(2400, 1520));
  CHECK(gui.width() == 900);
  CHECK(gui.height() == 570);
}

TEST_CASE("Without a host resize hook the request is refused", "[gui]") {
  Fixture f;
  eg13::Gui gui(f.api, 0, f.bridge, {});
  REQUIRE(gui.create());
  CHECK_FALSE(gui.requestResize(1800, 1140));
  CHECK(gui.width() == 1200);
}

TEST_CASE("A new editor opens at the settings' default size", "[gui]") {
  const std::string path = std::string(::getenv("TMPDIR") ? ::getenv("TMPDIR") : "/tmp") + "/eg13-gui-settings.json";
  {
    std::FILE* file = std::fopen(path.c_str(), "w");
    REQUIRE(file != nullptr);
    std::fputs("{\"schemaVersion\":1,\"defaultWindowPercent\":150,\"modulationDisplay\":true}", file);
    std::fclose(file);
  }
  // Its own settings path for the test, and the previous one put back after, so the other [gui]
  // tests in the same process are unaffected.
  const char* previous = ::getenv("EG13_SETTINGS_PATH");
  const std::string saved = previous ? previous : "";
  const bool hadPrevious = previous != nullptr;
  struct Restore {
    const std::string& saved;
    bool had;
    ~Restore() {
      if (had) setenv("EG13_SETTINGS_PATH", saved.c_str(), 1);
      else unsetenv("EG13_SETTINGS_PATH");
    }
  } restore{saved, hadPrevious};
  setenv("EG13_SETTINGS_PATH", path.c_str(), 1);
  CHECK(eg13_ui_default_zoom() == 1.5);
  Fixture f;
  eg13::Gui gui(f.api, 0, f.bridge, {});
  REQUIRE(gui.create());
  CHECK(gui.width() == 1800);
  CHECK(gui.height() == 1140);
  gui.destroy();
  setenv("EG13_SETTINGS_PATH", "/nonexistent/eg13-test/settings.json", 1);
  std::remove(path.c_str());
  REQUIRE(gui.create());
  CHECK(gui.width() == 1200);  // missing file: 100 %
  gui.destroy();
}
