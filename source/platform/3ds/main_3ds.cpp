// Pokemon Stadium 2: Recompiled, New Nintendo 3DS entry point (rt64-3ds).
// Modelled on oneup03's Goemon64Recomp3D / BanjoRecomp3D platform/3ds/main_3ds.cpp;
// the game's values come from the desktop recomp's src/main/main.cpp
// (michiiik/pokemonStadiumGSRecomp, GPL-3.0).
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include <3ds.h>

#include "recomp3ds.h"
#include "librecomp/rsp.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultra64.h"
#include "tpak_3ds.h"
#include "gamepak_screen.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();
extern "C" void recomp_handle_tailcalls(uint8_t* rdram, recomp_context* ctx);
// The fork-generated entrypoint ends in a tailcall (jr 0x80001530): drain it,
// upstream runtime does not know tailcalls (boot hang, Azahar 19/11).
static void s2_entrypoint(uint8_t* rdram, recomp_context* ctx) {
    recomp_entrypoint(rdram, ctx);
    recomp_handle_tailcalls(rdram, ctx);
}                     // generated/lookup.cpp
extern RspUcodeFunc aspMain_ps2;                  // rsp/aspMain_ps2.cpp: the game's own audio microcode
extern RspUcodeFunc njpgdspMain;                  // rsp/njpgdspMain.cpp: the JPEG task (Pokemon photos)
extern RspUcodeFunc gbTowerMain;                  // rsp/gbTowerMain.cpp: GB Tower (Game Boy on the RSP)
extern RspUcodeFunc gbTowerColorMain;             // rsp/gbTowerColorMain.cpp: GB Tower, Game Boy Color

namespace pokestadium {
    void register_overlays();                     // src/main/register_overlays.cpp
    void register_runtime_trampolines();
}
namespace recomp3ds { void settings_menu_redraw(); }   // rt64-3ds recomp3ds_internal.h: sets a flag only

