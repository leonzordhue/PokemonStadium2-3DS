// Pokemon Stadium 2 (3DS) - Game Pak screen on the touch screen, before the game starts.
// P44 ([Claude], 20/11/2026): the maintainer asked for the Stadium 1 port's model, drawn
// in the Stadium 2 look. P44b, after his photo of the S1 screen ("Setting up
// Transfer Pak 1", Pak tabs, "Connected", the cartridge cards Red/Blue/Yellow,
// "Remove", "Start game"; wording also in tpak/COMPORTAMENTO-S1.md): the same
// layout, with the six Game Boy Pokemon games. Drawn by the CPU straight into
// the bottom framebuffer (RGB565, 240x320 column-major, single-buffered by
// consoleInit), with libctru's 8x8 console font; the console and the settings
// menu are held off meanwhile (recomp3ds::screen_take). Redrawn only when
// something changed (a full redraw every frame showed as flicker).
#include <cstdio>
#include <cstring>
#include <string>

#include <3ds.h>

#include "recomp3ds.h"
#include "tpak_3ds.h"
#include "gamepak_screen.h"
#include "gamepak_art.h"   // P46: cartridge sprites + backdrop (claude/p46/gerar_arte.py)

// P44d: the pad scan of the runtime (rt64-3ds platform/recomp3ds_internal.h, same
// declaration): it takes the HID lock, so it is safe while the gfx thread scans too.
namespace recomp3ds {
    struct PadScan { u32 held; bool touching; int touch_x, touch_y; };
    PadScan input_scan_main();
}

namespace recomp3ds { void settings_menu_redraw(); }

namespace {

constexpr int W = 320, H = 240;

constexpr uint16_t rgb(int r, int g, int b) { return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

// Stadium 2 menus: deep blue, gold, light panels (the S1 screen is the same family).
constexpr uint16_t kNavy = rgb(10, 26, 74), kBlue = rgb(40, 84, 180), kSky = rgb(214, 226, 248),
                   kGold = rgb(248, 196, 40), kWhite = rgb(255, 255, 255), kInk = rgb(20, 28, 56),
                   kGrey = rgb(128, 136, 158), kDim = rgb(70, 82, 118), kRed = rgb(214, 48, 48),
                   kGreen = rgb(40, 190, 90), kShadow = rgb(4, 12, 40);

// tpak::PakInfo::game: 0 Red, 1 Blue, 2 Yellow, 3 Gold, 4 Silver, 5 Crystal.
const char* const kGame[6] = { "Red", "Blue", "Yellow", "Gold", "Silver", "Crystal" };
const char* const kGameUp[6] = { "RED", "BLUE", "YELLOW", "GOLD", "SILVER", "CRYSTAL" };

uint16_t* g_fb = nullptr;   // where draw() paints: the back buffer below
uint16_t g_back[320 * 240];   // P46: drawn off screen, then copied whole (the redraw used to show half-drawn = a dark flash)
const u8* g_font = nullptr;

inline void px(int x, int y, uint16_t c) {
    if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) g_fb[x * H + (H - 1 - y)] = c;
}
void fill(int x, int y, int w, int h, uint16_t c) {
    for (int i = x; i < x + w; i++)
        for (int j = y; j < y + h; j++) px(i, j, c);
}
// Rounded panel (P46: radius 4, rows trimmed by a quarter circle) with a 2 px drop shadow.
void panel_flat(int x, int y, int w, int h, uint16_t c) {
    static const int kTrim[4] = { 3, 2, 1, 1 };
    for (int j = 0; j < h; j++) {
        const int t = j < 4 ? kTrim[j] : (h - 1 - j < 4 ? kTrim[h - 1 - j] : 0);
        fill(x + t, y + j, w - 2 * t, 1, c);
    }
}
uint16_t get(int x, int y) { return ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) ? g_fb[x * H + (H - 1 - y)] : 0; }
uint16_t mix(uint16_t a, uint16_t b, int k16) {   // a*(16-k)/16 + b*k/16, per channel
    const int r = (((a >> 11) & 31) * (16 - k16) + ((b >> 11) & 31) * k16) >> 4;
    const int g = (((a >> 5) & 63) * (16 - k16) + ((b >> 5) & 63) * k16) >> 4;
    const int bl = ((a & 31) * (16 - k16) + (b & 31) * k16) >> 4;
    return uint16_t((r << 11) | (g << 5) | bl);
}
void shadow(int x, int y, int w, int h) {
    for (int i = x + 2; i < x + w + 2; i++)
        for (int j = y + 2; j < y + h + 2; j++) px(i, j, mix(get(i, j), 0, 6));
}
void panel(int x, int y, int w, int h, uint16_t c) { shadow(x, y, w, h); panel_flat(x, y, w, h, c); }
// A sprite with 4-bit alpha; dim = blended toward the dim grey (a game not connected).
void blit(int x, int y, int w, int h, const uint16_t* rgb, const uint8_t* a, bool dim) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int k = a[j * w + i];
            if (k == 0) continue;
            uint16_t c = rgb[j * w + i];
            if (dim) c = mix(c, 0x4A6E, 11);
            px(x + i, y + j, k >= 15 ? c : mix(get(x + i, y + j), c, k + 1));
        }
}
// libctru's console font: 8 bytes a glyph, bit 7 = leftmost pixel.
// adv = pixels per glyph (8; 7 packs a label into a narrow card).
void text(int x, int y, const char* s, uint16_t c, int scale = 1, int adv = 8) {
    for (; *s; s++, x += adv * scale) {
        const u8* g = g_font + (u8)*s * 8;
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 8; b++)
                if (g[r] & (0x80 >> b)) fill(x + b * scale, y + r * scale, scale, scale, c);
    }
}
int text_w(const char* s, int scale = 1) { return (int)std::strlen(s) * 8 * scale; }
void text_c(int cx, int y, const char* s, uint16_t c, int scale = 1) { text(cx - text_w(s, scale) / 2, y, s, c, scale); }

