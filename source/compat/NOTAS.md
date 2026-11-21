# compat/ — Frente A: código do Stadium 2 (fork N64Recomp) sobre o runtime 3DS do oneup03

Estratégia (a): o runtime é N64ModernRuntime upstream + os 41 nmr-patches do rt64-3ds (aplicam limpo),
e esta pasta cobre o que o código gerado pelo fork michiiik/mstan espera e o upstream não tem.
A estratégia (b), portar os patches para o fork, foi descartada: 32 de 41 patches falham, ~160 hunks.

## Arquivos
- `include/recomp.h`: header do fork (2b949c5c) + DMULT sem __int128 + `fragment_runtime_base` no fim do
  contexto + flags abaixo. Gerado por `tools/patch_int128.py` + `tools/mk_header.py` sobre o header do fork.
- `patches/0001-sections-content-hash.patch`: `content_hash` + `original_pattern_id` em `SectionTableEntry`.
  Aplicar no N64ModernRuntime **depois** dos nmr-patches (`git apply`).
- `patches/0002-overlays-remove-loaded-section.patch`: `recomp::overlays::remove_loaded_section(idx, ram)` e
  `remove_loaded_function(ram, func)` (só apaga se a entrada ainda aponta para `func`). Aplicar depois do 0001.
  Já aplicado em `lib/N64ModernRuntime` e em `work/up-nmr` (19/11).
- `s2_compat.cpp`: os 17 símbolos que faltavam (tailcalls, scheduler tick, unhandled, AI, pak, 3 funções
  não recompiladas, fragmentos v1, input_push/resolve_exit).
- `tests/host_test.sh`: compila funcs_8/28/0 + dispatch_aliases + compat no host e confere os símbolos.
- `tests/test_frag.cpp`: linka o `overlays.cpp` **real** (work/up-nmr). Seção de 0x400, duas vizinhas coladas,
  troca de variante no mesmo endereço, alias de link novo e antigo, unregister por id. Rodar:
  `g++ -std=c++20 -O1 $INC $DEF tests/test_frag.cpp s2_compat.cpp ../work/up-nmr/librecomp/src/overlays.cpp`
  (INC/DEF como em host_test.sh + `-I../work/up-n64recomp/include`). Com o evict antigo ele aborta
  em "Cannot partially unload section" (conferido).

## Integração (Frente E)
1. `compat/include` **antes** de `N64Recomp/include` no include path do runtime E do código gerado.
   O runtime aloca `recomp_context` (struct maior no fork): compilar os dois com o mesmo header.
2. Definições: `-DRECOMP_MEM_MASK=0x00FFFFFFu -DRECOMP_3DS_PREEMPT_CHECK -DRECOMP_CF_NOTE_DISABLED`.
   - `RECOMP_3DS_PREEMPT_CHECK`: os 2.584 pontos de checagem de laço viram load da flag (nmr 0039) +
     `ultramodern_preempt` só com evento pendente.
   - `RECOMP_CF_NOTE_DISABLED`: as 181 mil chamadas de diagnóstico somem (inline vazio).
3. No main do jogo, depois de `recomp::overlays::register_overlays(...)`:
   `s2_compat_set_sections(section_table, num_code_sections);` (mesma tabela, já ordenada por
   `init_overlays` em `recomp::start`, porque a ordenação é feita no mesmo array).
4. Transfer Pak: os stubs `__osContRamRead/Write`, `__osPfsGetStatus` e `osPfsIsPlug` são **weak** ("sem pak").
   A Frente C define os fortes e o linker os escolhe sem mudar nada aqui.

## Resultado do teste de host (19/11/2026)
- 4 arquivos gerados compilam contra o header (dispatch_aliases.c: 17 MB, 32 s, 1,3 GB de RAM a -O0).
- Externos fora dos funcs_*.c: 39. Todos definidos no runtime upstream+nmr ou na libc
  (sobram `__dso_handle`, `__fprintf_chk`, `memmove`, que são libc/crt).
- `recomp_cf_note` e `ultramodern_scheduler_tick_vram` não aparecem mais no código gerado (macros).
- `test_frag ok` com e sem `RECOMP_3DS_PREEMPT_CHECK`.

## Revisão D6 do [Nexus] aplicada (19/11/2026, Frente A2)
- **1 e 2 (crítico, confirmado):** `evict_at` chamava `unload_overlays(base, 1)`; o upstream aborta com
  "Cannot partially unload section" para qualquer seção > 1 byte, e também para vizinhas coladas (teste `>=`).
  Agora `evict_one` usa `remove_loaded_section` (patch 0002), por base exata. O teste antigo não pegou porque
  tinha um `unload_overlays` falso.
- **3:** aliases no endereço de link são apagados no despejo, só se ainda apontam para a variante despejada
  (não derruba alias de outra variante viva do mesmo bucket).
- **6:** fallback do `J` faz a busca exata (a do fork) antes da busca por intervalo.
- **4 (AI_LEN em 0xA4500004), NÃO aplicado, precisa de decisão do coordenador.** O cálculo do Nexus está certo:
  com `RECOMP_MEM_MASK=0x00FFFFFF`, 0xA4500004 cai em RDRAM 0x500004. Mas tirar a linha do compat não resolve:
  o próprio jogo, em `func_800195C0` (funcs_28.c ~7000), lê AI_STATUS (`lw 0xC($A4500000)`) e grava
  AI_DRAM/AI_LEN (`sw` em 0xA4500000/4) pelo mesmo `MEM_W`. Todos os registradores de hardware 0xA4xxxxxx
  (VI 0x400000, AI 0x500000, PI 0x600000...) caem nos 4–8 MB da RDRAM, e o runtime diz `osMemSize` = 8 MB
  (recomp.cpp:601). Riscos: (a) corromper dado do jogo nessas faixas; (b) AI_STATUS lido de lixo com bit 31
  ligado → a função devolve -1 e o buffer de áudio não é enviado. A escrita do compat ao menos mantém o AI_LEN
  coerente para quem o lê pela RDRAM. Opções: (i) trocar a máscara por uma que leve o bit 26 (0x04000000) ao
  bit 23 (`off = (t & 0xFFFFFF) | ((t >> 3) & 0x800000)`), mandando o I/O para os 8 MB de cima dos 16 MB
  alocados — custa ~2 instruções ARM por acesso à memória; (ii) informar `osMemSize` = 4 MB, se o S2 não
  usar o Expansion Pak (conferir); (iii) hooks no game.toml nos acessos de I/O conhecidos. Recomendo testar
  (ii) primeiro: é uma linha e não custa desempenho.
- **5, 7, 8:** sem mudança (5 e 8 OK; 7 = testar Mirror Match no roteiro de batalha).

## Limites conhecidos (v1) → quando atacar
- `resolve_exit` sem `recomp_resolve_via_data_context`. Atacar se o estádio carregar modelo/script errado.
- O pool sintético (0xC0000000–0xE0000000) resolve só pela variante carregada. O fork tem mais heurística
  em `recomp_resolve_synthetic_fragment`.
- Os jump slots (offset ≥ 0x20, rom_size 8) entram como funções normais da seção via `load_overlay`. Não há
  scan de trampolins (fork 868–1025). Atacar se `get_function` abortar em endereço dentro de fragmento.
- A conclusão da task JPEG do RSP (fotos) ainda não foi conferida contra nmr 0024/0026. Isso é com o Nexus (D1).
- `dispatch_aliases.c` a -O0 pede 1,3 GB: no build 3DS, compilar sozinho (um processo pesado por vez).