namespace {

using namespace recomp3ds;

// The Stadium 1 3DS port's map (u/Davzzzd), read from its binary (ps.3dsx,
// table at 0x0011A4D8) and confirmed on hardware: A=A, B=B, X=Z, Y=C-down,
// L=L, R=R, START=START, the D-pad = the C buttons, the C-Stick = the N64
// D-pad. The C-Stick's KEY_CSTICK_* bits come in hidKeysHeld on a New 3DS,
// so the map takes them as keys and s2_on_init turns off rt64-3ds's own
// C-Stick-as-C-buttons. The GB Tower gets kGbTower (below).
const ButtonMap kButtons[] = {
    { KEY_A, N64_A }, { KEY_B, N64_B }, { KEY_X, N64_Z }, { KEY_L, N64_L }, { KEY_R, N64_R },
    { KEY_START, N64_START },
    { KEY_DUP, N64_CUP }, { KEY_DDOWN | KEY_Y, N64_CDOWN }, { KEY_DLEFT, N64_CLEFT }, { KEY_DRIGHT, N64_CRIGHT },
    { KEY_CSTICK_UP, N64_DUP }, { KEY_CSTICK_DOWN, N64_DDOWN },
    { KEY_CSTICK_LEFT, N64_DLEFT }, { KEY_CSTICK_RIGHT, N64_DRIGHT },
    // D39 (nexus): the S2's menus read only the D-pad, so the Circle Pad also
    // presses it past the HID's own threshold (KEY_CPAD_* in hidKeysHeld; the
    // AUTOTEST STICK_* keys set the same bits). The stick's analog value still
    // goes to the game as before.
    { KEY_CPAD_UP, N64_DUP }, { KEY_CPAD_DOWN, N64_DDOWN },
    { KEY_CPAD_LEFT, N64_DLEFT }, { KEY_CPAD_RIGHT, N64_DRIGHT },
};

// D31b (nexus): the S1's GB Tower map (its scene 38): A, B, Y=Z, START,
// SELECT=L, L, R, the D-pad = the N64 D-pad, the C-Stick = the C buttons.
// No scene id needed: the GB Tower is the only screen that runs the
// gbTowerMain/gbTowerColorMain microcode, so get_rsp_microcode switches the
// map in on its first task and tpak_flush_loop switches it back after 1 s
// without one. shortcut: the GB Tower's menus before a game starts keep the
// normal map; read the S2 scene id if that bothers anyone.
const ButtonMap kGbTower[] = {
    { KEY_A, N64_A }, { KEY_B, N64_B }, { KEY_Y, N64_Z }, { KEY_START, N64_START },
    { KEY_SELECT, N64_L }, { KEY_L, N64_L }, { KEY_R, N64_R },
    { KEY_DUP, N64_DUP }, { KEY_DDOWN, N64_DDOWN }, { KEY_DLEFT, N64_DLEFT }, { KEY_DRIGHT, N64_DRIGHT },
    { KEY_CSTICK_UP, N64_CUP }, { KEY_CSTICK_DOWN, N64_CDOWN },
    { KEY_CSTICK_LEFT, N64_CLEFT }, { KEY_CSTICK_RIGHT, N64_CRIGHT },
};
volatile uint64_t g_gb_task_tick = 0;   // svcGetSystemTick of the last GB Tower task
volatile bool g_gb_map = false;
void s2_gb_task_seen() {
    g_gb_task_tick = svcGetSystemTick();
    if (!g_gb_map) { g_gb_map = true; input_set_map(kGbTower, sizeof(kGbTower) / sizeof(kGbTower[0])); }
}

const MenuDefault kDefaults[] = {
    { "separation", 30 },               // 3D depth: a menu-heavy game, mild by default
    // The S1 reads the Circle Pad linearly (x80/156) with no deadzone; the
    // runtime's convert_to_n64_range gives ~82 at full travel.
    { "stick_deadzone", 0 },            // percent
    { "cstick_deadzone", 20 },          // percent
};

// The Transfer Pak rows of the S1's touch screen (tpak_3ds.h menu_*), on the
// Game page. A search changes the other rows too: redraw the page.
template <int Row> void s2_tpak_row(int v) {
    tpak::menu_changed(Row, v);
    recomp3ds::settings_menu_redraw();
}

// The desktop's dispatch (main.cpp get_rsp_microcode). The desktop tells the
// GB Tower tasks by their boot code's size and first two words; the 3DS
// callback gets no rdram, and the boot sizes alone (0xA40, 0x9E4) differ
// from every other task's.
// ponytail: boot size only; add the two-word check if a task ever collides.
// rt64-3ds's timed_get_ucode takes the first microcode it sees as the audio
// one (recomp3ds_run.cpp:116). With audio_hle off that only decides which
// task is timed; before turning audio_hle on, make sure the audio task comes
// first (or patch that line to check M_AUDTASK).
// D11 Suspeito 3: an unknown task ends as done instead of nullptr (run_task
// would fail it); shortcut: drops the task's work, until nmr 0042 (D9 item 2).
RspExitReason s2_empty_ucode(uint8_t*, uint32_t) { return RspExitReason::Broke; }

// P10: Game_DoCopyProtection (func_80064C20) reads the ROM word at 0xB0000E38
// straight from the cart. The desktop mirrors the ROM at rdram+0x30000000
// (mac_pi.cpp); the 3DS masks RDRAM to 16 MB, so the read hit RDRAM junk, the
// check returned -0x10 and Game_Thread spun at 0x80064FD4 forever (0 dl/s).
// The US ROM word is 0x1060828A (low half 0x828A, checked), so it passes:
// return the state unchanged. Linked with -Wl,--wrap=func_80064C20.
extern "C" void __wrap_func_80064C20(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    ctx->r2 = (gpr)(int32_t)ctx->r4;
}

#ifdef S2_AUDIO_DIAG
// P17: aspMain_ps2 fails ~1 task / 3 s with the DMEM dispatch table (0x10..0x3F)
// and the command window (0x2C0..0x33F) overwritten by PCM. This wrapper keeps
// the task's command list and DMEM as loaded, and on a failure prints both, so
// the log shows whether the list in RDRAM was already bad or a handler wrote
// out of bounds. run_task already put the OSTask at DMEM 0xFC0 and ucode_data at 0.
// shortcut: first 4 failures only, list capped at 512 commands; remove after P14.
uint32_t s2_dmem_w(const uint8_t* m, uint32_t a) {   // DMEM bytes are stored ^3
    return (uint32_t)m[(a + 0) ^ 3] << 24 | (uint32_t)m[(a + 1) ^ 3] << 16 |
           (uint32_t)m[(a + 2) ^ 3] << 8 | (uint32_t)m[(a + 3) ^ 3];
}
RspExitReason aspMain_ps2_diag(uint8_t* rdram, uint32_t ucode_addr) {
    static uint32_t fails = 0, tasks = 0, last_ok_ptr = 0, last_ok_size = 0;
    static uint8_t dmem_in[0x1000];
    static uint32_t list[1024];
    const OSTask* t = reinterpret_cast<const OSTask*>(&dmem[0xFC0]);
    const uint32_t ptr = (uint32_t)t->t.data_ptr, size = (uint32_t)t->t.data_size;
    uint32_t n = size / 4 > 1024 ? 1024 : size / 4, bad_in_rdram = 0;
    for (uint32_t i = 0; i < n; i++) {   // rdram words are host-native (MEM_W)
        list[i] = *reinterpret_cast<const uint32_t*>(rdram + ((ptr + i * 4) & 0x7FFFFC));
        if (i % 2 == 0 && ((list[i] >> 24) & 0x7F) >= 0x18) bad_in_rdram++;
    }
    memcpy(dmem_in, dmem, sizeof(dmem_in));
    tasks++;
    const RspExitReason r = aspMain_ps2(rdram, ucode_addr);
    if (r == RspExitReason::Broke) { last_ok_ptr = ptr; last_ok_size = size; return r; }
    if (++fails > 4) return r;
    fprintf(stderr, "[S2AUD] fail #%lu at task %lu: data_ptr %08lX size %lX ucode_data %08lX"
            " | bad opcodes already in RDRAM list: %lu | last ok task ptr %08lX size %lX\n",
            (unsigned long)fails, (unsigned long)tasks, (unsigned long)ptr, (unsigned long)size,
            (unsigned long)(uint32_t)t->t.ucode_data, (unsigned long)bad_in_rdram,
            (unsigned long)last_ok_ptr, (unsigned long)last_ok_size);
    for (uint32_t a = 0x10; a < 0x40; a += 4)   // dispatch table: as loaded -> now
        fprintf(stderr, "[S2AUD] tbl %03lX %08lX -> %08lX\n", (unsigned long)a,
                (unsigned long)s2_dmem_w(dmem_in, a), (unsigned long)s2_dmem_w(dmem, a));
    for (uint32_t a = 0x2C0; a < 0x340; a += 8)  // command window now
        fprintf(stderr, "[S2AUD] win %03lX %08lX %08lX\n", (unsigned long)a,
                (unsigned long)s2_dmem_w(dmem, a), (unsigned long)s2_dmem_w(dmem, a + 4));
    for (uint32_t i = 0; i + 1 < n; i += 2)
        fprintf(stderr, "[S2AUD] cmd %3lu %02lX %08lX %08lX\n", (unsigned long)(i / 2),
                (unsigned long)((list[i] >> 24) & 0x7F), (unsigned long)list[i], (unsigned long)list[i + 1]);
    return r;
}
#endif

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    const uint32_t boot_size = (uint32_t)task->t.ucode_boot_size;
    if (task->t.type != 1) {   // 0082 [Claude]: diagnostic, first 40 non-graphics tasks
        static unsigned n82;
        if (n82 < 40) { n82++; fprintf(stderr, "[S2] task t%lu f%lX boot %08lX/%lX uc %08lX/%lX data %08lX/%lX\n",
            (unsigned long)task->t.type, (unsigned long)task->t.flags, (unsigned long)task->t.ucode_boot, (unsigned long)boot_size,
            (unsigned long)task->t.ucode, (unsigned long)task->t.ucode_size, (unsigned long)task->t.data_ptr, (unsigned long)task->t.data_size); }
    }
    if (task->t.ucode_boot != 0 && boot_size == 0xA40) { s2_gb_task_seen(); return gbTowerMain; }
    if (task->t.ucode_boot != 0 && boot_size == 0x9E4) { s2_gb_task_seen(); return gbTowerColorMain; }
    switch (task->t.type) {
#ifdef S2_AUDIO_DIAG
        case M_AUDTASK:   return aspMain_ps2_diag;
#else
        case M_AUDTASK:   return aspMain_ps2;
#endif
        case M_NJPEGTASK: return njpgdspMain;
        default:
            fprintf(stderr, "[S2] unknown RSP task type %" PRIu32 "\n", task->t.type);
            return s2_empty_ucode;
    }
}

