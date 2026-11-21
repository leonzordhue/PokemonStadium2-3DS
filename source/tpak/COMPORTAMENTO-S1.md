# Pokémon Stadium (S1) para New 3DS: comportamento observado e como o Stadium 2 copia

Fonte: textos (`strings -a`) do binário `ps.3dsx` v1.2.1-test5. Nenhum código foi copiado dele, porque o binário é fechado. A exceção é o controle, confirmado no console em 19/11.

## Plataforma
- O S1 não usa o rt64-3ds (não há nenhuma string `recomp3ds`).
- Compila só `generated-selected/`, um subconjunto da recompilação feita a partir de `PokemonStadiumRecomp/platform/3ds`.
- Exige New 3DS ("FAIL: New Nintendo 3DS required.").
- A ROM US v1.0 fica em `sdmc:/3ds/PokemonStadium/Rom.z64`. O S1 confere o CRC32 dela e rejeita outra revisão ("US v1.0 required").
- A ROM fica num cache com leitura antecipada. O que ele aprende fica em `rom-scenes.bin` e `rom-prefetch.bin`.
- **Atalhos:**
  - `L+R+SELECT`: sair;
  - `L+R+START`: reiniciar;
  - `ZL`: salva uma captura do erro, só com o log técnico ligado, que grava em `/3ds/PokemonStadium/dumps/`.
- **Opções:**
  - tela 4:3, FULL ou 3D (o 3D vale só em batalha);
  - fps na tela;
  - multiplayer local (beta), com criar e entrar em sala, escolhido antes de o jogo detectar os controles;
  - idioma (EN/FR/ES, com a ROM daquela versão);
  - overlay de desenvolvedor.

## Controles: extraídos do binário (função em 0x0011A4D8 do `ps.3dsx`) e confirmados no console (19/11)
Esta seção é interoperabilidade: ela lê só o mapeamento de teclas. Nenhum código foi copiado.

A função recebe `hidKeysHeld()`, o Circle Pad (x, y) e o id da cena do jogo (r3). Ela devolve os botões do N64 e o stick.

### Esquema normal (todas as cenas exceto 38)
| 3DS | N64 |
|---|---|
| A | A |
| B | B |
| **X** | **Z** |
| **Y** | **C-down** |
| L | L |
| R | R |
| START | START |
| **D-pad ↑ ↓ ← →** | **C-up, C-down, C-left, C-right** |
| **C-Stick ↑ ↓ ← →** (New 3DS) | **D-pad ↑ ↓ ← →** do N64 |
| ZL, ZR, SELECT | nada (ZL = captura com log; SELECT = atalhos) |

**Variações por cena:**
- **Cenas 35 e 130:** o D-pad do 3DS faz **também** o D-pad do N64, além dos botões C.
- **Cenas 2, 4, 16 a 19, 32 a 42 e 130:** o Circle Pad também aperta o D-pad do N64 quando passa do limite. Na horizontal o limite é ±37 (escala N64). Na vertical é ±74 do valor bruto (5927/80).

### Cena 38 (GB Tower)
| 3DS | N64 |
|---|---|
| A | A |
| B | B |
| **Y** | **Z** |
| START | configurável, padrão **START** |
| SELECT | configurável, padrão **L** |
| D-pad | D-pad do N64 |
| C-Stick | botões C |
| L | L |
| R | R |

A tabela START/SELECT fica em RAM e o padrão vem de 0x0011A490 (START=0x1000, SELECT=0x20). É o "Configure buttons START and SELECT" do menu do GB Tower.

### Circle Pad
Sem deadzone e linear: `n64 = raw × 80 / 156`, saturando em ±80 quando |raw| > 158 (12635/80).

