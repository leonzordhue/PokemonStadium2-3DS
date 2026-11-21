// Host test for tpak_3ds.cpp: g++ -std=c++17 tpak_3ds.cpp vc_save_3ds.cpp test_tpak.cpp -o /tmp/test_tpak && /tmp/test_tpak
#include "tpak_3ds.h"
#include "vc_save_3ds.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;
static fs::path T;

static std::vector<uint8_t> rd(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static void wr(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), d.size());
}
static std::vector<uint8_t> make_rom(const char* title, uint8_t type, uint8_t ram_code, size_t size) {
    std::vector<uint8_t> r(size, 0x11);
    std::memcpy(&r[0x134], title, std::strlen(title));
    r[0x147] = type;
    r[0x149] = ram_code;
    return r;
}
// One accessory write: libultra address = byte address, block = /32.
static void put(int port, uint16_t addr, uint8_t v) {
    uint8_t b[tpak::block_size];
    std::memset(b, v, sizeof b);
    assert(tpak::write_block(port, addr / 32, b) == 0);
}
static uint8_t get(int port, uint16_t addr) {
    uint8_t b[tpak::block_size];
    assert(tpak::read_block(port, addr / 32, b) == 0);
    return b[addr % 32];
}
// GB bus write through the cart window: bank select (0xA000) then 0xC000 + offset.
static void bus_write(int port, uint16_t bus, uint8_t v) {
    put(port, 0xA000, uint8_t(bus / 0x4000));
    put(port, uint16_t(0xC000 + bus % 0x4000), v);
}
static uint8_t bus_read(int port, uint16_t bus) {
    put(port, 0xA000, uint8_t(bus / 0x4000));
    return get(port, uint16_t(0xC000 + bus % 0x4000));
}
// Write 32 bytes of cart SRAM at bus address `bus` (block aligned).
static void sram_block(int port, uint16_t bus, const uint8_t* d) {
    put(port, 0xA000, uint8_t(bus / 0x4000));
    assert(tpak::write_block(port, uint16_t((0xC000 + bus % 0x4000) / 32), d) == 0);
}