// P46: the cartridge sprites (game 0-5; -1 = no cartridge: the empty grey model).
void cart_big(int x, int y, int g, bool lit) {
    if (g < 0 || g >= 6) blit(x, y, gp_art::EMPTY_W, gp_art::EMPTY_H, gp_art::EMPTY_RGB, gp_art::EMPTY_A, false);
    else blit(x, y, gp_art::BIG0_W, gp_art::BIG0_H, gp_art::BIG_RGB[g], gp_art::BIG_A[g], !lit);
}
void cart_small(int x, int y, int g, bool lit) { blit(x, y, gp_art::SMALL0_W, gp_art::SMALL0_H, gp_art::SMALL_RGB[g], gp_art::SMALL_A[g], !lit); }

struct Box { int x, y, w, h; bool in(int px_, int py_) const { return px_ >= x && px_ < x + w && py_ >= y && py_ < y + h; } };
const Box kTab[4] = { { 4, 4, 52, 20 }, { 58, 4, 52, 20 }, { 112, 4, 52, 20 }, { 166, 4, 52, 20 } };
const Box kDetail = { 4, 28, 312, 84 };
const Box kCard[6] = { { 5, 118, 50, 58 }, { 57, 118, 50, 58 }, { 109, 118, 50, 58 },
                       { 161, 118, 50, 58 }, { 213, 118, 50, 58 }, { 265, 118, 50, 58 } };
const Box kRemove = { 4, 182, 102, 26 }, kSearch = { 4, 211, 102, 26 }, kStart = { 112, 182, 204, 55 };

struct State {
    int pak = 0;            // the tab shown
    bool armed = false;     // Remove pressed once ("Tap again to disconnect")
    tpak::PakInfo info[4];
    std::string status;
    bool operator==(const State& o) const {
        if (pak != o.pak || armed != o.armed || status != o.status) return false;
        for (int i = 0; i < 4; i++) {
            const tpak::PakInfo &a = info[i], &b = o.info[i];
            if (a.present != b.present || a.game != b.game || a.id != b.id || a.vc != b.vc || std::strcmp(a.trainer, b.trainer)) return false;
        }
        return true;
    }
};

