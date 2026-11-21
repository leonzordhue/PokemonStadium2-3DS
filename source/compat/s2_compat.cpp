// s2_compat.cpp — Pokémon Stadium 2 (código gerado pelo fork michiiik/mstan do N64Recomp)
// sobre o N64ModernRuntime upstream + nmr-patches do rt64-3ds (3DS).
//
// Implementa os símbolos que o código gerado chama e o upstream não tem.
// Origem (GPL-3.0): mstan/N64ModernRuntime f61d5424 (librecomp/src/recomp.cpp,
// librecomp/src/overlays.cpp) e michiiik/pokemonStadiumGSRecomp (mac_ai.cpp).
// Diagnósticos do fork (cfdiag, probes, dumps, JIT/tcc, interpretador) ficaram de fora.
//
// Precisa: compat/include/recomp.h antes de N64Recomp/include no include path,
// e compat/patches/0001-sections-content-hash.patch aplicado no runtime.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "recomp.h"
#include "librecomp/sections.h"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"

// overlays.cpp do upstream: definida sem static, ausente do header.
void load_overlay(size_t section_table_index, int32_t ram);

namespace {

uint8_t* g_rdram = nullptr;                  // capturado na primeira chamada que recebe rdram
SectionTableEntry* g_sections = nullptr;     // a mesma tabela passada a register_overlays
size_t g_num_sections = 0;
std::mutex g_frag_mutex;

constexpr uint32_t kRdramSize = 8u * 1024u * 1024u;          // RDRAM do N64 (cabeçalho do fragmento)
constexpr uint32_t kMaxRuntimeCodeSectionSize = 8u * 1024u * 1024u;
constexpr uint32_t kSyntheticPoolBase = 0xC0000000u;          // variantes por padrão (fork)
constexpr uint32_t kSyntheticPoolEnd = 0xE8B00000u;   // 651 variantes (port web; nexus/D18); slot 651 = frag_81000000 fica fora
constexpr uint32_t kFragMagicA = 0x46524147u;                 // "FRAG"
constexpr uint32_t kFragMagicB = 0x4D454E54u;                 // "MENT"

struct LiveFragment {
    size_t section_index;
    int32_t base;
    uint32_t id;
    std::vector<std::pair<int32_t, recomp_func_t*>> slots;   // trampolins J+nop instalados (D20), runtime e link
};
std::vector<LiveFragment> g_live;            // o que este arquivo registrou

bool is_runtime_code_section(const SectionTableEntry& s) {
    return s.size != 0 && s.size <= kMaxRuntimeCodeSectionSize;
}
// P53 [Proton]: seções acrescentadas depois do slot 651 (frag_81000000, 0xE8B00000) também são sintéticas
// (ex.: frag_81500000 da GB Tower em 0xE8C00000, original_pattern_id 0x5); sem isso pick_section dá cand=0.
bool is_synthetic_addr(uint32_t a) { return (a >= kSyntheticPoolBase && a < kSyntheticPoolEnd) || a >= 0xE8C00000u; }

// RDRAM guarda palavras em ordem do host: byte N64 em (addr ^ 3).
uint32_t rd_be32(const uint8_t* rdram, uint32_t paddr) {
    return ((uint32_t)rdram[(paddr + 0) ^ 3] << 24) | ((uint32_t)rdram[(paddr + 1) ^ 3] << 16) |
           ((uint32_t)rdram[(paddr + 2) ^ 3] << 8) | (uint32_t)rdram[(paddr + 3) ^ 3];
}

uint64_t fnv1a_64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

// Base de link de um fragmento (bucket 0x8{id+0x10}00000); para variante sintética, a do id original.
uint32_t link_base_of(const SectionTableEntry& s) {
    if (is_synthetic_addr(s.ram_addr)) return 0x80000000u | ((s.original_pattern_id + 0x10u) << 20);
    return s.ram_addr;
}

// Despejo exato de uma instância (patch 0002). unload_overlays(base, 1) do upstream aborta com
// "Cannot partially unload section", e vizinhos colados também (teste >=): revisão D6 itens 1-3.
void evict_one(const LiveFragment& l) {
    const SectionTableEntry& s = g_sections[l.section_index];
    recomp::overlays::remove_loaded_section(l.section_index, l.base);
    uint32_t link = link_base_of(s);
    if ((int32_t)link != l.base)     // aliases no endereço de link: só os que ainda apontam para esta variante
        for (size_t f = 0; f < s.num_funcs; f++)
            if (s.funcs[f].func) recomp::overlays::remove_loaded_function((int32_t)(link + s.funcs[f].offset), s.funcs[f].func);
    for (const auto& sl : l.slots) recomp::overlays::remove_loaded_function(sl.first, sl.second);   // só se ainda apontar para esta variante
}

// Trampolins J+nop da tabela de saltos do fragmento, em [0x20, 1a função real) (fork overlays.cpp:771,
// scan_fragment_section_trampolines). Lidos antes da relocação (hook na entrada de func_80002210), então o J
// está no espaço de link. Ex.: 0x80243A90 = base+0x140 (nexus/D18). Devolve o que instalou, para o evict_one.
std::vector<std::pair<int32_t, recomp_func_t*>> install_slots(uint8_t* rdram, const SectionTableEntry& s, uint32_t link, int32_t base) {
    std::vector<std::pair<int32_t, recomp_func_t*>> out;
    const uint32_t pa = (uint32_t)base & 0x1FFFFFFFu;
    if (pa + 0x10 > kRdramSize || rd_be32(rdram, pa + 0x08) != kFragMagicA || rd_be32(rdram, pa + 0x0C) != kFragMagicB) return out;
    uint32_t first = s.size;
    for (size_t f = 0; f < s.num_funcs; f++) {
        const FuncEntry& fe = s.funcs[f];
        const bool may_be_slot = fe.offset >= 0x20 && (fe.offset & 7u) == 0 && fe.rom_size == 8;   // fork func_entry_may_be_fragment_jump_slot
        if (!may_be_slot && fe.offset >= 0x20 && fe.offset < first) first = fe.offset;
    }
    for (uint32_t off = 0x20; off + 8 <= first && pa + off + 8 <= kRdramSize; off += 8) {
        const uint32_t ins = rd_be32(rdram, pa + off);
        if (rd_be32(rdram, pa + off + 4) != 0 || ((ins >> 26) & 0x3Fu) != 0x02u) continue;
        const uint32_t tgt = ((link + off + 4) & 0xF0000000u) | ((ins & 0x03FFFFFFu) << 2);
        if (tgt < link || tgt >= link + s.size) continue;   // shortcut: só alvo no mesmo fragmento; o cruzado fica no interpretador
        bool recompiled_slot = false;   // o slot já é FuncEntry (rom_size 8): o load_overlay já o instalou
        for (size_t f = 0; f < s.num_funcs; f++) if (s.funcs[f].func && s.funcs[f].offset == off) recompiled_slot = true;
        if (recompiled_slot) continue;
        for (size_t f = 0; f < s.num_funcs; f++) {
            if (s.funcs[f].func == nullptr || s.funcs[f].offset != tgt - link) continue;
            const int32_t addrs[2] = { base + (int32_t)off, (int32_t)(link + off) };
            for (int k = 0; k < ((int32_t)link != base ? 2 : 1); k++) {
                recomp::overlays::add_loaded_function(addrs[k], s.funcs[f].func);
                out.push_back({addrs[k], s.funcs[f].func});
            }
            break;
        }
    }
    return out;
}

// P53l [Proton]: bytes a fragment takes in RAM (FRAGMENT header: larger of +0x18/+0x1C; text + data + bss);
// 0 when the header does not match. The section size alone is only the text.
uint32_t frag_ram_size(uint8_t* rdram, int32_t base) {
    const uint32_t pa = (uint32_t)base & 0x1FFFFFFFu;
    if (pa + 0x20 > kRdramSize || rd_be32(rdram, pa + 0x08) != kFragMagicA || rd_be32(rdram, pa + 0x0C) != kFragMagicB) return 0;
    const uint32_t a = rd_be32(rdram, pa + 0x18), b = rd_be32(rdram, pa + 0x1C);
    return a > b ? a : b;
}

// 0069/P53k [Claude]: also evict every live fragment whose RAM range overlaps the new one
// (new range covers the old base). The GB Tower (frag 0x5, 0x801495F0+0x2E950)
// is loaded over frag 0x8/0x6; with only the exact-base test their stale native funcs kept answering
// lookups in 0x8015xxxx and a new thread jumped into frag 0x8 code, then to 0x0.
void evict_at(int32_t base, size_t keep_index, uint32_t size = 0) {
    const uint32_t lo = (uint32_t)base, hi = lo + size;
    for (auto it = g_live.begin(); it != g_live.end();) {
        // Only when the new range covers the old BASE: frag 0x8 is loaded over the tail of frag 0x6
        // (0x801514D0 inside 0x801451C0..0x80152930) while 0x6 stays in use (teste-p53k, 20261121-150643).
        const uint32_t olo = (uint32_t)it->base;
        const bool overlaps = size != 0 && lo <= olo && olo < hi;
        if (it->base == base || it->section_index == keep_index || overlaps) {
            evict_one(*it);
            it = g_live.erase(it);
        } else {
            ++it;
        }
    }
}

size_t pick_section(uint8_t* rdram, uint32_t id, int32_t fragment_ptr) {
    std::vector<size_t> cand;
    bool hashed = false;
    for (size_t i = 0; i < g_num_sections; i++) {
        const SectionTableEntry& s = g_sections[i];
        if (!is_runtime_code_section(s)) continue;
        if (is_synthetic_addr(s.ram_addr)) {
            if (s.original_pattern_id == id && s.content_hash != 0) { cand.push_back(i); hashed = true; }
            continue;
        }
        uint32_t bucket = (s.ram_addr & 0x0FF00000u) >> 20;
        if (bucket >= 0x10 && bucket - 0x10 == id) {
            cand.push_back(i);
            hashed |= s.content_hash != 0;
        }
    }

    const uint32_t paddr = (uint32_t)fragment_ptr & 0x1FFFFFFFu;
    const bool hdr_ok = paddr + 0x100 <= kRdramSize;
    uint32_t size_in_ram = 0;
    if (hdr_ok && rd_be32(rdram, paddr + 0x08) == kFragMagicA && rd_be32(rdram, paddr + 0x0C) == kFragMagicB) {
        uint32_t a = rd_be32(rdram, paddr + 0x18), b = rd_be32(rdram, paddr + 0x1C);
        size_in_ram = a > b ? a : b;
    }
    auto fits = [&](size_t i) { return size_in_ram == 0 || g_sections[i].size <= size_in_ram; };

    if (cand.size() == 1 && !hashed) return fits(cand[0]) ? cand[0] : SIZE_MAX;

    // 1) hash FNV-1a dos primeiros 0x100 bytes
    if (!cand.empty() && hdr_ok) {
        uint8_t win[0x100];
        for (uint32_t i = 0; i < sizeof(win); i++) win[i] = rdram[(paddr + i) ^ 3];
        uint64_t h = fnv1a_64(win, sizeof(win));
        for (size_t i : cand) if (g_sections[i].content_hash == h && fits(i)) return i;
    }

    // 2) J do cabeçalho FRAGMENT: link_vram = alvo do J - offset da entrada
    uint32_t link_vram = 0;
    if (hdr_ok && rd_be32(rdram, paddr + 0x08) == kFragMagicA && rd_be32(rdram, paddr + 0x0C) == kFragMagicB) {
        uint32_t j = rd_be32(rdram, paddr);
        if (((j >> 26) & 0x3Fu) == 0x02u) link_vram = (((j & 0x03FFFFFFu) << 2) | 0x80000000u) - rd_be32(rdram, paddr + 0x10);
    }
    if (link_vram != 0) {
        for (size_t i = 0; i < g_num_sections; i++)    // exata primeiro, como o fork (overlays.cpp:1392-1404)
            if (is_runtime_code_section(g_sections[i]) && g_sections[i].ram_addr == link_vram && fits(i)) return i;
        for (size_t i : cand) {
            const SectionTableEntry& s = g_sections[i];
            if (link_vram >= s.ram_addr && link_vram < s.ram_addr + s.size && fits(i)) return i;
        }
    }

    // 3) único candidato que cabe no tamanho do fragmento em RAM
    if (size_in_ram != 0) {
        size_t sole = SIZE_MAX; int n = 0;
        for (size_t i : cand) if (g_sections[i].size <= size_in_ram) { sole = i; n++; }
        if (n == 1) return sole;
    }
    // P13 diagnóstico: por que nenhum hash casa (bytes vivos x hash de build, N64Recomp decompressed.cpp:2436).
    if (hdr_ok) {
        uint8_t w[0x100];
        for (uint32_t i = 0; i < sizeof(w); i++) w[i] = rdram[(paddr + i) ^ 3];
        fprintf(stderr, "[s2] P13 id=0x%X ptr=0x%08X cand=%zu hashed=%d size_in_ram=0x%X live_hash=%016llX link_vram=0x%08X bytes:",
                id, (uint32_t)fragment_ptr, cand.size(), (int)hashed, size_in_ram, (unsigned long long)fnv1a_64(w, sizeof(w)), link_vram);
        for (int i = 0; i < 32; i++) fprintf(stderr, "%s%02X", (i & 3) ? "" : " ", w[i]);
        fprintf(stderr, "\n");
        for (size_t k = 0; k < cand.size() && k < 4; k++)
            fprintf(stderr, "[s2] P13   cand[%zu] ram=0x%08X size=0x%X hash=%016llX\n", k, g_sections[cand[k]].ram_addr,
                    g_sections[cand[k]].size, (unsigned long long)g_sections[cand[k]].content_hash);
    }
    return SIZE_MAX;
}

} // namespace

