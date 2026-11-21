// s2-3ds: symbols the Stadium 2 code links against that upstream N64ModernRuntime lacks.
// __osSiRawStartDma joybus HLE ported from mstan/N64ModernRuntime f61d5424
// librecomp/src/si_joybus.cpp (GPL-3.0, (c) 2026 Matthew Stanley); librecomp::gbcart -> tpak (s2-3ds/tpak).
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "librecomp/game.hpp"
#include "tpak_3ds.h"


extern "C" volatile uint32_t s2_last_loop_pc = 0;

// Fork diagnostic ring (register_overlays.cpp trampolines call it): no-op on 3DS.
extern "C" void recomp_ultra_trace_record(const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}

// s32 osPiReadIo(u32 devAddr, u32* data): one 32-bit PIO read from the cart.
extern "C" void osPiReadIo_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dev = (uint32_t)ctx->r4;
    if (dev < 0x10000000u) dev |= 0x10000000u; // ponytail: assumes cart domain; other PI domains unsupported
    recomp::do_rom_pio(rdram, ctx->r5, dev);
    ctx->r2 = 0;
}


namespace {
    // Joybus command bytes.
    constexpr uint8_t JOY_INFO  = 0x00; // device type + status (3 rx)
    constexpr uint8_t JOY_STATE = 0x01; // controller buttons (4 rx)
    constexpr uint8_t JOY_READ  = 0x02; // read 32-byte accessory block (+ crc)
    constexpr uint8_t JOY_WRITE = 0x03; // write 32-byte accessory block (-> crc)

    // Flag set in the rxsize byte of a response when no device answered.
    constexpr uint8_t RX_NO_DEVICE = 0x80;

    // Standard N64 8-bit accessory data CRC (libultra __osContDataCrc, poly 0x85).
    uint8_t data_crc(const uint8_t* data) {
        uint32_t crc = 0;
        for (int i = 0; i <= 32; i++) {
            for (int mask = 0x80; mask != 0; mask >>= 1) {
                const uint32_t xorv = (crc & 0x80) ? 0x85u : 0u;
                crc <<= 1;
                if (i < 32 && (data[i] & mask)) {
                    crc |= 1;
                }
                crc ^= xorv;
            }
        }
        return static_cast<uint8_t>(crc);
    }

    // Always-on (env-gated) joybus trace. Set PSR_JOYBUS_TRACE=1 to dump every
    // 64-byte PIF block the shim processes + each parsed frame. Continuous from
    // process start, so the boot-time Transfer Pak detect is captured.
    bool joybus_trace() {
        static const bool on = []{
            const char* e = std::getenv("PSR_JOYBUS_TRACE");
            return e != nullptr && e[0] != '\0' && e[0] != '0';
        }();
        return on;
    }

    void dump_block(const char* tag, const uint8_t* blk) {
        std::fprintf(stderr, "[joybus] %s block:", tag);
        for (int i = 0; i < 64; i++) {
            if ((i & 15) == 0) std::fprintf(stderr, "\n[joybus]  %02x:", i);
            std::fprintf(stderr, " %02x", blk[i]);
        }
        std::fprintf(stderr, "\n");
    }