constexpr const char* kSdDir = "sdmc:/3ds/PokemonStadium2";
volatile bool g_tpak_run = true;
Thread g_tpak_thread = nullptr;
extern "C" volatile uint32_t s2_last_loop_pc;

// The S1 writes the Game Boy saves back every couple of seconds and on exit
// (tpak/COMPORTAMENTO-S1.md); tpak:: holds its own lock.
void tpak_flush_loop(void*) {
    while (g_tpak_run) {
        svcSleepThread(2000000000LL);
        if (!g_tpak_run) break;   // exit already flushing (D7 A5)
        tpak::flush_all();
        if (g_gb_map && svcGetSystemTick() - g_gb_task_tick > SYSCLOCK_ARM11) {   // left the GB Tower
            g_gb_map = false;
            input_set_map(kButtons, sizeof(kButtons) / sizeof(kButtons[0]));
        }
        static uint32_t last = 0;   // boot-hang diagnosis: where the game loops
        const uint32_t pc = s2_last_loop_pc;
        fprintf(stderr, "[S2] loop pc %08lX%s\n", (unsigned long)pc, pc == last ? " (same)" : "");
        last = pc;
    }
}

// P41: the S1 opens its Transfer Pak screen before the game (ref/s1_strings.txt:
// "Setting up Transfer Pak", "Connected to Game Pak", "Search saves", "Start
// game"). Here: the Game page of the settings menu (the Transfer Pak rows and
// "Start game") opens and the game waits until Start game, A, or the menu is
// closed (SELECT). A scripted run (AUTOTEST.TXT) starts at once.
volatile bool g_start_game = false;
int g_start_value = 0;
void s2_start_row(int) {
    static int calls = 0;
    if (calls++ > 0) g_start_game = true;   // the first call is the settings.ini load
}
void s2_gamepak_screen() {
    if (FILE* f = fopen((std::string(kSdDir) + "/AUTOTEST.TXT").c_str(), "r")) { fclose(f); return; }
    if (gamepak_screen_run()) return;   // P44: the drawn screen; the rows below if it cannot draw
    recomp3ds::settings_menu_request_open(recomp3ds::PageGame);
    fprintf(stderr, "[S2] P41: Game Pak screen, waiting for Start game\n");
    bool seen_open = false;
    u32 prev = hidKeysHeld();
    for (int t = 0; !g_start_game; t++) {
        svcSleepThread(16666667LL);
        const u32 held = hidKeysHeld();
        if (held & ~prev & KEY_A) break;
        prev = held;
        const bool open = recomp3ds::settings_menu_is_open();
        if (open) seen_open = true;
        else if (seen_open || t > 120) break;   // closed with SELECT (or never opened in 2 s)
    }
    recomp3ds::settings_menu_request_close();
    fprintf(stderr, "[S2] P41: start (%s)\n", tpak::status().c_str());
}