void draw(const State& s) {
    // Backdrop (P46): navy gradient, diagonal stripes, a faint ball (gamepak_art.h).
    for (int x = 0; x < W; x++)
        for (int y = 0; y < H; y++) g_fb[x * H + (H - 1 - y)] = gp_art::BG[y * W + x];

    // Pak tabs, and the pak's state on the right (S1: "Pak 1  Pak 2 ... Connected").
    for (int i = 0; i < 4; i++) {
        const bool cur = i == s.pak;
        panel(kTab[i].x, kTab[i].y, kTab[i].w, kTab[i].h, cur ? kGold : kNavy);
        char b[8];
        std::snprintf(b, sizeof b, "Pak %d", i + 1);
        if (s.info[i].present) fill(kTab[i].x + 5, kTab[i].y + 8, 4, 4, kGreen);
        text(kTab[i].x + 12, kTab[i].y + 6, b, cur ? kNavy : kWhite);
    }
    const tpak::PakInfo& p = s.info[s.pak];
    panel(224, 4, 92, 20, p.present ? kGreen : kDim);
    text_c(270, 10, p.present ? "Connected" : "Empty", kWhite);

    // The pak's cartridge (S1: "Setting up Transfer Pak 1", title, "Connected to Game Pak 1", trainer, ID, ROM, "ROM and save found").
    panel(kDetail.x, kDetail.y, kDetail.w, kDetail.h, kSky);
    char line[48];
    std::snprintf(line, sizeof line, "Setting up Transfer Pak %d", s.pak + 1);
    text(kDetail.x + 8, kDetail.y + 5, line, kBlue);
    if (!p.present) {
        cart_big(kDetail.x + 6, kDetail.y + 20, -1, false);
        text(kDetail.x + 70, kDetail.y + 34, "No cartridge", kInk, 2);
        text(kDetail.x + 70, kDetail.y + 56, "Search saves to look again", kGrey);
    }
    else {
        const int g = p.game >= 0 && p.game < 6 ? p.game : -1;
        cart_big(kDetail.x + 6, kDetail.y + 20, g, true);
        text(kDetail.x + 70, kDetail.y + 18, "Pokemon", kBlue);
        text(kDetail.x + 70, kDetail.y + 28, g >= 0 ? kGame[g] : "?", kInk, 2);
        fill(kDetail.x + 70, kDetail.y + 47, 6, 6, kGreen);
        std::snprintf(line, sizeof line, "Connected to Game Pak %d", s.pak + 1);
        text(kDetail.x + 80, kDetail.y + 46, line, kInk);
        if (p.trainer[0]) {
            std::snprintf(line, sizeof line, "%s   ID %05u", p.trainer, p.id);
            text(kDetail.x + 70, kDetail.y + 58, line, kInk);
        }
        text(kDetail.x + 70, kDetail.y + 70, p.vc ? "VC save found" : "ROM and save found", kBlue);
    }

    // The six games (S1: the cartridge cards); lit = connected in some pak, a check on this pak's.
    for (int g = 0; g < 6; g++) {
        const Box& b = kCard[g];
        int in_pak = -1;
        for (int i = 0; i < 4; i++) if (s.info[i].present && s.info[i].game == g) { in_pak = i; break; }
        const bool here = p.present && p.game == g;
        if (here) panel(b.x - 2, b.y - 2, b.w + 4, b.h + 4, kGold);
        panel(b.x, b.y, b.w, b.h, in_pak >= 0 ? kSky : kNavy);
        cart_small(b.x + (b.w - gp_art::SMALL0_W) / 2, b.y + 2, g, in_pak >= 0);
        const int lw = (int)std::strlen(kGame[g]) * 7 - 1;   // 7 px advance: "Crystal" fits in 50
        text(b.x + (b.w - lw) / 2, b.y + 38, kGame[g], in_pak >= 0 ? kInk : kGrey, 1, 7);
        if (in_pak >= 0) {
            char t[4];
            std::snprintf(t, sizeof t, "P%d", in_pak + 1);
            text_c(b.x + b.w / 2, b.y + 48, t, kBlue);
        }
        if (here) { panel(b.x + b.w - 12, b.y + 2, 10, 10, kGreen); text(b.x + b.w - 11, b.y + 3, "\xfb", kWhite); }
    }

    // Buttons (S1: Remove, Options/Search, Start game in yellow) and the hints.
    panel(kRemove.x, kRemove.y, kRemove.w, kRemove.h, s.armed ? kRed : kBlue);
    text_c(kRemove.x + kRemove.w / 2, kRemove.y + 9, s.armed ? "Tap again" : "B Remove", kWhite);
    panel(kSearch.x, kSearch.y, kSearch.w, kSearch.h, kBlue);
    text_c(kSearch.x + kSearch.w / 2, kSearch.y + 9, "X Search", kWhite);
    panel(kStart.x, kStart.y, kStart.w, kStart.h, kGold);
    text_c(kStart.x + kStart.w / 2 + 1, kStart.y + 13, "START GAME", kShadow, 2);
    text_c(kStart.x + kStart.w / 2, kStart.y + 12, "START GAME", kNavy, 2);
    const std::string st = s.status.substr(0, 24);
    text_c(kStart.x + kStart.w / 2, kStart.y + 38, st.c_str(), kNavy);
}