    // Parse the 64-byte PIF command block at `pif` in RDRAM and fill the accessory
    // responses in place. Channel index = N64 controller port (0..3) = gbcart port.
    void run_joybus(uint8_t* rdram, gpr pif) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            blk[i] = MEM_BU(i, pif);
        }

        const bool trace = joybus_trace();
        if (trace) {
            dump_block("rx-in", blk);
        }

        int chan = 0;
        int p = 0;
        while (p < 63) {
            const uint8_t t = blk[p];
            if (t == 0xFE) break;                       // end of commands
            if (t == 0xFF) { p++; continue; }           // pad / format byte
            if (t == 0x00) { p++; chan++; continue; }   // skip this channel
            if (t == 0xFD) { p++; continue; }           // channel reset

            const int tx = t & 0x3F;
            const int rx = blk[p + 1] & 0x3F;
            const int cmd_i = p + 2;
            const int rx_i = p + 2 + tx;
            if (rx_i + rx > 64) break;                  // malformed frame
            if (chan < 0 || chan > 3) { p = rx_i + rx; chan++; continue; }

            const uint8_t cmd = blk[cmd_i];
            bool no_device = false;

            if (cmd == JOY_INFO) {
                // Standard controller type (0x0500) + status; pak-present bit from
                // the shared cart model so the game's Transfer Pak detect succeeds.
                if (rx >= 3) {
                    blk[rx_i + 0] = 0x05;
                    blk[rx_i + 1] = 0x00;
                    blk[rx_i + 2] = tpak::has_pak(chan) ? 0x01 : 0x00;
                }
            } else if ((cmd == JOY_READ && tx < 3) || (cmd == JOY_WRITE && tx < 35)) {
                // 0024: frame curto demais para o comando; antes lia bytes de fora do frame
                // (ou além de blk[63]) e mandava lixo para tpak::write_block.
                static int short_warn = 0;
                if (short_warn++ < 8)
                    std::fprintf(stderr, "[joybus] short frame: cmd=0x%02x tx=%d chan=%d\n", cmd, tx, chan);
                no_device = true;
            } else if (cmd == JOY_READ) {
                // tx = [cmd][addr_hi][addr_lo]; rx = 32 data + 1 crc.
                const uint16_t addr = static_cast<uint16_t>((blk[cmd_i + 1] << 8) | blk[cmd_i + 2]);
                std::array<uint8_t, 32> data{};
                const int ret = tpak::read_block(chan, static_cast<uint16_t>(addr >> 5), data.data());
                if (ret == 0 && rx >= 33) {
                    for (int i = 0; i < 32; i++) {
                        blk[rx_i + i] = data[i];
                    }
                    blk[rx_i + 32] = data_crc(data.data());
                } else {
                    no_device = true;
                }
            } else if (cmd == JOY_WRITE) {
                // tx = [cmd][addr_hi][addr_lo][32 data]; rx = 1 crc.
                const uint16_t addr = static_cast<uint16_t>((blk[cmd_i + 1] << 8) | blk[cmd_i + 2]);
                std::array<uint8_t, 32> data{};
                for (int i = 0; i < 32; i++) {
                    data[i] = blk[cmd_i + 3 + i];
                }
                const int ret = tpak::write_block(chan, static_cast<uint16_t>(addr >> 5), data.data());
                if (ret == 0 && rx >= 1) {
                    blk[rx_i + 0] = data_crc(data.data());
                } else {
                    no_device = true;
                }
            } else if (cmd == JOY_STATE) {
                // Should be HLE'd at osContStartReadData; answer idle if reached.
                for (int i = 0; i < rx; i++) {
                    blk[rx_i + i] = 0;
                }
            }

            if (no_device) {
                blk[p + 1] = static_cast<uint8_t>(blk[p + 1] | RX_NO_DEVICE);
            }

            if (trace) {
                std::fprintf(stderr,
                    "[joybus]  frame p=%d chan=%d cmd=0x%02x tx=%d rx=%d has_pak=%d no_device=%d\n",
                    p, chan, cmd, tx, rx, tpak::has_pak(chan) ? 1 : 0, no_device ? 1 : 0);
            }

            p = rx_i + rx;
            chan++;
        }

        for (int i = 0; i < 64; i++) {
            MEM_B(i, pif) = static_cast<int8_t>(blk[i]);
        }

        if (trace) {
            dump_block("tx-out", blk);
        }
    }
}

// void __osSiRawStartDma(s32 direction, void* dramAddr)
//   direction OS_READ(0): the joybus responses are about to be copied PIF->RDRAM.
//   We process the command block (still in the RDRAM buffer from the preceding
//   OS_WRITE) and fill the responses in place. OS_WRITE(1) is a no-op for HLE.
extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    const s32 direction = _arg<0, s32>(rdram, ctx);
    const gpr dram_addr = ctx->r5;
    if (direction == 0 /* OS_READ */) {
        run_joybus(rdram, dram_addr);
    }
    // The real SI DMA raises an SI interrupt on completion; the game does
    // osRecvMesg on the SI event queue after EVERY __osSiRawStartDma â€” both the
    // write that sends the command and the read that receives the response.
    // Deliver that completion message (as osContStartReadData does); without it
    // osRecvMesg blocks forever and the boot-time accessory probe soft-locks.
    ultramodern::send_si_message();
    _return<s32>(ctx, 0);
}
