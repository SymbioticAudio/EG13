#pragma once

namespace eg13 {

// The process's shared random generators, which the module ports draw from: stmlib::Random
// (Braids, Plaits, Elements, Rings, Clouds, Beads, ...; per thread) and rack::random (the VCV
// models; one per process). reseedSharedRandom() puts both back to their start values, so an
// offline render that starts with it is a pure function of its input, whatever ran before it in
// the process. Main thread, never while audio runs (offline renders, tests).
void reseedSharedRandom();

}  // namespace eg13
