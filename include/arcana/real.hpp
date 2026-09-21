#pragma once

namespace arcana {

// Simulation scalar. `double` everywhere by default (PC, Switch, Vita: unchanged behaviour).
// The PSP builds with ARCANA_REAL_FLOAT: its Allegrex CPU has a single-precision FPU only and
// emulates `double` in software (~100x slower in the CPU probe). Pair it with
// -fsingle-precision-constant so unsuffixed literals don't drag expressions back to double.
#ifdef ARCANA_REAL_FLOAT
using real = float;
#else
using real = double;
#endif

} // namespace arcana
