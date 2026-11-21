#include <cstdint>
#include <cstdio>
#include <cassert>
static void* g_sections = nullptr;
thread_local uint32_t t_input_stack[16];
thread_local int t_input_sp = 0;
static int s2_input_stack_warn = 0;
void librecomp_fragment_input_push(uint32_t input) {
    if (t_input_sp >= 0 && t_input_sp < 16) t_input_stack[t_input_sp] = input;
    else if (s2_input_stack_warn++ < 8) fprintf(stderr, "[s2] fragment input stack overflow (sp=%d, input=%08X)\n", t_input_sp, input);
    t_input_sp++;
}

uint32_t librecomp_fragment_resolve_exit(uint32_t game_result, uint32_t /*data_ctx_addr*/) {
    if (t_input_sp <= 0) {
        if (s2_input_stack_warn++ < 8) fprintf(stderr, "[s2] fragment input stack underflow (result=%08X)\n", game_result);
        t_input_sp = 0;
        return game_result;
    }
    t_input_sp--;
    if (t_input_sp >= 16) return game_result;   // entrada perdida no overflow: já avisado no push
    const uint32_t input = t_input_stack[t_input_sp];
    if (g_sections == nullptr) return input;   // teste: devolve a entrada lida
    return game_result;
}
int main(){
    assert(librecomp_fragment_resolve_exit(0x111,0)==0x111 && t_input_sp==0);
    librecomp_fragment_input_push(0xAA); assert(t_input_sp==1);
    assert(librecomp_fragment_resolve_exit(0x111,0)==0xAA && t_input_sp==0);
    for(int k=0;k<18;k++) librecomp_fragment_input_push(0x100+k);
    assert(t_input_sp==18);
    assert(librecomp_fragment_resolve_exit(0x222,0)==0x222);
    assert(librecomp_fragment_resolve_exit(0x222,0)==0x222);
    assert(librecomp_fragment_resolve_exit(0x222,0)==0x10F);
    for(int k=0;k<15;k++) librecomp_fragment_resolve_exit(0,0);
    assert(t_input_sp==0);
    librecomp_fragment_input_push(0xBB); assert(t_input_stack[0]==0xBB);
    puts("P31 0025 host test OK"); return 0;
}