// ---- registro da tabela (chamar do main_3ds logo após recomp::overlays::register_overlays) ----
extern "C" void s2_compat_set_sections(SectionTableEntry* sections, size_t num_code_sections) {
    g_sections = sections;
    g_num_sections = num_code_sections;
}
extern "C" void s2_compat_set_rdram(uint8_t* rdram) { g_rdram = rdram; fprintf(stderr, "[s2] RDRAM em %p..%p\n", (void*)rdram, (void*)(rdram + 0x1000000)); }

// ---- tailcalls (fork recomp.cpp:583, sem diagnóstico) ----
extern "C" void recomp_handle_tailcalls(uint8_t* rdram, recomp_context* ctx) {
    // tailcall_dispatching = profundidade do trampolim aninhado (o código gerado decide
    // drenar localmente ou subir conforme ela); ++/-- balanceados, por thread (ctx).
    ctx->tailcall_dispatching++;
    uint32_t n = 0;
    while (ctx->tailcall_pending) {
        uint32_t target = ctx->tailcall_target;
        recomp_func_t* func = ctx->tailcall_func;
        ctx->tailcall_pending = 0;
        ctx->tailcall_target = 0;
        ctx->tailcall_func = nullptr;
        if (target == ctx->host_return_target) break;   // volta ao chamador do host
        if (func == nullptr) func = get_function((int32_t)target);
        func(rdram, ctx);
        if ((++n & 0xFFFu) == 0 && *ultramodern::ultramodern_preempt_flag) ultramodern::ultramodern_preempt(rdram);
    }
    ctx->tailcall_dispatching--;
}

