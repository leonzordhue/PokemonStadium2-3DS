// Pokemon Stadium 2 (3DS) - Transfer Pak. See tpak_3ds.h for origin and layout.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tpak_3ds.h"
#include "vc_save_3ds.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>
#ifdef __3DS__
#include <3ds.h>
#else
#include <chrono>
#include <mutex>
#endif

namespace fs = std::filesystem;

namespace tpak {
namespace {
constexpr int err_no_pak = 1;
constexpr int err_invalid = 5;
constexpr uint16_t address_mask = 0x7FFF;
constexpr uint16_t cart_window = 0x4000;
constexpr size_t rtc_footer = 48;       // VBA-M/mGBA: 5 regs, 5 latched (u32 LE), u64 unix time
constexpr size_t rtc_footer_old = 44;   // older VBA-M: u32 unix time
constexpr uint64_t quiet_ms = 1000;     // periodic flush waits for the game to stop writing (D7 A3)

std::string g_base = "sdmc:/3ds/PokemonStadium2";
std::string g_status;

uint64_t now_ms() {
#ifdef __3DS__
    return osGetTime();
#else
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

// One read, no byte-by-byte growth (D7 A10: a GBC ROM is 2 MB).
std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff n = f.tellg();
    if (n <= 0) return {};
    std::vector<uint8_t> d(static_cast<size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return f ? d : std::vector<uint8_t>{};
}

// P46: auto_scan needs only the cart header (title at 0x134), not the whole
// ROM (a GBC ROM is 2 MB, and the scan looked at each one three times).
std::vector<uint8_t> read_head(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> d(0x150);
    f.read(reinterpret_cast<char*>(d.data()), static_cast<std::streamsize>(d.size()));
    return f ? d : std::vector<uint8_t>{};
}

// Write to <p>.pending, then replace <p>. A .pending left by a cut write is
// checked on the next mount (see Cart::load).
bool write_atomic(const fs::path& p, const std::vector<uint8_t>& data) {
    fs::path pending = p;
    pending += ".pending";
    {
        std::ofstream f(pending, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!f) return false;
    }
    std::error_code ec;
    fs::remove(p, ec);
    fs::rename(pending, p, ec);
    return !ec;
}

void write_quiet(const fs::path& to, const std::vector<uint8_t>& data, bool overwrite) {
    std::error_code ec;
    if (data.empty() || (!overwrite && fs::exists(to, ec))) return;
    std::ofstream f(to, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

size_t header_ram_size(uint8_t code) {
    switch (code) {
    case 0x01: return 2 * 1024;
    case 0x02: return 8 * 1024;
    case 0x03: return 32 * 1024;
    case 0x04: return 128 * 1024;
    case 0x05: return 64 * 1024;
    default: return 0;
    }
}

uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }

// The VC title that holds this ROM's save, from the cart header title.
// P47: compared on the header's first 11 characters (the CGB title field), so "POKEMON BLUE"
// (12) matches too; Gen 1 titles are translated (the names tpak_menu_3ds.cpp listed).
bool vc_game_of(const uint8_t* rom, size_t n, vc::Game& g) {
    if (n < 0x150) return false;
    const std::string t(reinterpret_cast<const char*>(rom + 0x134), 11);
    static const struct { const char* title; vc::Game game; } map[] = {
        {"POKEMON RED", vc::Game::Red},        {"POKEMON ROUGE", vc::Game::Red},   {"POKEMON ROT", vc::Game::Red},
        {"POKEMON ROJO", vc::Game::Red},       {"POKEMON ROSSO", vc::Game::Red},
        {"POKEMON BLUE", vc::Game::Blue},      {"POKEMON BLEU", vc::Game::Blue},   {"POKEMON BLAU", vc::Game::Blue},
        {"POKEMON AZUL", vc::Game::Blue},      {"POKEMON BLU", vc::Game::Blue},
        {"POKEMON YELLOW", vc::Game::Yellow},  {"POKEMON JAUNE", vc::Game::Yellow}, {"POKEMON GELB", vc::Game::Yellow},
        {"POKEMON AMARILLO", vc::Game::Yellow}, {"POKEMON GIALLO", vc::Game::Yellow},
        {"POKEMON_GLD", vc::Game::Gold},       {"POKEMON_SLV", vc::Game::Silver},  {"PM_CRYSTAL", vc::Game::Crystal},
    };
    for (const auto& m : map) {
        const std::string want = std::string(m.title).substr(0, 11);
        if (t.compare(0, want.size(), want) == 0) { g = m.game; return true; }
    }
    return false;
}

enum class Mbc { RomOnly, Mbc1, Mbc3, Mbc5 };

// MBC3 clock: the counter is host time minus `base` (frozen while halted).
struct Rtc {
    bool present = false;
    int64_t base = 0;            // unix time at which the counter read 0
    int64_t halted_at = -1;      // counter value while halted, -1 = running
    bool carry = false;
    uint8_t latched[5]{};
    uint8_t latch_prev = 0xFF;

    static int64_t now() { return static_cast<int64_t>(std::time(nullptr)); }
    int64_t counter() const { return halted_at >= 0 ? halted_at : now() - base; }

    void regs(uint8_t out[5]) const {
        int64_t c = counter();
        int64_t days = c / 86400;
        out[0] = uint8_t(c % 60);
        out[1] = uint8_t((c / 60) % 60);
        out[2] = uint8_t((c / 3600) % 24);
        out[3] = uint8_t(days & 0xFF);
        out[4] = uint8_t(((days >> 8) & 1) | (halted_at >= 0 ? 0x40 : 0) | ((carry || days > 511) ? 0x80 : 0));
    }
    void set_regs(const uint8_t r[5]) {
        const int64_t days = r[3] | ((r[4] & 1) << 8);
        const int64_t c = days * 86400 + (r[2] % 24) * 3600 + (r[1] % 60) * 60 + (r[0] % 60);
        carry = (r[4] & 0x80) != 0;
        if (r[4] & 0x40) { halted_at = c; } else { halted_at = -1; base = now() - c; }
    }
    void write_reg(int i, uint8_t v) {
        uint8_t r[5];
        regs(r);
        r[i] = v;
        set_regs(r);
    }
    void latch_write(uint8_t v) {
        if (latch_prev == 0x00 && v == 0x01) regs(latched);
        latch_prev = v;
    }
};

struct Cart {
    fs::path rom_path, save_path;
    std::vector<uint8_t> rom, ram;
    std::vector<uint8_t> foreign_tail;   // save footer that is not VBA-M's (e.g. the VC's): kept as is (D7 A6)
    size_t open_bus_from = 0;
    Mbc mbc = Mbc::RomOnly;
    SaveKind kind = SaveKind::Unknown;
    Rtc rtc;
    bool ram_enabled = false, dirty = false;
    bool loaded_save = false;            // a save existed when mounted (backups)
    uint64_t vc_id = 0;                  // != 0: save lives in this 3DS VC title (proton P1)
    uint64_t last_write_ms = 0;
    uint8_t rom_bank_low = 1, bank_high = 0, ram_bank = 0, mbc1_mode = 0;
    uint16_t mbc5_rom_bank = 1;

    void touch() { dirty = true; last_write_ms = now_ms(); }

    // save: "" none, a path, or "vc [title id hex]" (no id = find the VC of this ROM).
    bool load(const fs::path& r, const std::string& s) {
        rom_path = r;
        rom = read_file(r);
        if (rom.size() < 0x150) { g_status = rom.empty() ? "ROM file not found." : "Invalid Game Boy ROM."; return false; }
        // Zero padding at the end of a dump reads as open bus (0xFF) on the
        // real Transfer Pak; the GB Tower depends on it (mstan, gbcart.cpp).
        open_bus_from = rom.size();
        auto last = std::find_if(rom.rbegin(), rom.rend(), [](uint8_t b) { return b != 0; });
        if (last != rom.rend()) {
            const size_t end = static_cast<size_t>(rom.rend() - last);
            if (rom.size() - end >= 0x4000) open_bus_from = end;
        }
        const uint8_t type = rom[0x147];
        if (type == 0x00 || type == 0x08 || type == 0x09) mbc = Mbc::RomOnly;
        else if (type >= 0x01 && type <= 0x03) mbc = Mbc::Mbc1;
        else if (type >= 0x0F && type <= 0x13) mbc = Mbc::Mbc3;
        else if (type >= 0x19 && type <= 0x1E) mbc = Mbc::Mbc5;
        else { g_status = "Unsupported cartridge type."; return false; }
        rtc.present = (type == 0x0F || type == 0x10);
        rtc.base = Rtc::now();
        kind = save_kind(rom.data(), rom.size());
        const size_t declared = header_ram_size(rom[0x149]);
        const std::string stem = r.stem().string();
        const fs::path sd_default = fs::path(g_base) / "gb" / "saves" / (stem + ".sav");

        if (s.rfind("vc", 0) == 0) {
            const uint64_t want = std::strtoull(s.c_str() + 2, nullptr, 16);
            vc::Game g;
            const bool found = want ? vc::vc_find_id(want) : (vc_game_of(rom.data(), rom.size(), g) && vc::vc_find(g));
            if (found) {
                vc_id = vc::found_id();
                save_path = vc::local_path(g_base, stem, crc32(rom.data(), rom.size()));
                fs::path pending = save_path;
                pending += ".pending";
                std::error_code ec;
                if (fs::exists(pending, ec)) {   // last VC write was cut: finish it (idempotent)
                    vc::vc_commit(save_path.string());
                    g_status = "Previous write interrupted. Backups kept.";
                    std::fprintf(stderr, "tpak: the last session ended in the middle of a VC write\n");
                }
                if (vc::vc_find_id(vc_id) && vc::vc_read(ram) && save_checksum_ok(kind, ram.data(), ram.size())) {
                    std::fprintf(stderr, "tpak: vc %016llX save size=%zu\n", static_cast<unsigned long long>(vc_id), ram.size());
                } else {
                    // Never write back over a VC save we could not read or validate.
                    std::fprintf(stderr, "tpak: the VC save fails the game's checksum; using the SD copy\n");
                    g_status = "Invalid VC save. Backups kept.";
                    vc_id = 0;
                    ram = read_file(save_path);
                }
            } else {
                save_path = sd_default;   // fallback: save on the SD (D7/P1)
                ram = read_file(save_path);
            }
        } else {
            save_path = s;
            if (!s.empty()) {
                recover_pending();
                ram = read_file(save_path);
            }
        }
        loaded_save = !ram.empty();

        const size_t tail = ram.size() > declared ? ram.size() - declared : 0;
        if (tail && tail != rtc_footer_old && tail != rtc_footer) foreign_tail.assign(ram.begin() + declared, ram.end());
        if (rtc.present && foreign_tail.empty() && tail >= rtc_footer_old) {
            const uint8_t* f = ram.data() + declared;
            uint8_t rr[5], l[5];
            for (int i = 0; i < 5; i++) { rr[i] = uint8_t(rd32(f + 4 * i)); l[i] = uint8_t(rd32(f + 20 + 4 * i)); }
            const int64_t saved_at = tail >= rtc_footer
                ? int64_t(rd32(f + 40)) | (int64_t(rd32(f + 44)) << 32)
                : int64_t(rd32(f + 40));   // 44-byte footer: u32 stamp (D7 A7)
            rtc.set_regs(rr);
            if (rtc.halted_at < 0) rtc.base -= Rtc::now() - saved_at;   // keep the clock running while off
            std::memcpy(rtc.latched, l, 5);
        }
        ram.resize(declared, 0xFF);   // footer (if any) rebuilt on write
        return true;
    }

    // A .pending is a complete new save whose final rename did not happen,
    // or a cut write. Promote it only if it is whole and passes the checksum.
    void recover_pending() {
        fs::path pending = save_path;
        pending += ".pending";
        std::error_code ec;
        if (!fs::exists(pending, ec)) return;
        const auto data = read_file(pending);
        const size_t declared = header_ram_size(rom[0x149]);
        if (data.size() >= declared && save_checksum_ok(kind, data.data(), declared)) {
            fs::remove(save_path, ec);
            fs::rename(pending, save_path, ec);
            std::fprintf(stderr, "tpak: finished the interrupted write of %s\n", save_path.string().c_str());
        } else {
            fs::remove(pending, ec);
            g_status = "Previous write interrupted. Backups kept.";
            std::fprintf(stderr, "tpak: the last session ended in the middle of a write; backups left as they are\n");
        }
    }

    size_t rom_index(size_t bank, uint16_t addr) const {
        const size_t banks = (rom.size() + 0x3FFF) / 0x4000;
        return (bank % banks) * 0x4000 + addr;
    }
    size_t switchable_bank() const {
        if (mbc == Mbc::Mbc5) return mbc5_rom_bank;
        if (mbc == Mbc::Mbc1) {
            size_t b = (rom_bank_low & 0x1F) | ((bank_high & 0x03) << 5);
            return (b & 0x1F) == 0 ? b + 1 : b;
        }
        size_t b = rom_bank_low & 0x7F;
        return b == 0 ? 1 : b;
    }
    size_t ram_offset(uint16_t addr) const {
        const size_t bank = (mbc == Mbc::Mbc1 && mbc1_mode == 0) ? 0 : (ram_bank & (mbc == Mbc::Mbc5 ? 0x0F : 0x03));
        return bank * 0x2000 + (addr - 0xA000);
    }
    uint8_t rom_byte(size_t i) const { return (i >= open_bus_from || i >= rom.size()) ? 0xFF : rom[i]; }

    uint8_t read(uint16_t addr) const {
        if (addr < 0x4000) {
            const size_t bank = (mbc == Mbc::Mbc1 && mbc1_mode) ? (bank_high & 0x03) << 5 : 0;
            return rom_byte(rom_index(bank, addr));
        }
        if (addr < 0x8000) return rom_byte(rom_index(switchable_bank(), addr - 0x4000));
        if (addr >= 0xA000 && addr < 0xC000 && ram_enabled) {
            if (mbc == Mbc::Mbc3 && rtc.present && ram_bank >= 0x08 && ram_bank <= 0x0C) return rtc.latched[ram_bank - 8];
            const size_t i = ram_offset(addr);
            return i < ram.size() ? ram[i] : 0xFF;
        }
        return 0xFF;
    }

    void write(uint16_t addr, uint8_t v) {
        if (mbc == Mbc::RomOnly) {
            if (addr >= 0xA000 && addr < 0xC000 && addr - 0xA000u < ram.size() && ram[addr - 0xA000] != v) {
                ram[addr - 0xA000] = v;
                touch();
            }
            return;
        }
        if (addr < 0x2000) { ram_enabled = (v & 0x0F) == 0x0A; return; }
        if (addr < 0x4000) {
            if (mbc == Mbc::Mbc1) { rom_bank_low = v & 0x1F; if (!rom_bank_low) rom_bank_low = 1; }
            else if (mbc == Mbc::Mbc5) {
                mbc5_rom_bank = addr < 0x3000 ? uint16_t((mbc5_rom_bank & 0x100) | v)
                                              : uint16_t((mbc5_rom_bank & 0xFF) | ((v & 1) << 8));
            } else { rom_bank_low = v & 0x7F; if (!rom_bank_low) rom_bank_low = 1; }
            return;
        }
        if (addr < 0x6000) {
            if (mbc == Mbc::Mbc1) { bank_high = v & 0x03; ram_bank = bank_high; }
            else if (mbc == Mbc::Mbc5) ram_bank = v & 0x0F;
            else if (v <= 0x03 || (rtc.present && v >= 0x08 && v <= 0x0C)) ram_bank = v;
            return;
        }
        if (addr < 0x8000) {
            if (mbc == Mbc::Mbc1) mbc1_mode = v & 1;
            else if (mbc == Mbc::Mbc3 && rtc.present) rtc.latch_write(v);
            return;
        }
        if (addr >= 0xA000 && addr < 0xC000 && ram_enabled) {
            if (mbc == Mbc::Mbc3 && rtc.present && ram_bank >= 0x08 && ram_bank <= 0x0C) {
                rtc.write_reg(ram_bank - 8, v);
                touch();
                return;
            }
            const size_t i = ram_offset(addr);
            if (i < ram.size() && ram[i] != v) { ram[i] = v; touch(); }
        }
    }

    // Bytes for the save file: RAM + the footer it came with (VC), or the
    // VBA-M RTC footer for MBC3 clock carts.
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> out = ram;
        if (!foreign_tail.empty()) {
            out.insert(out.end(), foreign_tail.begin(), foreign_tail.end());
        } else if (rtc.present) {
            uint8_t r[5];
            rtc.regs(r);
            out.resize(ram.size() + rtc_footer, 0);
            uint8_t* f = out.data() + ram.size();
            for (int i = 0; i < 5; i++) { wr32(f + 4 * i, r[i]); wr32(f + 20 + 4 * i, rtc.latched[i]); }
            const int64_t t = Rtc::now();
            wr32(f + 40, uint32_t(t));
            wr32(f + 44, uint32_t(uint64_t(t) >> 32));
        }
        return out;
    }
};

struct Port {
    bool configured = false, pak_enabled = false, cart_enabled = false;
    uint8_t address_bank = 3, reset_state = 0;
    Cart cart;

    uint8_t status() {
        const uint8_t v = (cart_enabled ? 0x01 : 0) | uint8_t((reset_state & 3) << 2) |
                          (configured ? 0 : 0x40) | (pak_enabled ? 0x80 : 0);
        if (cart_enabled && reset_state == 3) reset_state = 2;
        else if (!cart_enabled && reset_state == 2) reset_state = 1;
        else if (!cart_enabled && reset_state == 1) reset_state = 0;
        return v;
    }
    uint8_t read(uint16_t a) {
        a &= address_mask;
        if (!pak_enabled) return 0;
        if (a <= 0x1FFF) return 0x84;
        if (a <= 0x2FFF) return address_bank;
        if (a <= 0x3FFF) return status();
        if (!cart_enabled) return 0;
        return cart.read(uint16_t(cart_window * address_bank + a - cart_window));
    }
    void write(uint16_t a, uint8_t v) {
        a &= address_mask;
        if (a <= 0x1FFF) {
            const bool was = pak_enabled;
            if (v == 0x84) pak_enabled = true;
            else if (v == 0xFE) pak_enabled = false;
            if (!was && pak_enabled) { address_bank = 3; cart_enabled = false; reset_state = 0; }
            return;
        }
        if (!pak_enabled) return;
        if (a <= 0x2FFF) { address_bank = v > 3 ? 0 : v; return; }
        if (a <= 0x3FFF) {
            const bool was = cart_enabled;
            cart_enabled = (v & 1) != 0;
            // Settle on "reset happened, reset line released" (0x08 set, 0x04
            // clear): the GB Tower cart check needs both (mstan, gbcart.cpp).
            if (!was && cart_enabled) reset_state = 2;
            return;
        }
        if (!cart_enabled) return;
        cart.write(uint16_t(cart_window * address_bank + a - cart_window), v);
    }
};

std::array<Port, port_count> g_ports;

// devkitARM's libstdc++ has no std::mutex: libctru's LightLock there.
struct Lock {
#ifdef __3DS__
    LightLock l;
    Lock() { LightLock_Init(&l); }
    void lock() { LightLock_Lock(&l); }
    void unlock() { LightLock_Unlock(&l); }
#else
    std::mutex m;
    void lock() { m.lock(); }
    void unlock() { m.unlock(); }
#endif
};
// Lock order: g_mutex (cart state) before g_io (SD/VC writes; the VC module
// keeps one global "found" title). flush_all never holds both at once.
Lock g_mutex, g_io;
struct Guard {
    explicit Guard(Lock& l) : l_(l) { l_.lock(); }
    ~Guard() { l_.unlock(); }
    Lock& l_;
};

// The menu's pak lines (menu_names): written under g_mutex, read unlocked by
// the menu when it draws. shortcut: a line torn by a mount mid-draw is fixed
// on the next redraw; copy under the lock if it ever shows.
char g_line[port_count][24] = {"No cartridge", "No cartridge", "No cartridge", "No cartridge"};
bool g_armed[port_count], g_menu_seen[menu_rows];
void refresh_line(int port) {
    const Port& p = g_ports[port];
    const std::string t = p.configured ? pak_text(p.cart.rom_path.stem().string(), p.cart.save_path.stem().string(), p.cart.vc_id != 0)
                                       : "No cartridge";
    std::snprintf(g_line[port], sizeof g_line[port], "%s", t.c_str());
}

// One save write, SD first, then the VC title (S1: "save written to SD ... and VC").
struct Job {
    fs::path path;
    std::vector<uint8_t> data;
    uint64_t vc_id = 0, stamp = 0;
    int port = 0;
};
bool run_job(const Job& j, std::string& msg) {
    Guard io(g_io);
    if (!write_atomic(j.path, j.data)) {
        msg = "Save failed. Cartridge kept connected.";
        std::fprintf(stderr, "tpak: cannot write %s\n", j.path.string().c_str());
        return false;
    }
    if (!j.vc_id) { msg = "Save written to SD."; return true; }
    const std::string local = j.path.string();
    if (vc::vc_find_id(j.vc_id) && vc::vc_write_pending(local, j.data) && vc::vc_commit(local)) {
        msg = "Save written to SD and VC.";
        return true;
    }
    msg = "Could not write the VC save.";   // SD copy is good; .pending retried at next mount
    std::fprintf(stderr, "tpak: VC %016llX write failed\n", static_cast<unsigned long long>(j.vc_id));
    return true;
}

// Under g_mutex. False = nothing to write. A cart whose RAM fails the game's
// checksum is mid-write: not written, last good copy kept (D7 A8).
bool make_job(int port, bool force, Job& j) {
    Cart& c = g_ports[port].cart;
    if (!g_ports[port].configured || !c.dirty || c.save_path.empty() || c.ram.empty()) return false;
    if (!force && now_ms() - c.last_write_ms < quiet_ms) return false;   // game still writing (D7 A3)
    if (!save_checksum_ok(c.kind, c.ram.data(), c.ram.size())) {
        g_status = "Cart RAM fails the game's checksum; not written, last good copy kept.";
        return false;
    }
    j = Job{c.save_path, c.serialize(), c.vc_id, c.last_write_ms, port};
    return true;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t' || s.back() == '"')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '"')) i++;
    return s.substr(i);
}

// Relative cfg paths are relative to the base folder (D7 A9; the .3dsx CWD varies).
std::string resolve(const std::string& v) {
    if (v.empty() || v.rfind("vc", 0) == 0) return v;
    // P40: "sdmc:/..." (a device path, what auto_scan finds outside the base) is absolute on the 3DS,
    // but std::filesystem sees no root in it and would put it under g_base.
    const size_t colon = v.find(':');
    if (colon != std::string::npos && colon < v.find('/')) return v;
    const fs::path p(v);
    return p.is_relative() ? (fs::path(g_base) / p).string() : v;
}
}  // namespace

void set_base_dir(const std::string& dir) { g_base = dir; }
const std::string& status() { return g_status; }

uint32_t crc32(const uint8_t* d, size_t n) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

SaveKind save_kind(const uint8_t* rom, size_t n) {
    if (n < 0x150) return SaveKind::Unknown;
    const std::string title(reinterpret_cast<const char*>(rom + 0x134), 11);
    if (title.rfind("PM_CRYSTAL", 0) == 0) return SaveKind::Gen2Crystal;
    if (title.rfind("POKEMON_GLD", 0) == 0 || title.rfind("POKEMON_SLV", 0) == 0) return SaveKind::Gen2GS;
    if (title.rfind("POKEMON ", 0) == 0) return SaveKind::Gen1;   // RED, BLUE, GREEN, YELLOW (YELAPS)
    return SaveKind::Unknown;
}

// Main-data checksums of the international releases (Bulbapedia, "Save data
// structure"): Gen 1 0x2598..0x3522 -> 0x3523 (~sum & 0xFF); Gold/Silver
// 0x2009..0x2D68 -> 0x2D69 (u16 LE sum); Crystal 0x2009..0x2B82 -> 0x2D0D.
bool save_checksum_ok(SaveKind kind, const uint8_t* ram, size_t n) {
    auto sum = [&](size_t from, size_t to) { uint32_t s = 0; for (size_t i = from; i <= to; i++) s += ram[i]; return s; };
    switch (kind) {
    case SaveKind::Gen1:
        return n > 0x3523 && uint8_t(~sum(0x2598, 0x3522)) == ram[0x3523];
    case SaveKind::Gen2GS:
        return n > 0x2D6A && uint16_t(sum(0x2009, 0x2D68)) == (ram[0x2D69] | (ram[0x2D6A] << 8));
    case SaveKind::Gen2Crystal:
        return n > 0x2D0E && uint16_t(sum(0x2009, 0x2B82)) == (ram[0x2D0D] | (ram[0x2D0E] << 8));
    default:
        return true;   // other carts: no check
    }
}

bool set_cart(int port, const std::string& rom_in, const std::string& save_in) {
    if (port < 0 || port >= port_count) return false;
    const std::string rom = resolve(rom_in), save = resolve(save_in);
    Guard lock(g_mutex);
    struct Refresh { int p; ~Refresh() { refresh_line(p); } } refresh{port};   // every exit, lock still held
    // Refuse before touching this port: a refusal must not unmount its cart (D7 A1).
    // ponytail: two ports on the same VC title are not detected; add when the menu allows it.
    if (!rom.empty() && !save.empty() && save.rfind("vc", 0) != 0)
        for (int i = 0; i < port_count; i++)
            if (i != port && g_ports[i].configured && g_ports[i].cart.save_path == fs::path(save)) {
                g_status = "Already in the other Transfer Pak.";
                return false;
            }
    // A save that cannot be written keeps the cart connected (D7 A2).
    Job j;
    if (make_job(port, true, j)) {
        std::string msg;
        const bool ok = run_job(j, msg);
        g_status = msg;
        if (!ok) return false;
    }
    g_ports[port] = Port{};
    if (rom.empty()) { g_status = "Cartridge removed."; return true; }
    Port& p = g_ports[port];
    {
        Guard io(g_io);
        p.configured = p.cart.load(rom, save);
    }
    if (!p.configured) {
        std::fprintf(stderr, "tpak: pak %d not connected: %s\n", port + 1, g_status.c_str());
        return false;
    }
    // Backups (Stadium 3DS layout): -original once, -last every session.
    if (p.cart.loaded_save) {
        std::error_code ec;
        const fs::path dir = fs::path(g_base) / "gb" / "backup";
        fs::create_directories(dir, ec);
        char crc[9];
        std::snprintf(crc, sizeof crc, "%08lX", static_cast<unsigned long>(crc32(p.cart.rom.data(), p.cart.rom.size())));
        const std::string stem = p.cart.save_path.stem().string();
        const auto bytes = p.cart.serialize();
        write_quiet(dir / (stem + "-" + crc + "-original.sav"), bytes, false);
        write_quiet(dir / (stem + "-" + crc + "-last.sav"), bytes, true);
    }
    if (g_status.rfind("Previous", 0) != 0 && g_status.rfind("Invalid VC", 0) != 0) g_status = "Cartridge connected";
    std::fprintf(stderr, "tpak: pak %d connected %s save=%s\n", port + 1, rom.c_str(), p.cart.save_path.string().c_str());
    return true;
}

bool has_pak(int port) {
    Guard lock(g_mutex);
    return port >= 0 && port < port_count && g_ports[port].configured;
}

// D31 (nexus): no ROM in transferpak.cfg -> look where the S1 looks
// (tpak/COMPORTAMENTO-S1.md) and connect the Pokemon ROMs found, in name
// order, one per pak. A <rom>.sav beside the ROM is copied (never moved) to
// gb/saves/ when that has none, so the usual VC -> gb/saves fallback finds it.
void auto_scan(std::string (&rom)[port_count]) {
    const fs::path dirs[] = {fs::path(g_base) / "gb", "sdmc:/3ds/PokemonStadium/gb", "sdmc:/roms/gb", "sdmc:/roms/gbc", "sdmc:/gb"};
    std::vector<fs::path> found;
    for (const fs::path& d : dirs) {
        std::error_code ec;
        std::vector<fs::path> here;
        for (fs::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".gb" && ext != ".gbc") continue;
            const std::vector<uint8_t> r = read_head(it->path());
            if (r.size() < 0x150 || save_kind(r.data(), r.size()) == SaveKind::Unknown) continue;
            here.push_back(it->path());
        }
        std::sort(here.begin(), here.end());
        found.insert(found.end(), here.begin(), here.end());
    }
    // P45: a game installed only as VC (no .gb/.gbc found): its ROM is copied once
    // out of the VC title to gb/<Game>-VC.gbc (found by the scan above next time).
    static const char* const kVcName[] = {"Red", "Blue", "Yellow", "Gold", "Silver", "Crystal"};
    bool have[6] = {};
    for (const fs::path& r : found) {
        const std::vector<uint8_t> b = read_head(r);
        vc::Game g;
        if (vc_game_of(b.data(), b.size(), g)) have[static_cast<int>(g)] = true;
    }
    for (int g = 0; g < 6 && found.size() < size_t(port_count); g++) {
        if (have[g]) continue;
        // Only a VC with a save worth connecting (tested: all six installed, two with saves):
        // an empty or never-written sav.dat would take a pak from the one in use.
        std::vector<uint8_t> sav;
        if (!vc::vc_find(static_cast<vc::Game>(g)) || !vc::vc_read(sav)) continue;
        if (std::all_of(sav.begin(), sav.begin() + std::min(sav.size(), size_t(0x8000)), [](uint8_t b) { return b == 0x00 || b == 0xFF; })) continue;
        const fs::path out = fs::path(g_base) / "gb" / (std::string("Pokemon ") + kVcName[g] + "-VC.gbc");
        if (vc::vc_extract_rom(static_cast<vc::Game>(g), out.string())) {
            std::fprintf(stderr, "tpak: ROM of the %s VC copied to %s\n", kVcName[g], out.string().c_str());
            found.push_back(out);
        }
    }
    // P45b: one pak per game, the games with a save first (VC, else gb/saves/<rom>.sav).
    {
        std::vector<fs::path> with_save, without;
        bool taken[7] = {};
        for (const fs::path& r : found) {
            const std::vector<uint8_t> b = read_head(r);
            vc::Game g;
            const int gi = vc_game_of(b.data(), b.size(), g) ? static_cast<int>(g) : 6;
            if (gi < 6 && taken[gi]) continue;
            if (gi < 6) taken[gi] = true;
            std::error_code ec;
            bool saved = fs::exists(fs::path(g_base) / "gb" / "saves" / (r.stem().string() + ".sav"), ec);
            std::vector<uint8_t> sav;
            if (!saved && gi < 6 && vc::vc_find(g) && vc::vc_read(sav))
                saved = !std::all_of(sav.begin(), sav.begin() + std::min(sav.size(), size_t(0x8000)), [](uint8_t v) { return v == 0x00 || v == 0xFF; });
            (saved ? with_save : without).push_back(r);
        }
        found = with_save;
        found.insert(found.end(), without.begin(), without.end());
    }
    // P45b: one pak per game, the games with a save first (VC, else gb/saves/<rom>.sav).
    {
        std::vector<fs::path> with_save, without;
        bool taken[7] = {};
        for (const fs::path& r : found) {
            const std::vector<uint8_t> b = read_file(r);
            vc::Game g;
            const int gi = vc_game_of(b.data(), b.size(), g) ? static_cast<int>(g) : 6;
            if (gi < 6 && taken[gi]) continue;
            if (gi < 6) taken[gi] = true;
            std::error_code ec;
            bool saved = fs::exists(fs::path(g_base) / "gb" / "saves" / (r.stem().string() + ".sav"), ec);
            std::vector<uint8_t> sav;
            if (!saved && gi < 6 && vc::vc_find(g) && vc::vc_read(sav))
                saved = !std::all_of(sav.begin(), sav.begin() + std::min(sav.size(), size_t(0x8000)), [](uint8_t v) { return v == 0x00 || v == 0xFF; });
            (saved ? with_save : without).push_back(r);
        }
        found = with_save;
        found.insert(found.end(), without.begin(), without.end());
    }
    int n = 0;
    for (const fs::path& r : found) {
        bool dup = false;   // same file name in two folders: keep the first
        for (int i = 0; i < n; i++) dup |= fs::path(rom[i]).filename() == r.filename();
        if (dup || n == port_count) continue;
        fs::path sib = r;
        sib.replace_extension(".sav");
        const fs::path dst = fs::path(g_base) / "gb" / "saves" / (r.stem().string() + ".sav");
        std::error_code ec;
        if (fs::exists(sib, ec) && !fs::exists(dst, ec)) write_quiet(dst, read_file(sib), false);
        rom[n++] = r.string();
        std::fprintf(stderr, "tpak: auto-scan pak %d = %s\n", n, r.string().c_str());
    }
}

void load_config() {
    const fs::path cfg = fs::path(g_base) / "transferpak.cfg";
    std::ifstream f(cfg);
    std::string rom[port_count], save[port_count], line;
    while (std::getline(f, line)) {
        const size_t c = line.find_first_of("#;");
        if (c != std::string::npos) line.resize(c);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k.size() >= 6 && k[0] == 'p' && k[1] >= '1' && k[1] <= '4' && k[2] == '_') {
            const int i = k[1] - '1';
            if (k.substr(3) == "rom") rom[i] = v;
            else if (k.substr(3) == "save") save[i] = v;
        }
    }
    std::error_code ec;
    fs::create_directories(fs::path(g_base) / "gb" / "saves", ec);
    if (std::all_of(std::begin(rom), std::end(rom), [](const std::string& r) { return r.empty(); }))
        auto_scan(rom);
    for (int i = 0; i < port_count; i++) {
        if (rom[i].empty()) continue;
        // No p<n>_save: the installed VC save of this game, else gb/saves/<rom>.sav (S1).
        if (save[i].empty()) save[i] = "vc";
        set_cart(i, rom[i], save[i]);
    }
}

