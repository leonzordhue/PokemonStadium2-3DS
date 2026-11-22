# Compilando o Pokémon Stadium 2 para New 3DS

> Não é preciso compilar para jogar: baixe o `.cia` ou o `.3dsx` nos [releases](../../releases).
> Compilar é um processo avançado (Linux/WSL, ~30 min de geração de código) e **exige a sua ROM**.
> Nenhum código gerado a partir da ROM fica neste repositório.

## O que tem aqui

| Pasta | Conteúdo |
|---|---|
| `source/platform/3ds/` | Ponto de entrada do 3DS (`main_3ds.cpp`), tela do Game Pak, CMake do port, ícone/banner |
| `source/compat/` | Camada entre o código gerado e o runtime (fragmentos/overlays, tailcalls, interpretador MIPS de reserva) e os patches numerados em `compat/patches/` |
| `source/tpak/` | Transfer Pak: cartuchos de Game Boy, saves `.sav` e saves do Virtual Console do 3DS |
| `tools/N64Recomp-s2/` | Fork do N64Recomp usado para gerar o código (seções relocáveis, tabelas de salto, GB Tower) |
| `patches/` | Diferenças aplicadas em cima do `N64ModernRuntime`, do `rt64-3ds` e do `pokemonStadiumGSRecomp` |

## Dependências

* Linux ou WSL2 (Ubuntu 22.04+), `cmake`, `ninja`, `git`, `python3`, `clang`/`g++`.
* **devkitARM recente** (gcc 14+, com `std::mutex`). O jeito mais fácil é a imagem Docker `devkitpro/devkitarm:latest` (copie `/opt/devkitpro` para a sua máquina).
* [pokemonStadiumGSRecomp](https://github.com/michiiik/pokemonStadiumGSRecomp) (clone recursivo): configuração do jogo (`game.toml`, símbolos, `register_overlays.cpp`).
* [BanjoRecomp3D](https://github.com/oneup03/BanjoRecomp3D) (clone recursivo): traz o `lib/rt64-3ds`.
* [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime), no commit que o BanjoRecomp3D fixa.

## Passos

```bash
# 1. Pastas
mkdir ~/stadium2-3ds && cd ~/stadium2-3ds
git clone --recursive https://github.com/michiiik/pokemonStadiumGSRecomp.git
git clone --recursive https://github.com/oneup03/BanjoRecomp3D.git
git clone <este repositório> s2-3ds-repo
mkdir -p s2-3ds/lib && cp -r s2-3ds-repo/source/* s2-3ds/
git clone https://github.com/N64Recomp/N64ModernRuntime.git s2-3ds/lib/N64ModernRuntime

# 2. Patches
git -C s2-3ds/lib/N64ModernRuntime apply ../../../s2-3ds-repo/patches/N64ModernRuntime.diff
git -C BanjoRecomp3D/lib/rt64-3ds apply ../../../s2-3ds-repo/patches/rt64-3ds.diff
git -C pokemonStadiumGSRecomp apply ../s2-3ds-repo/patches/pokemonStadiumGSRecomp.diff

# 3. A sua ROM (US, z64, SHA-1 d8343e69a7dc63b869cf6361d87cde64444281d3)
cp /caminho/da/sua/rom.z64 pokemonStadiumGSRecomp/baserom.z64

# 4. Recompilador e geração do código (N64Recomp + RSPRecomp)
cmake -S s2-3ds-repo/tools/N64Recomp-s2 -B n64recomp-build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C n64recomp-build
cd pokemonStadiumGSRecomp
../n64recomp-build/N64Recomp game.toml          # gera generated/funcs_*.c
for t in aspMain.ps2.us.toml rsp/gbTowerMain.us.rev1.toml rsp/gbTowerColorMain.us.rev1.toml \n         rsp/gbTowerS2C.us.toml rsp/gbTowerS2B.us.toml; do   # os dois últimos: vídeo do GB na GB Tower (rsp/s2c/)
  ../n64recomp-build/RSPRecomp "$t"
done
python3 ../s2-3ds-repo/tools/strip_watchdog.py rsp/s2c/gbTowerS2C.cpp rsp/s2c/gbTowerS2B.cpp   # tira o watchdog/pc_trail (mais rápido)
python3 ../s2-3ds-repo/tools/p58_ai_hook.py generated   # som do jogo de GB na GB Tower
cd ..
mkdir -p s2-3ds/rsp-up && cp pokemonStadiumGSRecomp/rsp/*.cpp s2-3ds/rsp-up/   # confira o output_file_path de cada toml

# 5. Port do 3DS
R=~/stadium2-3ds/BanjoRecomp3D/lib/rt64-3ds
DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM \
cmake -S s2-3ds/platform/3ds -B s2-3ds/build-3ds -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=$R/cmake/devkitARM-3ds.cmake \
      -DS2_GAME_DIR=$HOME/stadium2-3ds/pokemonStadiumGSRecomp -DRT64_3DS_DIR=$R
ninja -C s2-3ds/build-3ds
```

Saída: `s2-3ds/build-3ds/PokemonStadium2Recompiled3DS.3dsx` e `.cia`.

## Observações
* Os patches de `source/compat/patches/` estão numerados na ordem em que entraram. Os que já estão incorporados aos arquivos de `source/` não precisam ser aplicados de novo; eles ficam como histórico.
* O código do port compila os `generated/funcs_*.c` em `-Os` para caber na memória do New 3DS; os ucodes do RSP (`rsp-up/`, `rsp/s2c/`) vão em `-O3`.
* Rode o `p58_ai_hook.py` de novo sempre que regenerar o `generated/`.
* Não faça commit da ROM, do `generated/`, do `rsp-up/` nem de saves (o `.gitignore` já bloqueia).
