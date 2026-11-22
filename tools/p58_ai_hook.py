"""[Nexus] P58: som do jogo de GB na Tower. O frag 0x5 traz a PRÓPRIA cópia do osAiSetNextBuffer da libultra em
0x8150DFA0 (lê AI_STATUS em 0xA450000C, converte o endereço e grava AI_DRAM_ADDR/AI_LEN direto em 0xA4500000/4,
com o contorno do bug de 8 KB em 0x815125B0). No código nativo esses stores caem na RDRAM (MEM_W com máscara) e o
áudio nunca chega ao 3DS. Este script, rodado DEPOIS do gerar2 sobre o generated/, faz func_8150DFA0 chamar o
osAiSetNextBuffer_recomp do host (librecomp/src/ai.cpp → ultramodern::queue_audio_buffer(rdram, a0, a1)) e voltar.
Mesma assinatura da libultra (a0 = buffer, a1 = tamanho); o resto do corpo fica como código morto. Idempotente;
--remover tira. Uso (WSL): python3 p58_ai_hook.py ~/stadium2-3ds/pokemonStadiumGSRecomp/generated"""
import glob, os, re, sys
G = sys.argv[1]
remover = "--remover" in sys.argv
FN, MARK = "func_8150DFA0", "/* P58 ai */"
NL = chr(10)
PROTO = ("#ifdef __cplusplus" + NL + 'extern "C"' + NL + "#endif" + NL +
         "void osAiSetNextBuffer_recomp(uint8_t* rdram, recomp_context* ctx); " + MARK + NL +
         ("#include <stdio.h>" + NL + "#include <time.h>" + NL + "static unsigned ultramodern_p58_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u); } " + MARK + NL if "--log" in sys.argv else ""))
ok = False
for f in sorted(glob.glob(os.path.join(G, "funcs_*.c"))):
    s = open(f, encoding="utf-8", errors="replace").read()
    i = s.find("RECOMP_FUNC void %s(" % FN)
    if i < 0: continue
    orig = s
    if remover:
        s = s.replace(PROTO, "")
        s = re.sub(NL + r"[^" + NL + r"]*" + re.escape(MARK) + r"[^" + NL + r"]*", "", s)
    elif MARK not in s:
        entry = "RECOMP_FUNC_ENTRY(%s)" % FN
        j = s.find(entry, i)
        assert 0 <= j - i < 400, "RECOMP_FUNC_ENTRY de %s não achado" % FN
        k = s.find(NL, j)
        call = "osAiSetNextBuffer_recomp(rdram, ctx); return;"
        if "--log" in sys.argv:   # tamanho de cada buffer e buffers/s (1 linha a cada 60 buffers)
            fmt = "[p58] buf #%u len=%u bytes (%u amostras) ms=%u" + chr(92) + "n"
            call = ("{ static unsigned n_p58 = 0; ++n_p58; if (n_p58 % 60 == 1) fprintf(stderr, \"" + fmt + "\", n_p58, "
                    "(unsigned)(uint32_t)ctx->r5, (unsigned)(uint32_t)ctx->r5 / 4u, (unsigned)ultramodern_p58_ms()); } " + call)
        s = s[:k] + NL + "    " + call + " " + MARK + s[k:]
        s = s[:i] + PROTO + s[i:]
    ok = True
    if s != orig:
        open(f, "w", encoding="utf-8").write(s)
        print("alterado:", os.path.basename(f))
    else:
        print("já estava:", os.path.basename(f))
if not ok:
    print("ERRO: %s não está no generated/" % FN); sys.exit(1)
