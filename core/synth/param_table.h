#pragma once

#include <cstddef>
#include <cstdint>

namespace eg13 {

// M0 parameter list: Part A Source P1-P8 (Macro Oscillator) and master level.
constexpr size_t kSourceParamCount = 8;
constexpr size_t kMasterLevelIndex = 8;
constexpr size_t kParamCount = 9;

struct ParamSpec {
  uint32_t id;
  const char* name;
  const char* module;
  float defaultValue;
};

const ParamSpec& paramSpec(size_t index);  // index < kParamCount
int paramIndex(uint32_t id);               // -1 if unknown
void formatParam(size_t index, double value, char* buf, size_t size);

}  // namespace eg13