#ifndef RECOMP_CF_NOTE_DISABLED
extern "C" void recomp_cf_note(const char*, uint32_t, uint32_t, uint32_t, recomp_context*) {}
#endif

// ---- ponto de checagem de laço sem a macro do 3DS (preempção do nmr 0025/0039) ----
extern "C" void ultramodern_scheduler_tick(void) {
    if (g_rdram != nullptr && *ultramodern::ultramodern_preempt_flag) ultramodern::ultramodern_preempt(g_rdram);
}
extern "C" void ultramodern_scheduler_tick_vram(uint32_t) { ultramodern_scheduler_tick(); }

// ---- código que o recompilador não traduziu: falha alta, não stub silencioso ----
extern "C" void recomp_unhandled_branch(uint8_t*, recomp_context*, uint32_t instr_vram, uint32_t target) {
    fprintf(stderr, "[s2] desvio nao traduzido em 0x%08X -> 0x%08X\n", instr_vram, target);
    abort();
}
extern "C" void recomp_unhandled_instruction(uint8_t*, recomp_context*, uint32_t instr_vram, const char* op) {
    fprintf(stderr, "[s2] instrucao nao traduzida em 0x%08X: %s\n", instr_vram, op ? op : "?");
    abort();
}
#define S2_MISSING(name) \
    extern "C" __attribute__((weak)) void name(uint8_t*, recomp_context*) { \
        fprintf(stderr, "[s2] funcao nao recompilada: " #name "\n"); abort(); }
S2_MISSING(func_80064980)
S2_MISSING(static_5_8000B770)
S2_MISSING(static_272_8334B334)

// ---- áudio: o Stadium 2 escreve AI_DRAM/AI_LEN direto (hook em func_800195C0; mac_ai.cpp:110) ----
extern "C" void recomp_ai_submit_buffer(uint8_t* rdram, uint32_t guest_addr, uint32_t byte_count) {
    if (byte_count == 0) return;
    ultramodern::queue_audio_buffer(rdram, (PTR(int16_t))guest_addr, byte_count);
    // AI_LEN espelhado na RDRAM: com RECOMP_MEM_MASK=0x00FFFFFF cai em 0x500004, o mesmo lugar onde o
    // próprio jogo grava AI_DRAM/AI_LEN e lê AI_STATUS (func_800195C0). Não é só esta linha: ver NOTAS.md (D6 item 4).
    MEM_W(0, (gpr)0xFFFFFFFFA4500004ull) = (int32_t)ultramodern::get_remaining_audio_bytes();   // AI_LEN
}

// ---- acessório do controle: stubs "sem pak". A Frente C (tpak/) fornece os fortes. ----
extern "C" __attribute__((weak)) void __osContRamRead_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }  // PFS_ERR_NOPACK
extern "C" __attribute__((weak)) void __osContRamWrite_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }
extern "C" __attribute__((weak)) void __osPfsGetStatus_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }
extern "C" __attribute__((weak)) void osPfsIsPlug_recomp(uint8_t* rdram, recomp_context* ctx) {
    MEM_B(0, ctx->r5) = 0;   // *bitpattern = nenhum pak
    ctx->r2 = 0;
}

