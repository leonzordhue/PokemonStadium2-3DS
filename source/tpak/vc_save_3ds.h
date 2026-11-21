// Pokemon Stadium 2 (3DS) - Transfer Pak: Game Boy save read/written straight
// in the save data of an installed 3DS Virtual Console title (Red/Blue/Yellow,
// Gold/Silver/Crystal), like the Pokemon Stadium 3DS port does.
//
// Flow (same file layout as tpak_3ds.h):
//   vc_find(game)            -> opens ARCHIVE_USER_SAVEDATA of the VC title
//   vc_read(buf)             -> whole /sav.dat (SRAM + VC footer, if any)
//   vc_write_pending(l, buf) -> <base>/gb/saves/<name>-VC-<crc32>.sav.pending
//   vc_commit(l)             -> pending SRAM into /sav.dat, archive commit,
//                               then pending renamed to <...>-VC-<crc32>.sav
// A .pending found at start means the last VC write was cut: call vc_commit
// again (it is idempotent).
//
// Only the first sram_size bytes of /sav.dat are ever written: the VC
// emulator's own footer (Gen 2 clock state) is left as the VC wrote it.
// Host builds simulate the archive as <sim_root>/<title id>/sav.dat.
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tpak::vc {
enum class Game { Red, Blue, Yellow, Gold, Silver, Crystal };

struct Title {
    uint64_t id;          // 00040000 0017xx00
    Game game;
    const char* lang;     // ENG = USA/EUR/AUS English (one title, region "A")
    const char* product;  // CTR-N-<code>
};
extern const Title titles[];
extern const size_t title_count;

constexpr size_t sram_size = 0x8000;  // international GB/GBC cart SRAM

// <base>/gb/saves/<name>-VC-<CRC32>.sav  (CRC32 of the GB ROM, as tpak backups)
std::string local_path(const std::string& base, const std::string& name, uint32_t rom_crc);

// Finds the first installed VC title of `game` whose /sav.dat opens
// (English first, then GER/FRA/SPN/ITA, JPN/KOR last). False if none.
bool vc_find(Game game);
bool vc_find_id(uint64_t title_id);  // for "pN_save=vc <titleid>" in the cfg
uint64_t found_id();

bool vc_read(std::vector<uint8_t>& buf);

// P45: copies the Game Boy ROM out of the installed VC title of `game` (the
// title's own RomFS, libctru romfsMountFromTitle) to out_path, so the S1-style
// scan finds a game that is only installed as VC. False if no title or no ROM.
bool vc_extract_rom(Game game, const std::string& out_path);
bool vc_write_pending(const std::string& local, const std::vector<uint8_t>& buf);
bool vc_commit(const std::string& local);

#ifndef __3DS__
void sim_root(const std::string& dir);  // host test only
#endif
}