// Gen 1/2 text: 0x80.. 'A'..'Z', 0xA0.. 'a'..'z', 0xF6.. '0'..'9', 0x50 ends.
// Trainer name and ID: Gen 1 at 0x2598 / 0x2605, Gen 2 at 0x200B / 0x2009 (Bulbapedia,
// "Save data structure in Generation I/II").
PakInfo pak_info(int port) {
    PakInfo r;
    if (port < 0 || port >= port_count) return r;
    Guard lock(g_mutex);
    const Port& p = g_ports[port];
    if (!p.configured) return r;
    r.present = true;
    r.vc = p.cart.vc_id != 0;
    vc::Game g;
    if (vc_game_of(p.cart.rom.data(), p.cart.rom.size(), g)) r.game = static_cast<int>(g);
    const bool gen1 = p.cart.kind == SaveKind::Gen1;
    const size_t name = gen1 ? 0x2598 : 0x200B, id = gen1 ? 0x2605 : 0x2009;
    const std::vector<uint8_t>& ram = p.cart.ram;
    if (p.cart.kind == SaveKind::Unknown || ram.size() < name + 11 || !save_checksum_ok(p.cart.kind, ram.data(), ram.size())) return r;
    int n = 0;
    for (size_t i = 0; i < 10 && n < 10; i++) {
        const uint8_t c = ram[name + i];
        if (c == 0x50) break;
        if (c >= 0x80 && c <= 0x99) r.trainer[n++] = char('A' + c - 0x80);
        else if (c >= 0xA0 && c <= 0xB9) r.trainer[n++] = char('a' + c - 0xA0);
        else if (c >= 0xF6) r.trainer[n++] = char('0' + c - 0xF6);
        else r.trainer[n++] = ' ';
    }
    r.id = unsigned(ram[id] << 8 | ram[id + 1]);
    return r;
}