### `button_map` proposto para o S2 (Frente B, `~/stadium2-3ds/s2-3ds/platform/3ds/main_3ds.cpp`; o [Claude] aplica)
```cpp
// Igual ao Pokemon Stadium 3DS (lido do binário dele, 0x0011A4D8; confirmado
// no console, 19/11): D-pad = botões C, X = Z, Y = C-down,
// C-Stick = D-pad do N64.
const ButtonMap kButtons[] = {
    { KEY_A, N64_A }, { KEY_B, N64_B }, { KEY_X, N64_Z }, { KEY_Y, N64_CDOWN },
    { KEY_L, N64_L }, { KEY_R, N64_R }, { KEY_START, N64_START },
    { KEY_DUP, N64_CUP }, { KEY_DDOWN, N64_CDOWN },
    { KEY_DLEFT, N64_CLEFT }, { KEY_DRIGHT, N64_CRIGHT },
    { KEY_CSTICK_UP, N64_DUP }, { KEY_CSTICK_DOWN, N64_DDOWN },
    { KEY_CSTICK_LEFT, N64_DLEFT }, { KEY_CSTICK_RIGHT, N64_DRIGHT },
};
// e no start: recomp3ds::input_set_cstick_buttons(false);  // o C-Stick deixa de ser botões C
```
**Atenção ao aplicar:**
- **C-Stick no rt64-3ds:** o C-Stick fica fixo como botões C (`input_hid.cpp`, `g_cstick_buttons`). A lista de teclas remapeáveis (linha ~122) não tem `KEY_CSTICK_*`. Para o C-Stick fazer o D-pad do N64 é preciso desligar `input_set_cstick_buttons(false)` e conferir se `input_map_buttons` lê os bits `KEY_CSTICK_*` do `hidKeysHeld`. Se não ler, acrescentar esses 4 bits à lista (um patch de 1 linha no rt64-3ds).
- **Variações por cena e GB Tower:** pedem um gancho por cena, que o `button_map` estático não cobre. Opções: `input_set_map()` do rt64-3ds trocado quando a cena muda, lendo o id da cena na RDRAM do S2 (endereço a descobrir no decomp do S2). Ou, mais simples no começo, deixar só o esquema normal.
- **IDs de cena:** os números são do **S1**. No S2 o equivalente tem que ser achado no decomp `pokestadiumgs`.

## Transfer Pak no S1
- **Configuração:** `sdmc:/3ds/PokemonStadium/transferpak.cfg`, com as chaves `writeback=`, `language=`, `<p>connected=`, `<p>game=`, `<p>rom=` e `<p>save=vc <titleid> <arquivo>` ou `<p>save=sd <arquivo>`.
- **Busca:** procura ROMs de GB em `/3ds/PokemonStadium/gb`, `sdmc:/roms/gb`, `sdmc:/roms/gbc` e `sdmc:/gb`. Também procura saves do **Virtual Console** instalados, lendo e escrevendo o save do VC direto pelo AM/FS do 3DS.
- **Arquivos:**
  - `gb/saves/<nome>.sav`;
  - `gb/saves/<nome>-VC-<crc>.sav` e o `.pending` dele;
  - `gb/backup/<nome>-<crc>-original.sav` e `...-last.sav`.
- **Validações:**
  - confere o checksum do save Gen1 e o cabeçalho de tileset da release;
  - recusa o mesmo jogador nos dois Transfer Paks;
  - grava numa thread própria;
  - não grava RAM com checksum inválido ("not written, last good copy kept").
- **Mensagens:**
  - Setting up Transfer Pak 1/2;
  - Connected to Game Pak 1/2;
  - Tap again to disconnect;
  - Search saves / Stop search;
  - Start game;
  - Ready to connect;
  - No cartridge;
  - Cartridge connected / Cartridge removed.;
  - Save written to SD. / Save written to VC.;
  - Save pending. Resume before removing.;
  - Previous write interrupted. Backups kept.;
  - VC save changed; using it.;
  - Already in the other Transfer Pak.;
  - Invalid Game Boy ROM. / Unsupported cartridge type.;
  - Use a ROM of the same game version and language as your VC save.

## O que o Stadium 2 já tem (`s2-3ds/tpak/`)
- **`tpak_3ds.h` e `tpak_3ds.cpp`:**
  - **Modelo do cartucho:** MBC1/3/5, registradores do acessório e open bus. Vem de mstan/N64ModernRuntime `gbcart.cpp` (f61d5424, GPL-3.0).
  - **RTC do MBC3:** para Gold/Silver/Crystal, guardado num rodapé de 48 bytes no formato VBA-M/mGBA.
  - **Gravação segura:** grava só com o checksum do jogo válido (Gen1, G/S e Crystal), de forma atômica via `.pending`, e recupera esse `.pending` no próximo início.
  - **Backups:** `-<crc32 da ROM>-original` uma vez e `-last` a cada sessão.
  - **Configuração:** `transferpak.cfg` com `p1_rom`/`p1_save`. Recusa o mesmo save nos dois paks e usa as mensagens do S1.
  - **Lock:** `LightLock` no 3DS (o devkitARM não tem `std::mutex`) e `std::mutex` no host.
