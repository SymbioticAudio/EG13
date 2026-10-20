# Eurorack patches

`third_party/eurorack` is compiled unmodified. When an upstream file cannot be
used as-is, EG13 ships a shadow copy here. This directory comes first on the
include path, so `#include "stmlib/..."` finds the shadow file, and the
build compiles the shadow `.cc` instead of the upstream one.

| File | Upstream | Change | Why |
|---|---|---|---|
| `stmlib/utils/random.h`, `random.cc` | `stmlib/utils/random.{h,cc}` @ eurorack f4cfa76 | `rng_state_` is `thread_local` | Upstream keeps one global generator state. EG13 instances processed on different host threads would race on it (undefined behaviour). |
