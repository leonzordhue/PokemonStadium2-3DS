# Pokemon Stadium 2: Recompiled for the New Nintendo 3DS (rt64-3ds).
#
#   cmake -S platform/3ds -B build-3ds -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<rt64-3ds>/cmake/devkitARM-3ds.cmake
#
# Model: oneup03's BanjoRecomp3D / Goemon64Recomp3D platform/3ds and
# rt64-3ds docs/porting-a-game.md ("Setting up a game repository").
# The generated sources come from the desktop recomp project
# (michiiik/pokemonStadiumGSRecomp, generated/ and rsp/), made on the host by
# the recovered N64Recomp fork with static-dedup (~/stadium2-3ds/gerar2.sh).
# Stadium 2 has no patches.elf and no mods: no RecompiledPatches, no
# BakedMods, the runtime built with RECOMP_NO_MODS.
cmake_minimum_required(VERSION 3.20)
project(PokemonStadium2Recompiled3DS LANGUAGES C CXX ASM)

set(S2_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
# The desktop project: generated/, rsp/, src/main/register_overlays.cpp.
set(S2_GAME_DIR "${S2_ROOT}/../pokemonStadiumGSRecomp" CACHE PATH "pokemonStadiumGSRecomp checkout (generated/, rsp/)")
set(RT64_3DS_DIR "${S2_ROOT}/../BanjoRecomp3D/lib/rt64-3ds" CACHE PATH "rt64-3ds checkout")
# The runtime with rt64-3ds's nmr-patches applied (nmr-patches/apply.sh).
# Frente A decides which one: upstream + nmr-patches + compat layer (compat/),
# or the mstan fork ported. Default: a patched copy beside this project.
set(RT64_3DS_NMR_DIR "${S2_ROOT}/lib/N64ModernRuntime" CACHE PATH "N64ModernRuntime with the 3DS patches")
# Header the generated code is compiled against (recomp_context layout).
# The fork's recomp.h (DMULT without __int128) until compat/ replaces it.
set(S2_RECOMP_INCLUDE "${S2_ROOT}/compat/include" CACHE PATH "recomp.h the generated code was made for")
# Stadium 2 has no mods: build the runtime without its mod system.
set(RT64_3DS_STATIC_MODS OFF)
add_compile_definitions(RECOMP_NO_MODS=1)
# The ROM is 64 MB; with 16 MB of RDRAM the New 3DS heap holds it beside a
# 16 MB linear heap (Dinosaur Planet's measured layout, porting guide item 9).
set(RT64_3DS_LINEAR_HEAP_MB 16)
# Frente A (compat/NOTAS.md): same recomp.h for runtime and generated code; 16 MB RDRAM mask.
include_directories(BEFORE "${S2_ROOT}/compat/include")
add_compile_definitions(RECOMP_MEM_MASK=0x00FFFFFFu RECOMP_3DS_PREEMPT_CHECK RECOMP_CF_NOTE_DISABLED)
add_subdirectory("${RT64_3DS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rt64-3ds")
# Boot diagnosis (19/11): [os] thread/mesg trace in ultramodern. Remove when the boot works.
# D28: per-queue blocked time in osRecvMesg, 1x/s ("qwait:" lines). Configure with -DS2_QUEUE_WAIT=ON.
option(S2_QUEUE_WAIT "Log how long game threads wait per message queue" OFF)
if(S2_QUEUE_WAIT AND TARGET ultramodern)
    target_compile_definitions(ultramodern PRIVATE S2_QUEUE_WAIT)
endif()
option(S2_BOOT_TRACE "ultramodern thread/message trace" ON)
if(S2_BOOT_TRACE AND TARGET ultramodern)
    target_compile_definitions(ultramodern PRIVATE S2_BOOT_TRACE)
endif()

# The generated game code (12,082 functions, 246 files). -Os keeps it near the
# Stadium 1 3DS's 59 MB; the hot files can move to -O2 once measured.
file(GLOB FUNC_SOURCES CONFIGURE_DEPENDS "${S2_GAME_DIR}/generated/funcs_*.c")
add_library(RecompiledFuncs STATIC ${FUNC_SOURCES} "${S2_GAME_DIR}/generated/lookup.cpp" "${S2_GAME_DIR}/generated/dispatch_aliases.c")
target_include_directories(RecompiledFuncs PRIVATE
    "${S2_GAME_DIR}/generated" "${CMAKE_CURRENT_LIST_DIR}" "${S2_ROOT}/compat" "${S2_RECOMP_INCLUDE}"
    "${RT64_3DS_NMR_DIR}/ultramodern/include" "${RT64_3DS_NMR_DIR}/librecomp/include")
target_compile_options(RecompiledFuncs PRIVATE
    -Os -funwind-tables -fno-strict-aliasing -w -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
    -include "${CMAKE_CURRENT_LIST_DIR}/stadium2_host_protos.h")

# Nexus 0018: funcs_65.c holds func_8AF233B4 (3,777 lines); -O2 ran past 30 min on it. Last -O wins.
set_source_files_properties("${S2_GAME_DIR}/generated/funcs_65.c" PROPERTIES COMPILE_OPTIONS "-Os")

# The runtime compatibility layer for the fork's generated code
# (recomp_request_tailcall, recomp_register_runtime_fragment, ...): Frente A.
file(GLOB S2_COMPAT_SOURCES CONFIGURE_DEPENDS "${S2_ROOT}/compat/*.c" "${S2_ROOT}/compat/*.cpp")

add_executable(PokemonStadium2Recompiled3DS
    main_3ds.cpp
    gamepak_screen.cpp
    recomp_api_3ds.cpp
    "${S2_ROOT}/tpak/tpak_3ds.cpp"
    "${S2_ROOT}/tpak/tpak_hle_3ds.cpp"
    "${S2_ROOT}/tpak/vc_save_3ds.cpp"
    ${S2_COMPAT_SOURCES}
    "${S2_ROOT}/rsp-up/aspMain_ps2.cpp"
    "${S2_GAME_DIR}/rsp/njpgdspMain.cpp"
    "${S2_GAME_DIR}/rsp/gbTowerMain.cpp"
    "${S2_GAME_DIR}/rsp/gbTowerColorMain.cpp"
    "${S2_GAME_DIR}/rsp/s2c/gbTowerS2C.cpp"   # 0084
    "${S2_GAME_DIR}/rsp/s2c/gbTowerS2B.cpp"   # 0092
    "${S2_GAME_DIR}/src/main/register_overlays.cpp"
)
target_include_directories(PokemonStadium2Recompiled3DS PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}" "${S2_ROOT}/compat" "${S2_ROOT}/tpak" "${S2_GAME_DIR}/include" "${S2_GAME_DIR}/generated"
    "${S2_RECOMP_INCLUDE}" "${RT64_3DS_NMR_DIR}/thirdparty")