// ---- interpretador (mips_interp.cpp): ganchos do fork sem a telemetria/shadow-diff ----
extern "C" void recomp_capture_interp_target(uint32_t) {}
extern "C" int recomp_shadow_diff_active(void) { return 0; }
extern "C" void recomp_shadow_diff_note_native_call(void) {}

// ---- fragmentos (fork overlays.cpp:1026-1680, v1 sobre a API pública do upstream) ----
extern "C" void recomp_register_runtime_fragment(uint8_t* rdram, uint32_t id, int32_t fragment_ptr) {
    g_rdram = rdram;
    if (g_sections == nullptr || fragment_ptr == 0) return;
    std::lock_guard<std::mutex> lock(g_frag_mutex);
    size_t idx = pick_section(rdram, id, fragment_ptr);
    {   // P12: a OSThread de quem carrega (thread 7) cai dentro do fragmento? (log 172956: id vira 0x8000003A após a 3a carga do 0xEF)
        const uint32_t pa = (uint32_t)fragment_ptr & 0x1FFFFFFFu;
        uint32_t sir = 0;
        if (pa + 0x20 <= kRdramSize) { uint32_t a = rd_be32(rdram, pa + 0x18), b = rd_be32(rdram, pa + 0x1C); sir = a > b ? a : b; }
        const uint32_t t = (uint32_t)ultramodern::this_thread();
        const uint32_t tp = t & 0x1FFFFFFFu;
        const OSThread* th = TO_PTR(OSThread, t);
        fprintf(stderr, "[s2] P12 carga id=0x%X faixa 0x%08X..0x%08X OSThread=0x%08X id=%d pri=%d%s" "\n", id, (uint32_t)fragment_ptr,
                (uint32_t)fragment_ptr + sir, t, (int)th->id, (int)th->priority, (tp >= pa && tp < pa + sir) ? " DENTRO DO FRAGMENTO" : "");
    }
    if (idx == SIZE_MAX) {
        evict_at(fragment_ptr, SIZE_MAX, frag_ram_size(rdram, fragment_ptr));   // P53l: overlap here too
        fprintf(stderr, "[s2] fragmento id=0x%X em 0x%08X sem secao correspondente\n", id, (uint32_t)fragment_ptr);
        return;
    }
    fprintf(stderr, "[s2] fragmento id=0x%X em 0x%08X -> secao %zu ram=0x%08X size=0x%X\n", id, (uint32_t)fragment_ptr, idx,
            g_sections[idx].ram_addr, g_sections[idx].size);
    {   const uint32_t fs = frag_ram_size(rdram, fragment_ptr), ss = (uint32_t)g_sections[idx].size;   // P53l
        evict_at(fragment_ptr, idx, fs > ss ? fs : ss); }
    load_overlay(idx, fragment_ptr);   // endereços de runtime no func_map + section_addresses
    const SectionTableEntry& s = g_sections[idx];
    // Aliases no endereço de link (o evict_one os apaga, patch 0002).
    uint32_t link = link_base_of(s);
    if ((int32_t)link != fragment_ptr)
        for (size_t f = 0; f < s.num_funcs; f++)
            if (s.funcs[f].func) recomp::overlays::add_loaded_function((int32_t)(link + s.funcs[f].offset), s.funcs[f].func);
    auto slots = install_slots(rdram, s, link, fragment_ptr);
    if (!slots.empty()) fprintf(stderr, "[s2] fragmento id=0x%X: %zu trampolins J registrados\n", id, slots.size());
    g_live.push_back({idx, fragment_ptr, id, std::move(slots)});
}

