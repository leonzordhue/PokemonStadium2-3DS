// Host test for vc_save_3ds: wsl g++ -std=c++17 test_vc_save.cpp vc_save_3ds.cpp
#include "vc_save_3ds.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace tpak::vc;

static std::vector<uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static void put(const fs::path& p, const std::vector<uint8_t>& d) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}

int main() {
    const fs::path root = fs::temp_directory_path() / "vc_save_test";
    fs::remove_all(root);
    sim_root((root / "vc").string());
    const std::string base = (root / "sd").string();

    assert(title_count == 38);
    assert(!vc_find(Game::Crystal) && found_id() == 0 && !vc_commit(local_path(base, "x", 0)));

    // German Crystal installed: 0x8000 SRAM + 0x10 VC footer.
    std::vector<uint8_t> vc(sram_size + 0x10, 0x11);
    for (size_t i = 0; i < 0x10; i++) vc[sram_size + i] = uint8_t(0xA0 + i);
    const fs::path vcfile = root / "vc" / "0004000000172B00" / "sav.dat";
    put(vcfile, vc);
    assert(!vc_find(Game::Gold));
    assert(vc_find(Game::Crystal) && found_id() == 0x0004000000172B00);

    std::vector<uint8_t> buf;
    assert(vc_read(buf) && buf == vc);

    const std::string local = local_path(base, "Pokemon Crystal", 0xEE6F5188);
    assert(local == (fs::path(base) / "gb" / "saves" / "Pokemon Crystal-VC-EE6F5188.sav").string());

    // Stadium 2 changed the SRAM; the footer in buf must not reach the VC file.
    buf.assign(buf.size(), 0x22);
    assert(vc_write_pending(local, buf));
    assert(fs::file_size(local + ".pending") == sram_size);
    assert(vc_commit(local));
    assert(!fs::exists(local + ".pending") && fs::file_size(local) == sram_size);
    const auto after = slurp(vcfile);
    assert(after.size() == sram_size + 0x10);
    for (size_t i = 0; i < sram_size; i++) assert(after[i] == 0x22);
    for (size_t i = 0; i < 0x10; i++) assert(after[sram_size + i] == 0xA0 + i);

    // Cut SD write: short pending is dropped, VC untouched.
    put(local + ".pending", std::vector<uint8_t>(100, 0x33));
    assert(!vc_commit(local) && !fs::exists(local + ".pending") && slurp(vcfile) == after);

    // VC gone (title deleted): pending kept for the next session.
    assert(vc_write_pending(local, std::vector<uint8_t>(sram_size, 0x44)));
    fs::remove(vcfile);
    assert(!vc_commit(local) && fs::exists(local + ".pending"));
    put(vcfile, after);
    assert(vc_find_id(0x0004000000172B00) && vc_commit(local) && slurp(vcfile)[0] == 0x44);

    assert(!vc_write_pending(local, std::vector<uint8_t>(10)));
    fs::remove_all(root);
    std::puts("test_vc_save: ok");
}
