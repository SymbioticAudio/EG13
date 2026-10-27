// Name lookups over the engine tables, shared by the build tools (eg13_ui_tables dumps them,
// eg13_preset resolves preset sources through them): the strings a P shows along its sweep, and the
// engine and P a name stands for. Header-only; the tools link eg13_core.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "synth/param_table.h"

namespace eg13::tools {

// Value strings are collected along 0..1 in this many steps.
constexpr int kSweepSteps = 1000;

// Every string the P shows, in the order the sweep first reaches them.
inline std::vector<std::string> sweepStrings(const EngineParamInfo& param) {
  std::vector<std::string> ordered;
  std::set<std::string> seen;
  for (int i = 0; i <= kSweepSteps; ++i) {
    char buffer[64] = {};
    if (param.format) param.format(float(i) / float(kSweepSteps), buffer, sizeof buffer);
    buffer[sizeof buffer - 1] = '\0';
    if (seen.insert(buffer).second) ordered.emplace_back(buffer);
  }
  return ordered;
}

inline bool sameName(const std::string& a, const char* b) {
  const size_t n = std::strlen(b);
  if (a.size() != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  }
  return true;
}

// The value (0..1) that makes the P show `option`: the middle of the first stretch of the sweep
// that shows it. False if the P never shows it.
inline bool valueForOption(const EngineParamInfo& param, const std::string& option, float* value) {
  int first = -1, last = -1;
  for (int i = 0; i <= kSweepSteps; ++i) {
    char buffer[64] = {};
    if (param.format) param.format(float(i) / float(kSweepSteps), buffer, sizeof buffer);
    buffer[sizeof buffer - 1] = '\0';
    if (sameName(option, buffer)) {
      if (first < 0) first = i;
      last = i;
    } else if (first >= 0) {
      break;
    }
  }
  if (first < 0) return false;
  *value = float(first + last) * 0.5f / float(kSweepSteps);
  return true;
}

// The engine of a slot with that display name, or -1.
inline int engineByName(SlotKind kind, const std::string& name) {
  for (int e = 0; e < slotEngineCount(kind); ++e) {
    if (sameName(name, slotEngineInfo(kind, e).displayName)) return e;
  }
  return -1;
}

// A P no engine function reads ("(unused)" fills the table; several can share the name).
inline bool isUnusedParam(const EngineParamInfo& param) { return std::strcmp(param.name, "(unused)") == 0; }

// The engine's P with that name, or -1. Unused P's have no name to find.
inline int paramByName(const EngineInfo& engine, const std::string& name) {
  for (int p = 0; p < engine.paramCount; ++p) {
    if (!isUnusedParam(engine.params[p]) && sameName(name, engine.params[p].name)) return p;
  }
  return -1;
}

}  // namespace eg13::tools
