// Pokemon Stadium 2 (3DS) - libultra accessory HLE for the Transfer Pak.
//
// The Stadium 2 code (fork N64Recomp, game.toml) calls these four as host
// functions; the upstream runtime the 3DS build uses (oneup03) has none of
// them. Bodies ported from mstan/N64ModernRuntime librecomp/src/gbpak.cpp and
// pak.cpp (commit f61d5424, Copyright (c) 2026 Matthew Stanley, GPL-3.0),
// without its trace ring, routed to tpak_3ds.cpp. Replaces the "no pak"
// stubs of s2-3ds/compat.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "tpak_3ds.h"

namespace {
// A real accessory read/write is two SI DMAs; the game's osRecvMesg ordering
// around it must hold. The upstream runtime has no delayed external message,
// so post the completion and take it at once (one per DMA).
// ponytail: zero-latency DMA; add a timed message if a game races on it.
void si_dma_pair(uint8_t* rdram, PTR(OSMesgQueue) mq) {
    if (mq == NULLPTR || !ultramodern::is_game_thread()) return;
    for (int i = 0; i < 2; i++) {
        // Receive only what we sent: a full queue must not eat a message of the game (D7 A11).
        if (osSendMesg(PASS_RDRAM mq, (OSMesg)0, OS_MESG_NOBLOCK) == 0)
            osRecvMesg(PASS_RDRAM mq, NULLPTR, OS_MESG_BLOCK);
    }
}
}  // namespace

// s32 __osContRamRead(OSMesgQueue* mq, int channel, u16 address, u8* buffer)
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    const PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    const int channel = _arg<1, s32>(rdram, ctx);
    const uint16_t address = _arg<2, u16>(rdram, ctx);
    const gpr buffer = ctx->r7;
    uint8_t block[tpak::block_size];
    si_dma_pair(rdram, mq);
    const int ret = tpak::read_block(channel, address, block);
    if (ret == 0)
        for (int i = 0; i < tpak::block_size; i++) MEM_B(i, buffer) = static_cast<int8_t>(block[i]);
    _return<s32>(ctx, ret);
}

// s32 __osContRamWrite(OSMesgQueue* mq, int channel, u16 address, u8* buffer, int force)
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    const PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    const int channel = _arg<1, s32>(rdram, ctx);
    const uint16_t address = _arg<2, u16>(rdram, ctx);
    const gpr buffer = ctx->r7;
    uint8_t block[tpak::block_size];
    for (int i = 0; i < tpak::block_size; i++) block[i] = MEM_BU(i, buffer);
    si_dma_pair(rdram, mq);
    _return<s32>(ctx, tpak::write_block(channel, address, block));
}

// s32 __osPfsGetStatus(OSMesgQueue* mq, int channel): 0 = pak present, 1 = none
extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    const PTR(OSMesgQueue) mq = _arg<0, PTR(OSMesgQueue)>(rdram, ctx);
    const int channel = _arg<1, s32>(rdram, ctx);
    si_dma_pair(rdram, mq);
    _return<s32>(ctx, tpak::has_pak(channel) ? 0 : 1);
}

// s32 osPfsIsPlug(OSMesgQueue* mq, u8* pattern): bit n = accessory on port n
extern "C" void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    const PTR(u8) pattern_out = _arg<1, PTR(u8)>(rdram, ctx);
    if (pattern_out != NULLPTR) {
        u8 pattern = 0;
        for (int port = 0; port < tpak::port_count; port++)
            if (tpak::has_pak(port)) pattern |= static_cast<u8>(1 << port);
        MEM_B(0, pattern_out) = static_cast<int8_t>(pattern);
    }
    _return<s32>(ctx, 0);
}
