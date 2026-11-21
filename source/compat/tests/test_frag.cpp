// Teste de host da camada de fragmentos do s2_compat contra o overlays.cpp REAL do upstream
// (work/up-nmr, com os nmr-patches + 0001 + 0002). Revisão D6 do [Nexus]: o teste antigo usava um
// unload_overlays falso e por isso não pegou o abort "Cannot partially unload section".
// Cobre: seção de tamanho real (0x400), duas seções vizinhas coladas (base e base+0x400),
// troca de variante no mesmo endereço, alias no endereço de link, unregister por id.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "recomp.h"
#include "librecomp/sections.h"
#include "librecomp/overlays.hpp"

extern int32_t* section_addresses;
extern "C" recomp_func_t* get_function(int32_t addr);

// O s2_compat também chama isto do ultramodern (áudio/preempção); aqui basta o mínimo.
namespace ultramodern {
    void queue_audio_buffer(uint8_t*, int32_t, uint32_t) {}
    uint32_t get_remaining_audio_bytes() { return 0; }
    extern "C" uint32_t* ultramodern_preempt_flag;
}
uint32_t pf = 0;
extern "C" uint32_t* ultramodern_preempt_flag = &pf;
extern "C" void ultramodern_preempt(uint8_t*) {}
namespace ultramodern::error_handling { void message_box(const char* m) { fputs(m, stderr); } }

static void fa(uint8_t*, recomp_context*) {}
static void fb(uint8_t*, recomp_context*) {}
static void fc(uint8_t*, recomp_context*) {}
static void fa2(uint8_t*, recomp_context*) {}
static void fb2(uint8_t*, recomp_context*) {}
// Variante B tem uma função a mais (offset 0x100) que a A não tem: o alias antigo tem que sumir.
static FuncEntry funcs_a[] = {{fa, 0x20, 0x40}};
static FuncEntry funcs_b[] = {{fb, 0x20, 0x40}, {fb2, 0x100, 0x40}};
static FuncEntry funcs_c[] = {{fc, 0x20, 0x40}, {fa2, 0x3F0, 0x10}};
static SectionTableEntry secs[3];

extern "C" void s2_compat_set_sections(SectionTableEntry*, size_t);
extern "C" void recomp_register_runtime_fragment(uint8_t*, uint32_t, int32_t);
extern "C" void recomp_unregister_runtime_fragment(uint32_t);
extern "C" void librecomp_fragment_input_push(uint32_t);
extern "C" uint32_t librecomp_fragment_resolve_exit(uint32_t, uint32_t);

static uint64_t fnv(const uint8_t* p) { uint64_t h = 0xCBF29CE484222325ull; for (int i = 0; i < 0x100; i++) { h ^= p[i]; h *= 0x100000001B3ull; } return h; }
static void put(uint8_t* rdram, uint32_t paddr, const uint8_t* img) { for (int i = 0; i < 0x100; i++) rdram[(paddr + i) ^ 3] = img[i]; }

int main() {
    static uint8_t rdram[8 << 20];
    uint8_t img_a[0x100], img_b[0x100], img_c[0x100];
    for (int i = 0; i < 0x100; i++) { img_a[i] = (uint8_t)i; img_b[i] = (uint8_t)(255 - i); img_c[i] = (uint8_t)(i * 7); }
    // rom ascendente: init_overlays ordena por rom_addr, os índices não mudam.
    secs[0] = {0x1000, 0x81300000, 0x400, funcs_a, 1, nullptr, 0, 0, fnv(img_a), 0};
    secs[1] = {0x2000, 0x81300000, 0x400, funcs_b, 2, nullptr, 0, 1, fnv(img_b), 0};
    secs[2] = {0x3000, 0x81400000, 0x400, funcs_c, 2, nullptr, 0, 2, fnv(img_c), 0};
    static int ovl_table[1] = {0};
    recomp::overlays::register_overlays({secs, 3, 3}, {ovl_table, 0});
    recomp::overlays::init_overlays();
    s2_compat_set_sections(secs, 3);

    const uint32_t base = 0x80200000, paddr = base & 0x1FFFFFFF;
    const uint32_t nb = base + 0x400, npaddr = nb & 0x1FFFFFFF;   // vizinho colado

    put(rdram, paddr, img_b);  recomp_register_runtime_fragment(rdram, 3, (int32_t)base);
    put(rdram, npaddr, img_c); recomp_register_runtime_fragment(rdram, 4, (int32_t)nb);
    assert(get_function((int32_t)(base + 0x20)) == fb);
    assert(get_function((int32_t)(base + 0x100)) == fb2);
    assert(get_function((int32_t)(0x81300000u + 0x100)) == fb2);         // alias de link da B
    assert(get_function((int32_t)(nb + 0x20)) == fc);
    assert(section_addresses[1] == (int32_t)base && section_addresses[2] == (int32_t)nb);

    // Troca B -> A no mesmo endereço, com o vizinho C colado: o upstream abortava aqui.
    put(rdram, paddr, img_a);  recomp_register_runtime_fragment(rdram, 3, (int32_t)base);
    assert(get_function((int32_t)(base + 0x20)) == fa);
    assert(get_function((int32_t)(0x81300000u + 0x20)) == fa);           // alias novo
    assert(get_function((int32_t)(nb + 0x20)) == fc);                    // vizinho intacto
    assert(get_function((int32_t)(nb + 0x3F0)) == fa2);
    assert(section_addresses[1] == (int32_t)0x81300000 && section_addresses[0] == (int32_t)base);
    // Alias antigo da B (offset 0x100, que a A não tem) apagado: re-adicionar e remover só com o
    // ponteiro certo prova que a entrada não existia mais apontando para fb2.
    recomp::overlays::add_loaded_function((int32_t)(0x81300000u + 0x100), fa2);
    recomp::overlays::remove_loaded_function((int32_t)(0x81300000u + 0x100), fb2);   // não remove (não é fb2)
    assert(get_function((int32_t)(0x81300000u + 0x100)) == fa2);
    recomp::overlays::remove_loaded_function((int32_t)(0x81300000u + 0x100), fa2);

    // Unregister do vizinho por id.
    recomp_unregister_runtime_fragment(4);
    assert(section_addresses[2] == (int32_t)0x81400000);
    assert(get_function((int32_t)(base + 0x20)) == fa);                  // o outro continua

    librecomp_fragment_input_push(0x81300020u);                          // fora do 0x8FF: devolve o do jogo
    assert(librecomp_fragment_resolve_exit(0x80200020u, 0) == 0x80200020u);
    puts("test_frag ok (overlays.cpp real, vizinhos colados)");
}
