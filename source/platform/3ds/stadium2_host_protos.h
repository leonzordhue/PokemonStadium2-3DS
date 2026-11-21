// Force-included into RecompiledFuncs only (CMakeLists.txt).
#ifndef STADIUM2_HOST_PROTOS_H
#define STADIUM2_HOST_PROTOS_H

#include "recomp.h"

// Stadium 2 has no RecompiledPatches, so nothing needs the originals weak;
// keep GCC's noipa (one function's identity per symbol) and rounding-math.
#undef RECOMP_FUNC
#define RECOMP_FUNC __attribute__((noipa, optimize("rounding-math")))

#endif
