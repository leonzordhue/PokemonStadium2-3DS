// Pokemon Stadium 2 (3DS) - VC save access. See vc_save_3ds.h.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "vc_save_3ds.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#ifdef __3DS__
#include <3ds.h>
#endif

namespace fs = std::filesystem;

namespace tpak::vc {
// Title IDs and product codes: Bl4ckSh4rk's 3DS title list
// (gist.github.com/Bl4ckSh4rk/256ed3b857c9677310837d5180121f35); the ENG IDs
// match PKSM's defaults (PKSM common/source/ConfigurationFile.cpp:107-115).
const Title titles[] = {
    {0x0004000000171000, Game::Red, "ENG", "RCQA"},     {0x0004000000171100, Game::Blue, "ENG", "RCRA"},
    {0x0004000000171200, Game::Yellow, "ENG", "QBFA"},  {0x0004000000172600, Game::Gold, "ENG", "QBPA"},
    {0x0004000000172700, Game::Silver, "ENG", "QBQA"},  {0x0004000000172800, Game::Crystal, "ENG", "QBRA"},
    {0x0004000000171300, Game::Red, "GER", "RCSA"},     {0x0004000000171400, Game::Blue, "GER", "RCTA"},
    {0x0004000000171500, Game::Yellow, "GER", "QBGA"},  {0x0004000000172900, Game::Gold, "GER", "QBSA"},
    {0x0004000000172A00, Game::Silver, "GER", "QBTA"},  {0x0004000000172B00, Game::Crystal, "GER", "QBUA"},
    {0x0004000000171600, Game::Red, "FRA", "RCUA"},     {0x0004000000171700, Game::Blue, "FRA", "RCVA"},
    {0x0004000000171800, Game::Yellow, "FRA", "QBHA"},  {0x0004000000172C00, Game::Gold, "FRA", "QBVA"},
    {0x0004000000172D00, Game::Silver, "FRA", "QBWA"},  {0x0004000000172E00, Game::Crystal, "FRA", "QBXA"},
    {0x0004000000171900, Game::Red, "SPN", "RCWA"},     {0x0004000000171A00, Game::Blue, "SPN", "RCXA"},
    {0x0004000000171B00, Game::Yellow, "SPN", "QBIA"},  {0x0004000000172F00, Game::Gold, "SPN", "QBYA"},
    {0x0004000000173000, Game::Silver, "SPN", "QBZA"},  {0x0004000000173100, Game::Crystal, "SPN", "QB2A"},
    {0x0004000000171C00, Game::Red, "ITA", "RCYA"},     {0x0004000000171D00, Game::Blue, "ITA", "RCZA"},
    {0x0004000000171E00, Game::Yellow, "ITA", "QBJA"},  {0x0004000000173200, Game::Gold, "ITA", "QB3A"},
    {0x0004000000173300, Game::Silver, "ITA", "QB4A"},  {0x0004000000173400, Game::Crystal, "ITA", "QB5A"},
    // JPN/KOR carts keep 0x10000 bytes of SRAM in the same layout; listed for
    // completeness, Stadium 2 USA rejects them through the ROM language check.
    {0x0004000000170C00, Game::Red, "JPN", "RCLA"},     {0x0004000000170E00, Game::Blue, "JPN", "RCNA"},
    {0x0004000000170F00, Game::Yellow, "JPN", "RCPA"},  {0x0004000000172300, Game::Gold, "JPN", "QBLA"},
    {0x0004000000172400, Game::Silver, "JPN", "QBMA"},  {0x0004000000172500, Game::Crystal, "JPN", "QBNA"},
    {0x0004000000173500, Game::Gold, "KOR", "QB6A"},    {0x0004000000173600, Game::Silver, "KOR", "QB7A"},
};
const size_t title_count = sizeof titles / sizeof titles[0];

namespace {
uint64_t g_found = 0;

std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

#ifdef __3DS__
// Other title's save: ARCHIVE_USER_SAVEDATA + {mediatype, low, high}, as
// Checkpoint 3ds/source/archive.cpp:68-78 and PKSM 3ds/source/io/Archive.cpp:705-711.
bool open_archive(uint64_t tid, FS_Archive* a) {
    const u32 path[3] = {MEDIATYPE_SD, u32(tid), u32(tid >> 32)};
    return R_SUCCEEDED(FSUSER_OpenArchive(a, ARCHIVE_USER_SAVEDATA, {PATH_BINARY, sizeof path, path}));
}

// The VC's GB save is the file /sav.dat (PKSM 3ds/source/titles/Title.cpp:160-164).
bool vc_get(uint64_t tid, std::vector<uint8_t>* out) {
    FS_Archive a;
    if (!open_archive(tid, &a)) return false;
    Handle h;
    bool ok = R_SUCCEEDED(FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, "/sav.dat"), FS_OPEN_READ, 0));
    if (ok) {
        u64 size = 0;
        ok = R_SUCCEEDED(FSFILE_GetSize(h, &size)) && size >= sram_size && size <= 0x20000;
        if (ok && out) {
            out->resize(size_t(size));
            u32 got = 0;
            ok = R_SUCCEEDED(FSFILE_Read(h, &got, 0, out->data(), u32(size))) && got == size;
        }
        FSFILE_Close(h);
    }
    FSUSER_CloseArchive(a);
    return ok;
}