target_compile_options(PokemonStadium2Recompiled3DS PRIVATE -O2 -Wno-unused-parameter)
# The RSP microcode runs scalar on the ARM11: the hottest files.
set_source_files_properties(
    "${S2_ROOT}/rsp-up/aspMain_ps2.cpp" "${S2_GAME_DIR}/rsp/njpgdspMain.cpp"
    "${S2_GAME_DIR}/rsp/gbTowerMain.cpp" "${S2_GAME_DIR}/rsp/gbTowerColorMain.cpp"
    "${S2_GAME_DIR}/rsp/s2c/gbTowerS2C.cpp"
    "${S2_GAME_DIR}/rsp/s2c/gbTowerS2B.cpp"
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-strict-aliasing;-w")
target_link_libraries(PokemonStadium2Recompiled3DS PRIVATE
    recomp_3ds rt64_3ds RecompiledFuncs ultramodern librecomp citro3d ctru m)
target_link_options(PokemonStadium2Recompiled3DS PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/PokemonStadium2Recompiled3DS.map")
# s2-3ds P10: Game_DoCopyProtection reads the cart at 0xB0000E38; no kseg1 ROM mirror on the 3DS (see main_3ds.cpp)
target_link_options(PokemonStadium2Recompiled3DS PRIVATE "-Wl,--wrap=func_80064C20")
# Bring-up: accept the display lists and draw nothing.
# s2-3ds P17/P21: dump failing aspMain_ps2 tasks (main_3ds.cpp) and DMEM-wrapping
# RSP DMAs with the audio command word (rsp.hpp). Configure with -DS2_AUDIO_DIAG=ON.
option(S2_AUDIO_DIAG "Log failing audio tasks and wrapping RSP DMAs" OFF)
if(S2_AUDIO_DIAG)
    target_compile_definitions(PokemonStadium2Recompiled3DS PRIVATE S2_AUDIO_DIAG)
    set_property(SOURCE "${S2_ROOT}/rsp-up/aspMain_ps2.cpp" APPEND PROPERTY
        COMPILE_DEFINITIONS "S2_DMA_CMD_W0=r26" "S2_DMA_CMD_W1=r25")
endif()
# s2-3ds P35b: aspMain_ps2 RESAMPLE 0x05, ADPCM 0x01, mixers 0x13/0x0C and FILTER 0x07 loops in scalar C++ (bit-exact, see rsp-up/aspMain_ps2.cpp).
option(S2_ASP_NATIVE "Native audio command loops in aspMain_ps2" OFF)
if(S2_ASP_NATIVE)
    set_property(SOURCE "${S2_ROOT}/rsp-up/aspMain_ps2.cpp" APPEND PROPERTY COMPILE_DEFINITIONS "S2_ASP_NATIVE")
endif()
option(S2_NULL_RENDERER "Build with rt64-3ds's null renderer" OFF)
if(S2_NULL_RENDERER)
    target_compile_definitions(PokemonStadium2Recompiled3DS PRIVATE S2_NULL_RENDERER)
endif()

set(S2_TITLE "Pokemon Stadium 2")
set(S2_DESCRIPTION "Pokemon Stadium 2 Recompiled - New 3DS")
# icon.png: original 48x48 art (nexus D30).
add_3dsx(PokemonStadium2Recompiled3DS TITLE "${S2_TITLE}" DESCRIPTION "${S2_DESCRIPTION}"
         ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png")
include("${RT64_3DS_DIR}/cmake/Cia.cmake")
# New 3DS focus: the template asks SystemModeExt 124MB.
# OLD3DS_MODE stays the library 80MB default. Banner: original art (nexus D30), jingle from rt64-3ds.
add_cia(PokemonStadium2Recompiled3DS RSF "${RT64_3DS_DIR}/cia/template.rsf" TITLE "${S2_TITLE}" PRODUCT "PKS2-3DS" UNIQUEID 0xF5D20
        ICON "${CMAKE_CURRENT_LIST_DIR}/icon.png" DESCRIPTION "${S2_DESCRIPTION}"
        BANNER_PNG "${CMAKE_CURRENT_LIST_DIR}/cia/banner.png" BANNER_WAV "${CMAKE_CURRENT_LIST_DIR}/cia/banner.wav")