- **`tpak_hle_3ds.cpp`:** `__osContRamRead_recomp`, `__osContRamWrite_recomp`, `__osPfsGetStatus_recomp` e `osPfsIsPlug_recomp`.
  - **Substitui as declarações "sem pak" da Frente A (`s2-3ds/compat`).** No link, entra este arquivo e saem os stubs.
  - A latência do SI foi simplificada para duas mensagens imediatas, porque o upstream não tem `send_external_message_after`.
- **`test_tpak.cpp`:** testes do protocolo do acessório, do latch de status (0x08 ligado, 0x04 desligado), dos checksums, do CRC32, do RTC (escrita e latch), da recusa por checksum ruim, do rodapé do RTC, da promoção e do descarte do `.pending`, dos backups, do banco do MBC1 e da remoção. **Passa** no host (g++).
- **Compilação:** `tpak_3ds.cpp` compila no devkitARM (armv6k, -O2), dando 26 KB de `.text`. `tpak_hle_3ds.cpp` passa no `-fsyntax-only` contra os headers do runtime upstream.

## Falta
- **VC direto pelo AM/FS:** como o S1 faz. Por enquanto, o save do VC precisa ser copiado para o SD.
- **Busca automática de ROMs e saves:** nas pastas que o S1 varre.
- **Tela de baixo do Transfer Pak:** "Transfer Pak 1/2", "Search saves" e "Tap again to disconnect", no menu do rt64-3ds (`settings_menu.cpp`).
- **Chamadas no `main_3ds.cpp` (Frente B):**
  - no início: `tpak::set_base_dir("sdmc:/3ds/PokemonStadium2"); tpak::load_config();`;
  - `tpak::flush_all()` a cada ~2 s e ao sair.

## Revisão D7 do [Nexus] + save do VC (Frente C2, 19/11/2026)
Aplicado, com cada achado conferido no código:
- A1: a recusa "Already in the other Transfer Pak." não desmonta mais o cartucho da porta.
- A2: falha de gravação no SD mantém o cartucho conectado (set_cart recusa a troca).
- A3: o flush periódico (`flush_all(false)`) só grava depois de 1 s sem escrita do jogo; saída e troca usam `flush_all(true)`.
- A4: cópia sob o lock, gravação fora dele (o jogo não espera o SD). Lock próprio `g_io` para SD/VC; ordem g_mutex antes de g_io.
- A5 (main_3ds.cpp): thread de flush com join na saída, rechecagem da flag e pilha de 16 KB.
- A6: rodapé de save que não é VBA-M (44/48) é preservado igual (caso do VC).
- A7: rodapé de 44 bytes usa o carimbo gravado. A8: mensagem quando o checksum falha. A9: caminhos relativos do cfg resolvidos pela pasta base. A10: leitura em bloco e CRC32 por tabela. A11: `si_dma_pair` só recebe o que enviou. A15: MBC5 com 16 bancos de RAM.
- Não aplicado: A12 (PFS_ERR_NEW_PACK, só com troca a quente pelo menu); A14 parcial (Red continua MBC1 no teste); checksums de caixa do Gen1 (offsets não conferidos).

Save do VC (`vc_save_3ds.{h,cpp}`, do [Proton] P1):
- `pN_save=vc` (ou `vc <title id hex>`) usa o save do VC instalado. **Sem `pN_save`, tenta o VC daquele jogo e cai em `gb/saves/<rom>.sav`.**
- Lê `/sav.dat` inteiro; se o checksum falhar, usa a cópia do SD e não grava no VC ("Invalid VC save. Backups kept.").
- Gravação: SD primeiro (`<rom>-VC-<crc>.sav`), depois VC (só os 0x8000 da SRAM, commit do archive). `.pending` na montagem = termina a escrita cortada.
- Limite: duas portas no mesmo título VC não são detectadas. Versão/idioma da ROM contra o VC não é conferido (o S1 lê a ROM do RomFS do VC).
- Permissões: ver `NOTAS-RSF.md`.
