// Host test for the Transfer Pak menu rows (P32, patch 0028):
// g++ -std=c++17 tpak_3ds.cpp vc_save_3ds.cpp test_tpak_menu.cpp -o /tmp/test_tpak_menu && /tmp/test_tpak_menu
#include "tpak_3ds.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void wr(const fs::path& p, const std::string& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << d;
}
static std::string rom(const char* title) {
    std::string r(0x8000, '\x11');
    std::memcpy(&r[0x134], title, std::strlen(title));
    r[0x147] = 0x03;   // MBC1 + RAM + battery
    r[0x149] = 0x03;   // 32 KB
    return r;
}
// What the menu's step() does: write the value, then call on_change.
static void step(int row, int v) {
    tpak::menu_value[row] = v;
    tpak::menu_changed(row, v);
}
static std::string line(int port) { return tpak::menu_names(port)[0]; }

int main() {
    // Line text: 23 characters at most (the menu's value column).
    assert(tpak::pak_text("red", "red", false) == "red/red");
    assert(tpak::pak_text("Pokemon Gold", "x", true) == "Pokemon Gold/VC");
    assert(tpak::pak_text("red", "", false) == "red/no save");
    assert(tpak::pak_text("", "x", false) == "No cartridge");
    const std::string longer = tpak::pak_text(std::string(40, 'R'), std::string(40, 'S'), false);
    assert(longer.size() == 23 && longer == std::string(12, 'R') + "/" + std::string(10, 'S'));

    const fs::path T = fs::temp_directory_path() / "tpak_menu_test";
    fs::remove_all(T);
    fs::create_directories(T / "gb" / "saves");
    tpak::set_base_dir(T.string());
    wr(T / "gb" / "red.gb", rom("POKEMON RED"));
    wr(T / "gb" / "blue.gb", rom("POKEMON BLUE"));
    wr(T / "gb" / "saves" / "red.sav", std::string(0x8000, '\0'));
    wr(T / "transferpak.cfg", "p1_rom=gb/red.gb\np1_save=gb/saves/red.sav\n");

    assert(std::string(tpak::menu_label(0)) == "Transfer Pak 1" && std::string(tpak::menu_label(tpak::port_count)) == "Search saves");
    assert(line(0) == "No cartridge" && std::string(tpak::menu_names(0)[1]) == "Again: unplug");

    // The call after settings.ini loads does nothing, even with "armed" saved there.
    for (int r = 0; r < tpak::menu_rows; r++) step(r, 1);
    for (int r = 0; r < tpak::menu_rows; r++) assert(tpak::menu_value[r] == 0);
    assert(!tpak::has_pak(0));

    tpak::load_config();
    assert(tpak::has_pak(0) && line(0) == "red/red" && line(1) == "No cartridge");

    // Double press: the first arms, the second unplugs.
    step(0, 1);
    assert(tpak::menu_value[0] == 1 && tpak::has_pak(0));
    step(0, 0);
    assert(tpak::menu_value[0] == 0 && !tpak::has_pak(0) && line(0) == "No cartridge");
    // Nothing to unplug: no arming.
    step(0, 1);
    assert(tpak::menu_value[0] == 0 && !tpak::has_pak(0));

    // Search saves: reconnects from transferpak.cfg.
    step(tpak::port_count, 1);
    assert(tpak::menu_value[tpak::port_count] == 0 && tpak::has_pak(0) && line(0) == "red/red");

    // A search disarms: the next press arms again instead of unplugging.
    step(0, 1);
    step(tpak::port_count, 1);
    assert(tpak::menu_value[0] == 0 && tpak::has_pak(0));
    step(0, 0);
    assert(tpak::has_pak(0));

    // An edited cfg is picked up: Blue in pak 2, pak 1 empty.
    wr(T / "transferpak.cfg", "p2_rom=gb/blue.gb\np2_save=gb/saves/blue.sav\n");
    step(tpak::port_count, 0);
    assert(!tpak::has_pak(0) && tpak::has_pak(1) && line(0) == "No cartridge" && line(1) == "blue/blue");

    // Out of range rows are ignored.
    tpak::menu_changed(-1, 1);
    tpak::menu_changed(tpak::menu_rows, 1);

    fs::remove_all(T);
    std::puts("tpak menu: todos os testes passaram");
    return 0;
}
