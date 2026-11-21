"""Gera compat/include/recomp.h a partir do header do fork (michiiik/mstan N64Recomp 2b949c5c):
+ campo fragment_runtime_base (usado pelos hooks do game.toml) e RECOMP_MEM_MASK configurável (3DS: 16 MB)."""
import os, sys
src = os.path.expanduser('~/stadium2-3ds/n64recomp-fork/include/recomp.h')
dst = os.path.expanduser('~/stadium2-3ds/s2-3ds/compat/include/recomp.h')
s = open(src).read()

old_field = '    uint32_t dispatch_entry_rejected_target;\n'
assert s.count(old_field) == 1, 'campo dispatch_entry_rejected_target'
s = s.replace(old_field, old_field +
    '    // s2-3ds: base em RDRAM do fragmento sendo relocado (hooks do game.toml\n'
    '    // em Memmap_RelocateFragment / func_800024A0). Sempre o ÚLTIMO campo.\n'
    '    uint32_t fragment_runtime_base;\n')

old_mask = '#define RECOMP_MEM_MASK 0x3FFFFFFFu\n'
assert s.count(old_mask) == 1, 'RECOMP_MEM_MASK'
s = s.replace(old_mask,
    '// s2-3ds: configurável. 3DS = RDRAM de 16 MB (nmr patch 0003): -DRECOMP_MEM_MASK=0x00FFFFFFu\n'
    '#ifndef RECOMP_MEM_MASK\n#define RECOMP_MEM_MASK 0x3FFFFFFFu\n#endif\n')

old_cf = 'void recomp_cf_note(const char* kind, uint32_t value0, uint32_t value1, uint32_t value2, recomp_context* ctx);\n'
assert s.count(old_cf) == 1, 'recomp_cf_note'
s = s.replace(old_cf,
    '// s2-3ds: recomp_cf_note é só diagnóstico (181 mil chamadas no Stadium 2). Com\n'
    '// RECOMP_CF_NOTE_DISABLED vira inline vazio e some do binário junto com as strings.\n'
    '#ifdef RECOMP_CF_NOTE_DISABLED\n'
    'static inline void recomp_cf_note(const char* kind, uint32_t value0, uint32_t value1, uint32_t value2, recomp_context* ctx) {\n'
    '    (void)kind; (void)value0; (void)value1; (void)value2; (void)ctx;\n'
    '}\n#else\n' + old_cf + '#endif\n')

old_lc = '#ifndef RECOMP_LOOP_CHECKPOINT\n'
assert s.count(old_lc) == 1, 'RECOMP_LOOP_CHECKPOINT'
s = s.replace(old_lc,
    '// s2-3ds: no 3DS o ponto de checagem lê a cópia da flag de preempção (nmr patch 0039)\n'
    '// e só chama o runtime quando há evento pendente: um load + desvio no caminho rápido.\n'
    '#ifdef RECOMP_3DS_PREEMPT_CHECK\n'
    'extern uint32_t* ultramodern_preempt_flag;\n'
    'void ultramodern_preempt(uint8_t* rdram);\n'
    '#define RECOMP_LOOP_CHECKPOINT() \\\n'
    '    if (__builtin_expect(*(volatile uint32_t*)ultramodern_preempt_flag, 0)) ultramodern_preempt(rdram);\n'
    '#define RECOMP_LOOP_CHECKPOINT_VRAM(pc) RECOMP_LOOP_CHECKPOINT()\n'
    '#endif\n' + old_lc)

assert '3DS/ARM32' in s, 'DMULT portátil ausente: rode patch_int128.py antes'
os.makedirs(os.path.dirname(dst), exist_ok=True)
open(dst, 'w').write(s)
print('ok', dst)
