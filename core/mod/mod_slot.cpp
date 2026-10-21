#include "mod/mod_slot.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace eg13 {

ModSlot::ModSlot(std::vector<std::unique_ptr<ModEngine>> engines) : engines_(std::move(engines)) {
  if (engines_.empty()) engines_.push_back(std::make_unique<OffModEngine>());
}

void ModSlot::prepare(double hostRate) {
  for (auto& e : engines_) e->prepare(hostRate);
  reset();
}

void ModSlot::setSeed(uint32_t seed) {
  for (auto& e : engines_) e->setSeed(seed);
}

void ModSlot::reset() {
  for (auto& e : engines_) e->reset();
  std::fill(std::begin(out_), std::end(out_), 0.0f);
}

void ModSlot::restart() {
  engines_[size_t(current_)]->reset();
  std::fill(std::begin(out_), std::end(out_), 0.0f);
}

void ModSlot::setEngine(int engine) {
  engine = std::clamp(engine, 0, engineCount() - 1);
  if (engine == current_) return;
  current_ = engine;
  restart();
}

void ModSlot::process(const ModContext& ctx, const ParamValues& p, const EnvelopeShape& shape, int n) {
  ModEngine& e = *engines_[size_t(current_)];
  e.setShape(shape);
  e.process(ctx, p, n, out_);
  for (float v : out_) {
    if (!std::isfinite(v)) {
      e.reset();
      std::fill(std::begin(out_), std::end(out_), 0.0f);
      ++nanResets_;
      return;
    }
  }
}

}  // namespace eg13