// Writes only the SRAM part, then commits: without ARCHIVE_ACTION_COMMIT_SAVE_DATA
// the write is lost (PKSM 3ds/source/io/Archive.cpp:744-756, loader.cpp:945-955).
bool vc_put(uint64_t tid, const uint8_t* sram) {
    FS_Archive a;
    if (!open_archive(tid, &a)) return false;
    Handle h;
    bool ok = R_SUCCEEDED(FSUSER_OpenFile(&h, a, fsMakePath(PATH_ASCII, "/sav.dat"), FS_OPEN_WRITE, 0));
    if (ok) {
        u32 put = 0;
        ok = R_SUCCEEDED(FSFILE_Write(h, &put, 0, sram, u32(sram_size), FS_WRITE_FLUSH)) && put == sram_size;
        FSFILE_Close(h);
    }
    ok = ok && R_SUCCEEDED(FSUSER_ControlArchive(a, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0));
    FSUSER_CloseArchive(a);
    return ok;
}
// P45: the first file in the RomFS (3 levels deep) with a valid GB header
// checksum (0x14D = -sum(0x134..0x14C) - 25, Pan Docs "The Cartridge Header").
bool find_gb_rom(const fs::path& dir, std::vector<uint8_t>& out, int depth) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) {
            if (depth < 3 && find_gb_rom(it->path(), out, depth + 1)) return true;
            continue;
        }
        const auto size = it->file_size(ec);
        if (ec || size < 0x8000 || size > 0x800000) continue;
        std::vector<uint8_t> r = read_file(it->path());
        uint8_t x = 0;
        for (size_t i = 0x134; i <= 0x14C && r.size() > 0x150; i++) x = uint8_t(x - r[i] - 1);
        if (r.size() > 0x150 && x == r[0x14D]) {
            std::fprintf(stderr, "tpak: VC ROM %s (%zu bytes)\n", it->path().string().c_str(), r.size());
            out = std::move(r);
            return true;
        }
    }
    return false;
}
#else
fs::path g_sim;

fs::path sim_file(uint64_t tid) {
    char id[17];
    std::snprintf(id, sizeof id, "%016llX", static_cast<unsigned long long>(tid));
    return g_sim / id / "sav.dat";
}

bool vc_get(uint64_t tid, std::vector<uint8_t>* out) {
    auto data = read_file(sim_file(tid));
    if (data.size() < sram_size || data.size() > 0x20000) return false;
    if (out) *out = std::move(data);
    return true;
}

bool vc_put(uint64_t tid, const uint8_t* sram) {
    std::fstream f(sim_file(tid), std::ios::binary | std::ios::in | std::ios::out);  // no create, no truncate
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(sram), std::streamsize(sram_size));
    return bool(f);
}
#endif
}  // namespace

#ifndef __3DS__
void sim_root(const std::string& dir) { g_sim = dir; }
#endif

std::string local_path(const std::string& base, const std::string& name, uint32_t rom_crc) {
    char crc[9];
    std::snprintf(crc, sizeof crc, "%08lX", static_cast<unsigned long>(rom_crc));
    return (fs::path(base) / "gb" / "saves" / (name + "-VC-" + crc + ".sav")).string();
}

bool vc_find_id(uint64_t title_id) {
    g_found = vc_get(title_id, nullptr) ? title_id : 0;
    return g_found != 0;
}

bool vc_find(Game game) {
    for (size_t i = 0; i < title_count; i++)
        if (titles[i].game == game && vc_find_id(titles[i].id)) return true;
    return false;
}

uint64_t found_id() { return g_found; }

bool vc_extract_rom(Game game, const std::string& out_path) {
#ifdef __3DS__
    for (size_t i = 0; i < title_count; i++) {
        if (titles[i].game != game) continue;
        const Result rc = romfsMountFromTitle(titles[i].id, MEDIATYPE_SD, "vcrom");
        if (R_FAILED(rc)) {
            if (vc_get(titles[i].id, nullptr))   // installed (its save opens) but RomFS refused
                std::fprintf(stderr, "tpak: VC %016llX RomFS rc=%08lX\n", static_cast<unsigned long long>(titles[i].id), static_cast<unsigned long>(rc));
            continue;
        }
        std::vector<uint8_t> rom;
        const bool ok = find_gb_rom("vcrom:/", rom, 0);
        if (!ok) {
            std::error_code ec;
            for (fs::directory_iterator it("vcrom:/", ec), end; !ec && it != end; it.increment(ec))
                std::fprintf(stderr, "tpak: VC %016llX RomFS has %s, no GB ROM\n", static_cast<unsigned long long>(titles[i].id), it->path().string().c_str());
        }
        romfsUnmount("vcrom");
        if (!ok) continue;
        std::error_code ec;
        fs::create_directories(fs::path(out_path).parent_path(), ec);
        std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()));
        if (f) return true;
        std::fprintf(stderr, "tpak: cannot write %s\n", out_path.c_str());
    }
    return false;
#else
    (void)game; (void)out_path;
    return false;   // host: no installed titles
#endif
}

bool vc_read(std::vector<uint8_t>& buf) { return g_found && vc_get(g_found, &buf); }

bool vc_write_pending(const std::string& local, const std::vector<uint8_t>& buf) {
    if (buf.size() < sram_size) return false;
    std::error_code ec;
    fs::create_directories(fs::path(local).parent_path(), ec);
    std::ofstream f(local + ".pending", std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(sram_size));
    return bool(f);
}

bool vc_commit(const std::string& local) {
    const std::string pending = local + ".pending";
    const auto data = read_file(pending);
    // A short pending is a cut SD write: the VC save was not touched yet, drop it.
    if (data.size() != sram_size) {
        std::error_code ec;
        fs::remove(pending, ec);
        return false;
    }
    if (!g_found || !vc_put(g_found, data.data())) return false;  // pending kept for the next try
    std::error_code ec;
    fs::remove(local, ec);
    fs::rename(pending, local, ec);
    return !ec;
}
}  // namespace tpak::vc
