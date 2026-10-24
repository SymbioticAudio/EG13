// Built only when the Swift UI is part of the build (EG13_HAS_UI). Calling into it proves the
// static library, the Swift runtime and SwiftUI link into the same binary the plugins use.
#include <catch2/catch_test_macros.hpp>

#include "eg13/ui_entry.h"

TEST_CASE("The Swift UI links into the plugin", "[ui]") {
  const int32_t linked = eg13_ui_link_check();
  REQUIRE(linked == 1);
}
