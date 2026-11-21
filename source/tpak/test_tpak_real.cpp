// P47 [Proton]: the Transfer Pak with real cartridge headers and a save only in the (simulated) VC.
// Host only; the ROMs are not in the repo: test_tpak_real <dir with the 6 USA/Europe ROMs>
// g++ -std=c++17 tpak_3ds.cpp vc_save_3ds.cpp test_tpak_real.cpp -o /tmp/test_tpak_real
#include "tpak_3ds.h"
#include "vc_save_3ds.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static std::vector<uint8_t> rd(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static void wr(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), std::streamsize(d.size()));
}
// A save that passes the game's own checksum (as in test_tpak.cpp).
static std::vector<uint8_t> valid_save(tpak::SaveKind k) {
    std::vector<uint8_t> s(0x8000, 0);
    if (k == tpak::SaveKind::Gen1) { s[0x2600] = 5; s[0x3523] = uint8_t(~5); }
    else if (k == tpak::SaveKind::Gen2Crystal) {
        uint32_t c = 0;
        for (size_t i = 0x2009; i <= 0x2B82; i++) { s[i] = uint8_t(i); c += s[i]; }
        s[0x2D0D] = uint8_t(c); s[0x2D0E] = uint8_t(c >> 8);
    } else {   // Gen 2 Gold/Silver: sum of 0x2009..0x2D68 at 0x2D69
        uint32_t c = 0;
        for (size_t i = 0x2009; i <= 0x2D68; i++) { s[i] = uint8_t(i); c += s[i]; }
        s[0x2D69] = uint8_t(c); s[0x2D6A] = uint8_t(c >> 8);
    }
    return s;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: test_tpak_real <rom dir>\n"); return 2; }
    const struct { const char* match; const char* vc; } games[] = {
        {"Red", "0004000000171000"}, {"Blue", "0004000000171100"}, {"Yellow", "0004000000171200"},
        {"Gold", "0004000000172600"}, {"Silver", "0004000000172700"}, {"Crystal", "0004000000172800"},
    };
    const fs::path T = fs::temp_directory_path() / "tpak_real";
    int fails = 0, n = 0;
    for (const auto& e : fs::directory_iterator(argv[1])) {
        const std::string name = e.path().filename().string();
        const char* vc = nullptr;
        for (const auto& g : games) if (name.find(g.match) != std::string::npos) vc = g.vc;
        if (!vc) continue;
        n++;
        fs::remove_all(T);
        fs::create_directories(T / "gb" / "saves");
        fs::create_directories(T / "vc" / vc);
        tpak::set_base_dir(T.string());
        tpak::vc::sim_root((T / "vc").string());
        const auto rom = rd(e.path());
        const tpak::SaveKind kind = tpak::save_kind(rom.data(), rom.size());
        auto save = valid_save(kind);
        save.resize(0x8010, 0xAB);                       // VC footer
        wr(T / "vc" / vc / "sav.dat", save);
        // The save lives only in the VC title: nothing in gb/saves.
        const bool ok = tpak::set_cart(0, e.path().string(), "vc");
        const std::string st = tpak::status();
        const uint64_t found = tpak::vc::found_id();
        char want[17];
        std::snprintf(want, sizeof want, "%016llX", static_cast<unsigned long long>(found));
        const bool vc_ok = ok && std::string(want) == vc && st == "Cartridge connected";
        std::printf("%-45s kind %d  connected %d  vc %s (want %s)  status '%s'  %s\n", name.c_str(), int(kind), int(ok), want, vc,
                    st.c_str(), vc_ok ? "OK" : "FALHOU");
        fails += !vc_ok;
        tpak::set_cart(0, "", "");
    }
    fs::remove_all(T);
    std::printf("%d ROMs, %d falhas\n", n, fails);
    return fails == 0 && n == 6 ? 0 : 1;
}