State snapshot(int pak, bool armed) {
    State s;
    s.pak = pak;
    s.armed = armed;
    for (int i = 0; i < 4; i++) s.info[i] = tpak::pak_info(i);
    s.status = tpak::status();
    return s;
}

}   // namespace

bool gamepak_screen_run() {
    u16 fw = 0, fh = 0;
    uint16_t* const screen = (uint16_t*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh);
    g_fb = g_back;
    PrintConsole* def = consoleGetDefault();
    g_font = def != nullptr ? def->font.gfx : nullptr;
    if (screen == nullptr || g_font == nullptr || fw != H || fh != W || gfxGetScreenFormat(GFX_BOTTOM) != GSP_RGB565_OES) {
        std::fprintf(stderr, "[S2] P44: no RGB565 bottom framebuffer (%ux%u): the menu rows instead\n", fw, fh);
        return false;
    }
    recomp3ds::screen_take(true);
    std::fprintf(stderr, "[S2] P44: Game Pak screen\n");
    int pak = 0;
    for (int i = 0; i < 4; i++) if (tpak::has_pak(i)) { pak = i; break; }
    bool armed = false, touching = false, have = false;
    State shown;
    u32 prev = recomp3ds::input_scan_main().held;
    int tx0 = 0, ty0 = 0;
    for (int frame = 0;; frame++) {
        const State now = snapshot(pak, armed);
        if (!have || !(now == shown) || frame % 120 == 0) {   // every 2 s too, in case something drew over it
            draw(now);
            std::memcpy(screen, g_back, W * H * 2);
            svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)screen, W * H * 2);
            shown = now;
            have = true;
        }
        svcSleepThread(16666667LL);
        // P44d: this loop scans the pad itself; nobody else does before the game
        // starts (the console-7 stayed on this screen: START never arrived).
        const recomp3ds::PadScan scan = recomp3ds::input_scan_main();
        const u32 held = scan.held;
        // P44e: log every change of the pad (diagnosis: the console-7/7b/7c stayed here).
        if (held != prev || frame % 600 == 0)
            std::fprintf(stderr, "[S2] P44: frame %d keys %08lx touch %d,%d\n", frame, (unsigned long)held, scan.touch_x, scan.touch_y);
        const u32 down = held & ~prev;
        prev = held;
        enum { None, Start, Remove, Search, Tab, Card } act = None;
        int arg = 0;
        if (down & (KEY_START | KEY_A)) act = Start;
        else if (down & KEY_B) act = Remove;
        else if (down & KEY_X) act = Search;
        else if (down & (KEY_LEFT | KEY_L)) { act = Tab; arg = (pak + 3) % 4; }
        else if (down & (KEY_RIGHT | KEY_R)) { act = Tab; arg = (pak + 1) % 4; }
        touchPosition tp{};
        tp.px = (u16)scan.touch_x; tp.py = (u16)scan.touch_y;
        const bool t = (held & KEY_TOUCH) != 0;
        if (t && !touching) { tx0 = tp.px; ty0 = tp.py; }
        if (!t && touching) {   // acts on release, on what the touch began on
            if (kStart.in(tx0, ty0)) act = Start;
            else if (kRemove.in(tx0, ty0)) act = Remove;
            else if (kSearch.in(tx0, ty0)) act = Search;
            for (int i = 0; i < 4; i++) if (kTab[i].in(tx0, ty0)) { act = Tab; arg = i; }
            for (int g = 0; g < 6; g++) if (kCard[g].in(tx0, ty0)) { act = Card; arg = g; }
        }
        touching = t;
        if (act == None) continue;
        if (act != Remove) armed = false;
        if (act == Start) break;
        if (act == Tab) pak = arg;
        else if (act == Card) {   // a connected game: show the pak it is in
            for (int i = 0; i < 4; i++) if (now.info[i].present && now.info[i].game == arg) pak = i;
        }
        else if (act == Search) tpak::search_saves();
        else if (act == Remove && tpak::has_pak(pak)) {
            if (!armed) armed = true;
            else { tpak::set_cart(pak, "", ""); armed = false; }
        }
    }
    std::memset(screen, 0, W * H * 2);
    svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)screen, W * H * 2);
    recomp3ds::screen_take(false);
    recomp3ds::settings_menu_redraw();
    std::fprintf(stderr, "[S2] P44: start (%s)\n", tpak::status().c_str());
    return true;
}