extern "C" void recomp_unregister_runtime_fragment(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_frag_mutex);
    for (auto it = g_live.begin(); it != g_live.end();) {
        if (it->id == id) { evict_one(*it); it = g_live.erase(it); } else ++it;
    }
}

// ---- resolução de endereço de fragmento (fork overlays.cpp:4786-4870, v1) ----
namespace {
thread_local uint32_t t_input_stack[16];
thread_local int t_input_sp = 0;

// Variante carregada do bucket que cobre o offset: a menor (fork recomp_lookup_fragment_offset).
int32_t lookup_variant(uint32_t bucket, uint32_t offset) {
    int32_t best = 0; uint32_t best_size = ~0u;
    for (auto it = g_live.rbegin(); it != g_live.rend(); ++it) {
        const SectionTableEntry& s = g_sections[it->section_index];
        uint32_t b = is_synthetic_addr(s.ram_addr) ? (s.ram_addr & 0xFFF00000u) : link_base_of(s);
        if (b != bucket || offset >= s.size) continue;
        if (s.size < best_size) { best = it->base + (int32_t)offset; best_size = s.size; }
    }
    return best;
}
bool in_loaded_variant(uint32_t bucket, uint32_t addr) {
    for (const auto& l : g_live) {
        const SectionTableEntry& s = g_sections[l.section_index];
        if (link_base_of(s) == bucket && addr >= (uint32_t)l.base && addr < (uint32_t)l.base + s.size) return true;
    }
    return false;
}
} // namespace

