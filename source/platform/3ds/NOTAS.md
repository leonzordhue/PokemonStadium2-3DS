# platform/3ds — Pokémon Stadium 2 no New 3DS (Frente B, 19/11/2026)

Modelo: `oneup03/Goemon64Recomp3D` e `BanjoRecomp3D` (`platform/3ds`), guia `lib/rt64-3ds/docs/porting-a-game.md`.
Referências clonadas (sparse, só `platform/`): `~/stadium2-3ds/ref/Donkey-Kong-64-3D`, `~/stadium2-3ds/ref/Goemon64Recomp3D`.

## Arquivos
- `CMakeLists.txt`: rt64-3ds + runtime `RECOMP_NO_MODS` (o Stadium 2 não tem patches.elf nem mods). Compila `generated/funcs_*.c` em `-Os` (meta ≤ 60 MB), o RSP (aspMain_ps2, njpgdspMain, gbTower, gbTowerColor) em `-O3`, `src/main/register_overlays.cpp` do projeto desktop, e tudo de `../../compat/*.c|cpp` (Frente A). `add_3dsx` + `add_cia` (PRODUCT `PKS2-3DS`, UNIQUEID `0xF5D20`, sem banner próprio ainda).
  - Variáveis de cache: `S2_GAME_DIR` (pokemonStadiumGSRecomp), `RT64_3DS_DIR`, `RT64_3DS_NMR_DIR` (padrão `s2-3ds/lib/N64ModernRuntime`), `S2_RECOMP_INCLUDE` (recomp.h do código gerado; padrão o do fork).
- `main_3ds.cpp`: `GameDesc` com game id `stadium2`, sd_dir `PokemonStadium2` (ROM `sdmc:/3ds/PokemonStadium2/stadium2.z64`), hash `0x13DE5F210AA8D502`, Flashram, F3DEX2, VI 320x240, ROM em memória no New 3DS (stream no Old), estéreo sem regras (persp = World, ortho = Hud), `render_to_texture` (fotos/GB Tower). RSP: mesmo despacho do desktop; o GB Tower é reconhecido só pelo tamanho do boot (0xA40/0x9E4), porque o callback do 3DS não recebe rdram. Áudio: o aspMain recompilado do jogo (`audio_hle=false`); testar `AudioHle::Aspmain` depois que bootar.
  - Botões: layout padrão oneup03 (placeholder). Frente C troca pelo do Stadium 1.
- `recomp_api_3ds.cpp`: stubs fracos para o que falta. Medido: o código gerado chama 76 símbolos externos; 8 não existem no runtime upstream:
  - `recomp_handle_tailcalls` → compat (Frente A)
  - `__osContRamRead/Write`, `__osPfsGetStatus`, `osPfsIsPlug` → Transfer Pak (Frente C). Por enquanto respondem "sem pak".
  - `func_80064980`, `static_5_8000B770`, `static_272_8334B334` → sem corpo no código gerado; stub que avisa e encerra.
- `stadium2_host_protos.h`: força `RECOMP_FUNC` = `noipa` + `rounding-math` no GCC.
- `icon.png`: placeholder 48x48 (listras azul/dourado).

## Runtime usado no configure
`s2-3ds/lib/N64ModernRuntime`: clone do upstream no commit que o Banjo3D fixa (`cdf5abb`), com os 7 + 41 nmr-patches aplicados limpos (`nmr-patches/apply.sh`). O checkout do Banjo3D não foi alterado.

## Estado
- `cmake` configure: **OK** (`build-3ds/`, log `build-3ds-configure.log`).
- `main_3ds.cpp` e `recomp_api_3ds.cpp` compilam sem erro com o devkitARM.
- Os 246 arquivos gerados não foram compilados (combinado: 1 processo pesado por vez).