void search_saves() {
    for (int i = 0; i < port_count; i++) set_cart(i, "", "");
    load_config();
}

int menu_value[menu_rows];

const char* menu_label(int row) {
    static const char* const labels[] = {"Transfer Pak 1", "Transfer Pak 2", "Transfer Pak 3", "Transfer Pak 4", "Search saves"};
    static_assert(sizeof labels / sizeof labels[0] == menu_rows, "one label per row");
    return row >= 0 && row < menu_rows ? labels[row] : "";
}

const char* const* menu_names(int row) {
    static const char* names[menu_rows][2];
    for (int i = 0; i < port_count; i++) { names[i][0] = g_line[i]; names[i][1] = "Again: unplug"; }
    names[port_count][0] = names[port_count][1] = "Press >";
    return names[row >= 0 && row < menu_rows ? row : 0];
}

void menu_changed(int row, int value) {
    if (row < 0 || row >= menu_rows) return;
    const bool first = !g_menu_seen[row];
    g_menu_seen[row] = true;
    menu_value[row] = 0;
    if (first) return;   // the call after settings.ini loads: an "armed" saved there starts disarmed
    if (row == port_count) {
        search_saves();
        for (bool& a : g_armed) a = false;
        for (int i = 0; i < port_count; i++) menu_value[i] = 0;
        return;
    }
    if (value == 1 && !g_armed[row] && has_pak(row)) { g_armed[row] = true; menu_value[row] = 1; return; }
    if (g_armed[row]) set_cart(row, "", "");   // "Tap again to disconnect"
    g_armed[row] = false;
}