// 0025: a pilha nunca fica negativa (antes, um pop a mais deixava t_input_sp = -1 e o push
// seguinte gravava em t_input_stack[-1]). Overflow conta (para casar com os pops), mas n�o grava.
static int s2_input_stack_warn = 0;
extern "C" void librecomp_fragment_input_push(uint32_t input) {
    if (t_input_sp >= 0 && t_input_sp < 16) t_input_stack[t_input_sp] = input;
    else if (s2_input_stack_warn++ < 8) fprintf(stderr, "[s2] fragment input stack overflow (sp=%d, input=%08X)\n", t_input_sp, input);
    t_input_sp++;
}

extern "C" uint32_t librecomp_fragment_resolve_exit(uint32_t game_result, uint32_t /*data_ctx_addr*/) {
    if (t_input_sp <= 0) {
        if (s2_input_stack_warn++ < 8) fprintf(stderr, "[s2] fragment input stack underflow (result=%08X)\n", game_result);
        t_input_sp = 0;
        return game_result;
    }
    t_input_sp--;
    if (t_input_sp >= 16) return game_result;   // entrada perdida no overflow: j� avisado no push
    const uint32_t input = t_input_stack[t_input_sp];
    if (g_sections == nullptr) return game_result;
    std::lock_guard<std::mutex> lock(g_frag_mutex);
    if (is_synthetic_addr(input)) {
        int32_t r = lookup_variant(input & 0xFFF00000u, input & 0x000FFFFFu);
        return r ? (uint32_t)r : game_result;
    }
    if ((input & 0xFFF00000u) != 0x8FF00000u) return game_result;
    if (in_loaded_variant(0x8FF00000u, game_result)) return game_result;
    // ponytail: sem recomp_resolve_via_data_context (contexto de dados do fork); v2 se o estádio errar modelo.
    int32_t r = lookup_variant(0x8FF00000u, input & 0x000FFFFFu);
    return r ? (uint32_t)r : game_result;
}

// P53m [Proton]: a synthetic address (section ram_addr + offset, e.g. 0xE8C0B938) -> live base + offset of the loaded
// fragment of that section; 0 when it is not synthetic or no live fragment has that section. get_function uses it
// before interpreting, so a miss on a synthetic address runs the native function (or interprets real RDRAM code).
extern "C" uint32_t s2_translate_synthetic(uint32_t a) {
    if (g_sections == nullptr || !is_synthetic_addr(a)) return 0;
    std::lock_guard<std::mutex> lock(g_frag_mutex);
    for (auto it = g_live.rbegin(); it != g_live.rend(); ++it) {
        const SectionTableEntry& s = g_sections[it->section_index];
        if (a >= s.ram_addr && a - s.ram_addr < s.size) return (uint32_t)it->base + (a - s.ram_addr);
    }
    return 0;
}