## Bloqueios encontrados
1. **devkitARM r59 (gcc 12.2, de 2022) não tem `std::mutex`** (`_GLIBCXX_HAS_GTHREADS` desligado). O `ultramodern` do runtime não compila (`audio.cpp`, `events.cpp`: "'mutex' in namespace 'std' does not name a type"). Os ports do oneup03 usam um devkitARM novo, com threads da libctru. **Precisa atualizar o devkitARM** (o pacman do devkitPro deu 403 aqui; alternativa: extrair `/opt/devkitpro` da imagem docker `devkitpro/devkitarm:latest`, o docker já roda no WSL).
2. **ABI do fork x upstream** (Frente A): `generated/recomp_overlays.inl` usa `SectionTableEntry::content_hash`, que só existe no fork. O `register_overlays.cpp` do desktop também usa `recomp_request_tailcall` e `ctx->host_return_target`. O código gerado é compilado com o `recomp.h` do fork, mas o librecomp/ultramodern são do upstream patched: os dois lados precisam concordar no `recomp_context` e no `SectionTableEntry`. Isso a camada `compat/` tem que resolver (ou regerar o código com o N64Recomp upstream patched).

## Depende da Frente A (`s2-3ds/compat/`)
- `recomp_handle_tailcalls`, `recomp_request_tailcall*`, `recomp_register_runtime_fragment`, `recomp_fragment_*`, campos `fragment_runtime_base`/`host_return_target` do contexto.
- `SectionTableEntry.content_hash` em `recomp_overlays.inl`.
- Decidir `S2_RECOMP_INCLUDE` e se `RT64_3DS_NMR_DIR` fica no upstream patched (`s2-3ds/lib/N64ModernRuntime`).

## Como reconfigurar
```
cd ~/stadium2-3ds/s2-3ds
R=~/stadium2-3ds/BanjoRecomp3D/lib/rt64-3ds
DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM \
cmake -S platform/3ds -B build-3ds -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$R/cmake/devkitARM-3ds.cmake
```

## B2 · 19/11/2026 15:28 (Manaus) · controles do S1 + Transfer Pak ligados
- **Controles = S1** (main_3ds.cpp kButtons): A=A, B=B, X=Z, Y=C-baixo, L=L, R=R, START=START, D-pad=botões C, C-Stick=D-pad do N64.
  Sem patch no rt64-3ds: no New 3DS, hidKeysHeld já traz KEY_CSTICK_*. O mapa usa esses bits e s2_on_init chama input_set_cstick_buttons(false).
  Circle Pad: stick_deadzone 0 (o S1 não tem deadzone). A escala do runtime dá ~82 no curso todo (o S1 dá ×80/156).
- **Transfer Pak**:
  - CMake compila tpak/tpak_3ds.cpp e tpak/tpak_hle_3ds.cpp.
  - s2_on_init: set_base_dir("sdmc:/3ds/PokemonStadium2") + load_config() (transferpak.cfg).
  - Uma thread grava os saves (flush_all) a cada 2 s, e grava de novo na saída do run().
  - Os 4 stubs "sem pak" saíram de recomp_api_3ds.cpp.
- **Microcode**: o despacho continua por boot size/tipo. Armadilha documentada: timed_get_ucode (recomp3ds_run.cpp:116) trata o 1º microcode como áudio. Com audio_hle=false isso só muda qual tarefa é cronometrada; antes de ligar audio_hle, trocar a checagem para M_AUDTASK.
- **Compilado** com devkitARM gcc 16.1 (docker devkitpro/devkitarm): os 4 .o em s2-3ds/build-b2/, sem erro. nm confere: main→tpak::*, tpak_hle→tpak::read/write/has_pak, os 4 *_recomp definidos.
  Includes extras necessários: N64ModernRuntime/thirdparty/{concurrentqueue,xxHash}. O CMake já pega thirdparty via librecomp/ultramodern.
- **Falta**:
  - Mapa do GB Tower (Y=Z, D-pad=D-pad, C-Stick=C): input_set_map quando o P2 do Proton der o endereço da tela.
  - Atalhos L+R+SELECT (sair) e L+R+START (reset): no rt64-3ds o SELECT abre o menu, decidir depois.
  - ZL = captura: no rt64-3ds a captura é SELECT segurado.
