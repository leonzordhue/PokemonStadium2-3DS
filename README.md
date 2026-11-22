# Pokémon Stadium 2 — New Nintendo 3DS

Port nativo do **Pokémon Stadium 2 (N64, versão americana)** para o **New Nintendo 3DS**, feito com recompilação estática ([N64: Recompiled](https://github.com/N64Recomp/N64Recomp)) e o runtime [rt64-3ds](https://github.com/oneup03/rt64-3ds). Não é um emulador: o código do jogo foi traduzido para rodar direto no processador do 3DS.

### [Baixe a versão mais recente aqui](../../releases/latest)

> **Este repositório e os releases NÃO contêm a ROM nem nenhum arquivo do jogo.**
> Você precisa ter a sua própria cópia do Pokémon Stadium 2 (cartucho americano) e gerar a ROM dele.

---

## Sumário
* [Situação atual](#situação-atual)
* [O que você precisa](#o-que-você-precisa)
* [Instalação passo a passo](#instalação-passo-a-passo)
* [Transfer Pak (Pokémon do Game Boy)](#transfer-pak-pokémon-do-game-boy)
* [Controles](#controles)
* [Perguntas frequentes](#perguntas-frequentes)
* [Problemas conhecidos](#problemas-conhecidos)
* [Compilando](#compilando)
* [Créditos](#créditos)

---

## Situação atual

**Versão beta.** Dá para jogar o jogo inteiro, incluindo a **GB Tower**.

| Parte do jogo | Situação |
|---|---|
| Menus e batalhas (Stadium, Gym Leader Castle, Free Battle) | ✅ Funciona |
| Minigames, Pokémon Academy, Laboratório | ⚠️ Pouco testado: avise se achar problema |
| Transfer Pak (Pokémon do Red/Blue/Yellow/Gold/Silver/Crystal) | ✅ Funciona |
| Música e efeitos sonoros | ✅ Funciona |
| 3D estereoscópico | ✅ Funciona (ajuste no slider) |
| **GB Tower** (jogar o Game Boy dentro do Stadium) | ✅ Funciona, com save real e som (testado com Red, Blue, Yellow, Gold, Silver e Crystal; pode ficar um pouco abaixo da velocidade normal) |

A GB Tower usa as mesmas ROMs e saves de Game Boy do [Transfer Pak](#transfer-pak-pokémon-do-game-boy).

## O que você precisa

1. **New Nintendo 3DS, New 3DS XL ou New 2DS XL.** O 3DS/2DS antigo (Old 3DS) não é suportado: falta memória e CPU.
2. **Custom firmware** ([Luma3DS](https://github.com/LumaTeam/Luma3DS)). Se você ainda não tem, siga o guia oficial: **https://3ds.hacks.guide/pt_BR/**
3. **A ROM do Pokémon Stadium 2 americano**, gerada a partir do **seu** cartucho, no formato `.z64`.
4. Um cartão SD com pelo menos **150 MB livres**.

### A ROM certa

O jogo só aceita **uma** ROM: o Pokémon Stadium 2 dos EUA (código `NP3E`), em `.z64` (big-endian).

| | |
|---|---|
| Tamanho | 67.108.864 bytes (64 MB) |
| SHA-1 | `d8343e69a7dc63b869cf6361d87cde64444281d3` |
| MD5 | `1561c75d11cedf356a8ddb1a4a5f9d5d` |

Para conferir no Windows (PowerShell): `Get-FileHash stadium2.z64 -Algorithm SHA1`.

**Como as pessoas geram a ROM do próprio cartucho:** com um leitor de cartucho de N64 ligado ao PC (por exemplo o *Retrode 2* com adaptador de N64, o *INLretro* ou o *Sanni Cart Reader*). O leitor salva o conteúdo do cartucho num arquivo. Se o arquivo sair como `.v64` ou `.n64` (byte order trocado), converta para `.z64` (ex.: com o programa *Tool64*) antes de copiar. **Não pedimos e não fornecemos ROMs.**

## Instalação passo a passo

Escolha **um** dos dois formatos do release:

### Opção A: `.cia` (aparece no menu HOME, recomendado)
1. Baixe `PokemonStadium2-3DS.cia` na [página de releases](../../releases/latest).
2. Copie para qualquer pasta do cartão SD (ex.: `sdmc:/cias/`).
3. No 3DS, abra o **FBI**, vá em *SD → cias*, selecione o arquivo e escolha **Install CIA**.

### Opção B: `.3dsx` (Homebrew Launcher)
1. Baixe `PokemonStadium2-3DS.3dsx`.
2. Copie para `sdmc:/3ds/`.
3. Abra pelo **Homebrew Launcher**.

### Depois, em qualquer opção: a ROM
1. Crie a pasta `sdmc:/3ds/PokemonStadium2/` no cartão SD.
2. Renomeie a sua ROM para **`stadium2.z64`** e coloque dentro dela.

O cartão deve ficar assim:
```
sdmc:/
└── 3ds/
    ├── PokemonStadium2-3DS.3dsx      (só na opção B)
    └── PokemonStadium2/
        ├── stadium2.z64              ← a sua ROM
        └── gb/                       ← opcional: ROMs e saves do Game Boy (Transfer Pak)
            ├── Pokemon Crystal.gbc
            └── Pokemon Crystal.sav
```
Na primeira vez o jogo confere a ROM (leva alguns segundos). O save do Stadium 2 fica na mesma pasta e é criado sozinho.

## Transfer Pak (Pokémon do Game Boy)

Para usar os seus Pokémon do Red/Blue/Yellow/Gold/Silver/Crystal, você tem duas opções:

* **ROM + save do Game Boy:** coloque o `.gb`/`.gbc` e o `.sav` com o **mesmo nome** em `sdmc:/3ds/PokemonStadium2/gb/`. O jogo encontra sozinho. O save usado fica em `gb/saves/`, e o seu `.sav` original não é alterado.
* **Virtual Console do 3DS:** se o Pokémon Red/Blue/Yellow/Gold/Silver/Crystal do eShop estiver instalado no console, coloque só a ROM em `gb/`. O jogo conecta o save do Virtual Console direto.

Também funciona com a pasta do port do Stadium 1 (`sdmc:/3ds/PokemonStadium/gb/`), se você já tiver ela.

Na tela de baixo, a página **Game Pak** mostra os 4 Transfer Paks e qual cartucho está em cada um. Toque para conectar/desconectar. Para configurar à mão, crie `sdmc:/3ds/PokemonStadium2/transferpak.cfg`:
```
p1_rom=gb/Pokemon Crystal.gbc
p1_save=gb/Pokemon Crystal.sav
p2_rom=gb/Pokemon Red.gb
p2_save=vc
```

## Controles

Mesmo esquema do port do Pokémon Stadium 1 para 3DS:

| 3DS | N64 |
|---|---|
| Circle Pad | Analógico |
| A | A |
| B | B |
| X | Z |
| Y | C-baixo |
| L / R | L / R |
| START | START |
| D-pad | Botões C |
| C-Stick (New 3DS) | D-pad |
| **SELECT** | **Menu de configurações** (controles, 3D, Transfer Pak) |

Os botões podem ser trocados no menu do SELECT.

## Perguntas frequentes

#### Posso usar outra ROM (europeia, japonesa, hack)?
**Não.** Este projeto é um port de **uma** versão específica: o Pokémon Stadium 2 americano. Ele não é um emulador e não roda outras ROMs.

#### O jogo fecha ou mostra erro ao abrir.
Confira o nome (`stadium2.z64`), a pasta (`sdmc:/3ds/PokemonStadium2/`) e o SHA-1 da ROM. A causa mais comum é ROM em `.v64`/`.n64` ou de outra região.

#### Funciona no Citra / Azahar?
Funciona no [Azahar](https://azahar-emu.org/) (mais lento que no console). Coloque a ROM em `sdmc/3ds/PokemonStadium2/` dentro da pasta de usuário do emulador.

#### Funciona no 3DS antigo?
Não. Só nos modelos **New**.

#### Onde fica o save?
`sdmc:/3ds/PokemonStadium2/`. Faça backup dessa pasta antes de atualizar.

## Problemas conhecidos
* **GB Tower:** o jogo de Game Boy pode rodar um pouco abaixo da velocidade normal, e a imagem do Game Boy pode pular quadros (o 3DS desenha sempre o quadro mais recente).
* Minigames, Pokémon Academy e Laboratório foram pouco testados: avise se achar problema.

## Compilando
Não precisa compilar para jogar: use os arquivos dos [releases](../../releases). Para compilar a partir do código, veja [BUILDING.md](BUILDING.md). É um processo avançado e exige a sua ROM.

## Créditos
* [N64Recomp](https://github.com/N64Recomp/N64Recomp): recompilação estática do código do N64.
* [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime): substitui as bibliotecas do N64.
* [rt64-3ds](https://github.com/oneup03/rt64-3ds) e [BanjoRecomp3D](https://github.com/oneup03/BanjoRecomp3D) (oneup03): runtime, renderizador e base dos ports para 3DS.
* [pokemonStadiumGSRecomp](https://github.com/michiiik/pokemonStadiumGSRecomp) (michiiik): recompilação do Pokémon Stadium 2 para PC.
* [PokemonStadiumRecomp](https://github.com/mstan/PokemonStadiumRecomp): referência para Transfer Pak e GB Tower.
* [devkitPro](https://devkitpro.org/) / libctru: toolchain do 3DS.

Pokémon e Pokémon Stadium 2 são marcas da Nintendo, Creatures e GAME FREAK. Este é um projeto de fãs, sem fins lucrativos e sem ligação com essas empresas. Licença: GPL-3.0 (ver [COPYING](COPYING)).
