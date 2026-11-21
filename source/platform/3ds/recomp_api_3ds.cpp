// Pokemon Stadium 2 host functions for the 3DS.
//
// What the generated code calls and neither the generated code nor the
// upstream N64ModernRuntime defines (measured 19/11/2026, 76 external
// symbols, 8 missing upstream):
//   recomp_handle_tailcalls                      -> compat/ (Frente A)
//   __osContRamRead/__osContRamWrite,
//   __osPfsGetStatus, osPfsIsPlug                -> tpak/tpak_hle_3ds.cpp (the Transfer Pak
//                                                   rides the controller-pak bus)
//   func_80064980, static_5_8000B770,
//   static_272_8334B334                          -> stubs below (N64Recomp emitted
//                                                   calls but no body)
// The stubs are weak: compat/ or tpak/ replace them with real versions.
#include <cstdio>
#include <cinttypes>

#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

namespace {
[[noreturn]] void s2_missing(const char* name, recomp_context* ctx) {
    fprintf(stderr, "[S2] missing function %s (ra=%08" PRIX32 ")\n", name, (uint32_t)ctx->r31);
    ultramodern::quit();
    for (;;) {}
}
}

#define S2_STUB(name) \
    extern "C" __attribute__((weak)) void name(uint8_t* rdram, recomp_context* ctx) { (void)rdram; s2_missing(#name, ctx); }

// No body in the generated code (N64Recomp could not recompile them).
S2_STUB(func_80064980)
S2_STUB(static_5_8000B770)
S2_STUB(static_272_8334B334)

// Controller pak bus: tpak/tpak_hle_3ds.cpp (the Transfer Pak).

// The desktop's error/print exports, if the game ever reaches them.
extern "C" void recomp_puts(uint8_t* rdram, recomp_context* ctx) {
    const gpr str = ctx->r4;
    const uint32_t len = (uint32_t)ctx->r5;
    for (uint32_t i = 0; i < len; i++) fputc(MEM_B(i, str), stderr);
}