std::string pak_text(const std::string& rom, const std::string& save, bool vc) {
    if (rom.empty()) return "No cartridge";
    const std::string s = (vc ? std::string("VC") : save.empty() ? std::string("no save") : save).substr(0, 10);
    return rom.substr(0, 22 - s.size()) + "/" + s;
}

int read_block(int port, uint16_t block_address, uint8_t* out) {
    Guard lock(g_mutex);
    if (port < 0 || port >= port_count || !g_ports[port].configured) return err_no_pak;
    if (!out) return err_invalid;
    const uint16_t a = uint16_t(block_address * block_size);
    // Register blocks are read once and repeated: reading status 32 times
    // would drain the reset latch (mstan, gbcart.cpp).
    if ((a & address_mask) <= 0x3FFF) {
        std::memset(out, g_ports[port].read(a), block_size);
    } else {
        for (int i = 0; i < block_size; i++) out[i] = g_ports[port].read(uint16_t(a + i));
    }
    return 0;
}

int write_block(int port, uint16_t block_address, const uint8_t* data) {
    Guard lock(g_mutex);
    if (port < 0 || port >= port_count || !g_ports[port].configured) return err_no_pak;
    if (!data) return err_invalid;
    const uint16_t a = uint16_t(block_address * block_size);
    for (int i = 0; i < block_size; i++) g_ports[port].write(uint16_t(a + i), data[i]);
    return 0;
}

// Copies under the lock, writes outside it: the game's block I/O never waits
// on the SD (D7 A4). A cart written again meanwhile stays dirty.
void flush_all(bool force) {
    std::vector<Job> jobs;
    {
        Guard lock(g_mutex);
        for (int i = 0; i < port_count; i++) {
            Job j;
            if (make_job(i, force, j)) jobs.push_back(std::move(j));
        }
    }
    for (const Job& j : jobs) {
        std::string msg;
        const bool ok = run_job(j, msg);
        Guard lock(g_mutex);
        Cart& c = g_ports[j.port].cart;
        if (ok && g_ports[j.port].configured && c.save_path == j.path && c.last_write_ms == j.stamp) c.dirty = false;
        g_status = msg;
    }
}
}  // namespace tpak
