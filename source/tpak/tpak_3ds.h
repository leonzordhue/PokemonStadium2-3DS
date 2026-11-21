// Pokemon Stadium 2 (3DS) - Transfer Pak: Game Boy cart model + save handling.
//
// Cart model (MBC1/3/5, accessory registers, open-bus padding, status reset
// latch) ported from librecomp/src/gbcart.cpp of mstan/N64ModernRuntime
// (commit f61d5424, Copyright (c) 2026 Matthew Stanley, GPL-3.0), itself from
// mstan/PokemonStadiumRecomp's transfer_pak.cpp. Added here for the 3DS:
// MBC3 real-time clock (Gold/Silver/Crystal), saves written only when the
// game's own checksum is valid, atomic writes, -original/-last backups and
// .pending recovery, following the Pokemon Stadium 3DS file layout:
//   <base>/gb/saves/<name>.sav
//   <base>/gb/backup/<name>-<crc32>-original.sav   (first time a save is seen)
//   <base>/gb/backup/<name>-<crc32>-last.sav       (every session start)
//   <base>/gb/saves/<name>-VC-<crc32>.sav(.pending) (SD copy of a 3DS VC save,
//   vc_save_3ds.h; the VC title is written after the SD copy)
// No runtime dependency: builds and tests on the host (VC archive simulated).
#pragma once
#include <cstdint>
#include <string>

namespace tpak {
constexpr int block_size = 32;   // libultra accessory block
constexpr int port_count = 4;

// Base folder: "sdmc:/3ds/PokemonStadium2" on the console.
void set_base_dir(const std::string& dir);

// Mount the cart (ROM + save) in the Transfer Pak of `port` (0..3).
// Empty rom = remove. Returns false and sets status() on failure.
bool set_cart(int port, const std::string& rom, const std::string& save);
bool has_pak(int port);

// Reads <base>/transferpak.cfg (p1_rom=, p1_save=, p2_rom=, ...) and mounts
// the carts. Relative paths are relative to the base folder. p<n>_save=vc
// [title id] uses the installed 3DS VC save; without p<n>_save the VC of that
// game is tried first, then gb/saves/<rom name>.sav.
void load_config();

// libultra block I/O: 0 = ok, 1 = no pak, 5 = invalid.
int read_block(int port, uint16_t block_address, uint8_t* out);
int write_block(int port, uint16_t block_address, const uint8_t* data);

// Writes cart RAM that changed and passes the game's checksum. Periodic
// calls (force=false) wait until the game stopped writing for 1 s; quit and
// cart removal use force=true.
void flush_all(bool force = false);

// Last user-facing message (S1 wording, English), e.g. "Cartridge connected".
const std::string& status();

// Unplugs every pak (saves flushed), then load_config() again: picks up an
// edited transferpak.cfg, a newly installed VC save and (patch 0025) the scan.
void search_saves();

// Touch-screen menu rows (S1: "Transfer Pak n", "Tap again to disconnect",
// "Search saves"); main_3ds.cpp makes a recomp3ds::MenuOption of each, with
// value = &menu_value[row], 2 names, on_change calling menu_changed(row, v).
// Pak row (0..port_count-1): 0 shows "<ROM>/<save>" or "No cartridge"; a
// step onto 1 shows "Again: unplug", the next step unplugs. Row port_count:
// "Search saves", any step searches. The first call per row (the one after
// settings.ini loads) does nothing.
constexpr int menu_rows = port_count + 1;
extern int menu_value[menu_rows];
const char* menu_label(int row);
const char* const* menu_names(int row);
void menu_changed(int row, int value);
// "<ROM stem>/<save stem or VC>" in 23 characters (the menu's value column).
std::string pak_text(const std::string& rom, const std::string& save, bool vc);

// What a Transfer Pak holds, for the Game Pak screen (P44): game 0 Red, 1 Blue,
// 2 Yellow, 3 Gold, 4 Silver, 5 Crystal, -1 other; the trainer's name and ID
// from the save (empty when the save is blank).
struct PakInfo { bool present = false; int game = -1; char trainer[12] = {}; unsigned id = 0; bool vc = false; };
PakInfo pak_info(int port);

// Exposed for the host test.
enum class SaveKind { Unknown, Gen1, Gen2GS, Gen2Crystal };
SaveKind save_kind(const uint8_t* rom, size_t rom_size);
bool save_checksum_ok(SaveKind kind, const uint8_t* ram, size_t ram_size);
uint32_t crc32(const uint8_t* data, size_t size);
}
