# Eurorack patches

`third_party/eurorack` is compiled unmodified. When an upstream file cannot be
used as-is, EG13 ships a shadow copy here. This directory comes first on the
include path, so `#include "stmlib/..."` finds the shadow file, and the
build compiles the shadow `.cc` instead of the upstream one.

| File | Upstream | Change | Why |
|---|---|---|---|
| `stmlib/utils/random.h`, `random.cc` | `stmlib/utils/random.{h,cc}` @ eurorack f4cfa76 | `rng_state_` is `thread_local` | Upstream keeps one global generator state. EG13 instances processed on different host threads would race on it (undefined behaviour). |
| `clouds/dsp/window.h` | `clouds/dsp/window.h` @ eurorack f4cfa76 | `Window::Start()` sets `done_ = false` again | Upstream commits fbb53ba and 0e3756f each removed one of two duplicate `done_ = false;` lines; merge d1d8839 kept both removals. `Init()` sets `done_ = true`, so Stretch's WSOLA windows never played: digital silence. VCV's AudibleInstruments pins an older eurorack copy that still has the line. The bare-module reference for Stretch in the fidelity tests includes this fix (it is compiled through the same shadow), so the adapters stay bit-exact against it. |