int main() {
    T = fs::temp_directory_path() / "tpak_test";
    fs::remove_all(T);
    fs::create_directories(T / "gb" / "saves");
    tpak::set_base_dir(T.string());

    // Checksums
    std::vector<uint8_t> crys(0x8000, 0);
    for (size_t i = 0x2009; i <= 0x2B82; i++) crys[i] = uint8_t(i);
    uint32_t s = 0;
    for (size_t i = 0x2009; i <= 0x2B82; i++) s += crys[i];
    crys[0x2D0D] = uint8_t(s);
    crys[0x2D0E] = uint8_t(s >> 8);
    assert(tpak::save_checksum_ok(tpak::SaveKind::Gen2Crystal, crys.data(), crys.size()));
    crys[0x2100] ^= 1;
    assert(!tpak::save_checksum_ok(tpak::SaveKind::Gen2Crystal, crys.data(), crys.size()));
    crys[0x2100] ^= 1;
    std::vector<uint8_t> g1(0x8000, 0);
    g1[0x2600] = 5;
    g1[0x3523] = uint8_t(~5);
    assert(tpak::save_checksum_ok(tpak::SaveKind::Gen1, g1.data(), g1.size()));
    assert(tpak::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);

    // Crystal cart (MBC3 + timer), existing save -> backups
    const auto rom = make_rom("PM_CRYSTAL", 0x10, 0x03, 0x8000);
    assert(tpak::save_kind(rom.data(), rom.size()) == tpak::SaveKind::Gen2Crystal);
    const fs::path romp = T / "crystal.gbc", save = T / "gb" / "saves" / "crystal.sav";
    wr(romp, rom);
    wr(save, std::vector<uint8_t>(0x8000, 0xFF));
    assert(tpak::set_cart(0, romp.string(), save.string()));
    assert(tpak::has_pak(0) && !tpak::has_pak(1));
    assert(tpak::status() == "Cartridge connected");
    int backups = 0;
    for (auto& e : fs::directory_iterator(T / "gb" / "backup")) backups += e.path().filename().string().rfind("crystal-", 0) == 0;
    assert(backups == 2);   // -original and -last

    // Accessory protocol
    put(0, 0x8000, 0x84);
    assert(get(0, 0x8000) == 0x84);
    put(0, 0xB000, 0x01);                           // cart on
    const uint8_t st = get(0, 0xB000);
    assert((st & 0x08) && !(st & 0x04) && (st & 0x80) && (st & 0x01));
    assert(bus_read(0, 0x0134) == 'P');             // ROM through the window
    bus_write(0, 0x0000, 0x0A);                     // MBC RAM on
    bus_write(0, 0x4000, 0x01);                     // RAM bank 1 = save offset 0x2000

    // Bad checksum: not written
    uint8_t blk[32];
    std::memset(blk, 0x42, 32);
    sram_block(0, 0xA100, blk);
    tpak::flush_all(true);
    assert(rd(save) == std::vector<uint8_t>(0x8000, 0xFF));
    assert(bus_read(0, 0xA100) == 0x42);

    // Whole valid bank 1: written with the RTC footer
    for (uint16_t off = 0; off < 0x2000; off += 32) sram_block(0, uint16_t(0xA000 + off), &crys[0x2000 + off]);
    tpak::flush_all(true);
    auto out = rd(save);
    assert(out.size() == 0x8000 + 48);
    assert(std::memcmp(&out[0x2000], &crys[0x2000], 0x2000) == 0);
    assert(tpak::status() == "Save written to SD.");

    // RTC: set seconds, latch, read back
    bus_write(0, 0x4000, 0x08);
    bus_write(0, 0xA000, 30);
    bus_write(0, 0x6000, 0x00);
    bus_write(0, 0x6000, 0x01);
    const uint8_t sec = bus_read(0, 0xA000);
    assert(sec == 30 || sec == 31);

    // Same save in another pak: refused
    assert(!tpak::set_cart(1, romp.string(), save.string()));
    assert(tpak::status() == "Already in the other Transfer Pak.");

    // .pending: valid copy promoted, cut copy dropped
    tpak::flush_all(true);   // the RTC write left the cart dirty; settle it first
    fs::path pend = save;
    pend += ".pending";
    auto newer = crys;
    newer[0x2009] ^= 0xFF;
    newer[0x2D0D] = uint8_t(s + newer[0x2009] - crys[0x2009]);
    newer[0x2D0E] = uint8_t((s + newer[0x2009] - crys[0x2009]) >> 8);
    assert(tpak::save_checksum_ok(tpak::SaveKind::Gen2Crystal, newer.data(), newer.size()));
    wr(pend, newer);
    assert(tpak::set_cart(0, romp.string(), save.string()));
    assert(!fs::exists(pend) && rd(save) == newer);
    wr(pend, std::vector<uint8_t>(100, 0));
    assert(tpak::set_cart(0, romp.string(), save.string()));
    assert(!fs::exists(pend) && rd(save) == newer);
    assert(tpak::status() == "Previous write interrupted. Backups kept.");

    // MBC1 bank switch (Gen 1, 4 banks)
    auto red = make_rom("POKEMON RED", 0x03, 0x03, 0x10000);
    red[3 * 0x4000] = 0x33;
    wr(T / "red.gb", red);
    assert(tpak::set_cart(1, (T / "red.gb").string(), (T / "gb" / "saves" / "red.sav").string()));
    put(1, 0x8000, 0x84);
    put(1, 0xB000, 0x01);
    bus_write(1, 0x2000, 3);
    assert(bus_read(1, 0x4000) == 0x33);

    // D7 A1: a refused mount keeps the cart already in that pak
    assert(!tpak::set_cart(1, romp.string(), save.string()));
    assert(tpak::has_pak(1) && bus_read(1, 0x4000) == 0x33);

    // P1: save read from / written to a 3DS VC title (archive simulated), VC footer kept
    tpak::vc::sim_root((T / "vc").string());
    fs::create_directories(T / "vc" / "0004000000172800");
    auto vcsav = crys;
    vcsav.resize(0x8010, 0xAB);                                   // 16-byte VC footer
    wr(T / "vc" / "0004000000172800" / "sav.dat", vcsav);
    assert(tpak::set_cart(2, romp.string(), "vc"));
    assert(tpak::status() == "Cartridge connected");
    put(2, 0x8000, 0x84);
    put(2, 0xB000, 0x01);
    bus_write(2, 0x0000, 0x0A);
    bus_write(2, 0x4000, 0x01);
    for (uint16_t off = 0; off < 0x2000; off += 32) sram_block(2, uint16_t(0xA000 + off), &newer[0x2000 + off]);
    tpak::flush_all(false);                                       // game just wrote: waits (D7 A3)
    assert(rd(T / "vc" / "0004000000172800" / "sav.dat") == vcsav);
    tpak::flush_all(true);
    assert(tpak::status() == "Save written to SD and VC.");
    auto vcout = rd(T / "vc" / "0004000000172800" / "sav.dat");
    assert(vcout.size() == 0x8010 && std::memcmp(vcout.data(), newer.data(), 0x8000) == 0 && vcout[0x8005] == 0xAB);
    assert(tpak::set_cart(2, "", ""));
    // VC not installed: falls back to the SD save
    assert(tpak::set_cart(2, (T / "red.gb").string(), "vc"));
    assert(tpak::set_cart(2, "", ""));

    // Removal
    assert(tpak::set_cart(1, "", ""));
    assert(!tpak::has_pak(1));
    uint8_t b[32];
    assert(tpak::read_block(1, 0, b) == 1);

    // D31: no transferpak.cfg -> auto-scan of <base>/gb (Pokemon ROMs only, sibling .sav copied)
    {
        const fs::path B = T / "scan";
        fs::create_directories(B / "gb");
        wr(B / "gb" / "zz-red.gb", red);
        wr(B / "gb" / "aa-tetris.gb", make_rom("TETRIS", 0x00, 0x00, 0x8000));
        std::vector<uint8_t> sib(0x8000, 0x5A);
        wr(B / "gb" / "zz-red.sav", sib);
        tpak::set_base_dir(B.string());
        tpak::load_config();
        assert(tpak::has_pak(0) && !tpak::has_pak(1));             // Tetris skipped
        assert(rd(B / "gb" / "saves" / "zz-red.sav") == sib);      // copied
        assert(fs::exists(B / "gb" / "zz-red.sav"));               // original kept
        assert(tpak::set_cart(0, "", ""));
        tpak::set_base_dir(T.string());
    }

    // P40: on the console auto_scan finds "sdmc:/3ds/PokemonStadium/gb/Yellow Edition USA.gb" (S1 folder,
    // space in the name); set_cart used to put that device path under the base dir -> "ROM file not found".
    // Here "sdmc:" is a folder under the current directory, so the bug shows on the host too.
    {
        const fs::path old_cwd = fs::current_path();
        const fs::path C = T / "cwd";
        fs::create_directories(C / "sdmc:" / "3ds" / "PokemonStadium" / "gb");
        fs::create_directories(C / "s2base");
        wr(C / "sdmc:" / "3ds" / "PokemonStadium" / "gb" / "Yellow Edition USA.gb", red);
        fs::current_path(C);
        tpak::set_base_dir((C / "s2base").string());
        tpak::load_config();
        assert(tpak::has_pak(0));
        assert(tpak::status() == "Cartridge connected");
        assert(tpak::set_cart(0, "", ""));
        fs::current_path(old_cwd);
        tpak::set_base_dir(T.string());
    }

    fs::remove_all(T);
    std::puts("tpak: todos os testes passaram");
    return 0;
}