void s2_on_init(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
    pokestadium::register_runtime_trampolines();
    recomp3ds::input_set_cstick_buttons(false);    // the C-Stick is the D-pad (kButtons)
    tpak::set_base_dir(kSdDir);
    tpak::load_config();                            // transferpak.cfg: Pak 1..4 ROM + save
    fprintf(stderr, "[S2] tpak: %s\n", tpak::status().c_str());
    s2_gamepak_screen();
    // Joined on exit; 16 KB: std::filesystem + ofstream + fprintf (D7 A5).
    g_tpak_thread = threadCreate(tpak_flush_loop, nullptr, 64 * 1024, 0x18, -2, false);   // 0076: 16 KB overflowed in the Tower   // 0x3F starved behind the busy game thread: loop pc never printed
}

}   // namespace

int main(int argc, char** argv) {
    setvbuf(stderr, NULL, _IONBF, 0);   // boot lento no Azahar: log inteiro mesmo se o emulador for fechado
    (void)argc; (void)argv;

    recomp3ds::GameDesc desc{};
    desc.game_id = u8"stadium2";
    desc.sd_dir = "PokemonStadium2";        // the ROM: sdmc:/3ds/PokemonStadium2/stadium2.z64
    desc.rom_hint = "Pokemon Stadium 2 (USA) z64, md5 1561c75d11cedf356a8ddb1a4a5f9d5d";
    desc.mods_folder = false;
    recomp::Version::from_string("0.1.0-dev", desc.project_version);
    desc.entry = recomp::GameEntry{
        .rom_hash = 0x13DE5F210AA8D502ULL,  // XXH3_64bits of the US ROM (desktop main.cpp)
        .internal_name = "POKEMON STADIUM 2",
        .display_name = "Pokemon Stadium 2",
        .game_id = u8"stadium2",
        .mod_game_id = "pokestadium2",
        .save_type = recomp::SaveType::Flashram,
        .thumbnail_bytes = {},
        .is_enabled = false,
        .decompression_routine = nullptr,
        .has_compressed_code = false,       // the fragments are decompressed by the game itself
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = s2_entrypoint,
        .on_init_callback = s2_on_init,
    };
    desc.rsp.get_rsp_microcode = get_rsp_microcode;
    desc.register_overlays = pokestadium::register_overlays;
    // New 3DS focus: the 64 MB ROM in memory (124 MB mode); room measured later.
    desc.rom_new3ds = recomp3ds::GameDesc::RomPlace::Stream; // 64 MB ROM + exe do not fit the heap (Azahar 19/11: OOM); Nexus D2
    desc.rom_old3ds = recomp3ds::GameDesc::RomPlace::Stream;
    desc.rom_room_mb = 12;
    desc.rom_cache_mb = 8;
    // The Transfer Pak rides the HLE'd pak calls (tpak/tpak_hle_3ds.cpp:
    // osPfsIsPlug, __osPfsGetStatus, __osContRamRead/Write); the runtime's
    // Pak enum has no TransferPak, so the SI status says none.
    desc.controller_pak = ultramodern::input::Pak::None;

    desc.render.game_name = "Pokemon Stadium 2";
    desc.render.gbi = rt64_3ds::Gbi::F3DEX2;
    desc.render.vi_width = 320;
    desc.render.vi_height = 240;
    // The intro and menus draw a 640x480 16-bit framebuffer (G_SETCIMG w640,
    // buffers 0x96080 apart) and Vi_SelectMode switches VI modes at run time:
    // each list is read at the size its VI width names (P22).
    static const rt64_3ds::RenderDesc::FrameSize kS2FrameSizes[] = { { 320, 240 }, { 640, 480 } };
    desc.render.frame_sizes = kS2FrameSizes;
    desc.render.frame_size_count = sizeof(kS2FrameSizes) / sizeof(kS2FrameSizes[0]);
    // No patches tag the projections: no classification rules yet.
    desc.render.rules = nullptr;
    desc.render.rule_count = 0;
    desc.render.unclassified_persp = rt64_3ds::StereoClass::World;
    desc.render.unclassified_ortho = rt64_3ds::StereoClass::Hud;
    desc.render.fix_rect_lr = true;
    // D53 (nexus): the Poke Ball's white flash and fade are a full-frame
    // quad drawn as triangles: unstretched, it covered the 320-wide frame
    // and left the wide screen's margins showing the field (a white box).
    desc.render.stretch_tint_quads = true;
    // D52g (nexus): the camera wipe's black bar at the frame's left edge
    // (x -1.5..0.5, hidden by the TV's overscan) showed as a line at the
    // margin through the Poke Ball's opening.
    desc.render.drop_edge_bars = true;
    // D61 (nexus): the wipe's two scissors open to the wide screen's
    // edges (the margins showed the clear colour for its frames).
    desc.render.scissor_edges_wide = true;
    // D63 (nexus): the iris's four black fill rectangles reach the wide
    // screen's edges (it closed to a black square with the field beside it).
    desc.render.widen_edge_fills = true;
    desc.render.render_to_texture = true;   // photos and the GB Tower screen are drawn into images
    desc.render.crosshair_row = false;
    desc.render.eye_depth16 = true;
    desc.render.intensity_textures16 = true;
    desc.render.early_drawn = true;
    desc.render.full_width_on_screen = true;   // P38: the battle's new 320-wide framebuffers
    desc.render.keep_unfilled = true;   // P37: menus drawn once, then only what changes (team selection)
    // Audio: the game's own recompiled aspMain (no CPU interpreter for its
    // variant is known to match yet). Try audio_hle + AudioHle::Aspmain once
    // the game boots and compare.
    desc.audio_hle = false;
    // D28: the audio task (~30 ms on core 1) is reported complete at once and
    // run from a copy; its samples reach the host when it has really run. Its
    // buffers go through recomp_ai_submit_buffer -> queue_audio_buffer, which
    // holds them for that (Goemon does the same). Without it the scheduler
    // keeps the graphics task back behind every audio task: ~4 VIs a frame.
    desc.audio_async = true;
    desc.button_map = kButtons;
    desc.button_map_count = sizeof(kButtons) / sizeof(kButtons[0]);
    desc.menu_defaults = kDefaults;
    desc.menu_default_count = sizeof(kDefaults) / sizeof(kDefaults[0]);
    static recomp3ds::MenuOption tpak_rows[tpak::menu_rows + 1];
    static const char* const kTpakKeys[] = { "tpak_1", "tpak_2", "tpak_3", "tpak_4", "tpak_search" };
    void (*const tpak_cb[])(int) = { s2_tpak_row<0>, s2_tpak_row<1>, s2_tpak_row<2>, s2_tpak_row<3>, s2_tpak_row<4> };
    static_assert(sizeof(tpak_cb) / sizeof(tpak_cb[0]) == tpak::menu_rows, "one callback per tpak row");
    for (int i = 0; i < tpak::menu_rows; i++)
        tpak_rows[i] = { kTpakKeys[i], tpak::menu_label(i), 0, 1, &tpak::menu_value[i], tpak::menu_names(i), tpak_cb[i] };
    static const char* const kStartNames[] = { "Press >", "Press >" };
    tpak_rows[tpak::menu_rows] = { "tpak_start", "Start game", 0, 1, &g_start_value, kStartNames, s2_start_row };
    desc.menu_options = tpak_rows;
    desc.menu_option_count = tpak::menu_rows + 1;
    desc.old3ds_frame_cap = 1;
#ifdef S2_NULL_RENDERER
    desc.null_renderer = true;
#endif

    const int code = recomp3ds::run(desc);
    g_tpak_run = false;
    if (g_tpak_thread) { threadJoin(g_tpak_thread, U64_MAX); threadFree(g_tpak_thread); }
    tpak::flush_all(true);
    return code;
}
