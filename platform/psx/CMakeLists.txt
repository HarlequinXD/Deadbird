# Deadbird for the original PlayStation (PSn00bSDK).
#   cmake --preset default -S platform/psx   (needs PSN00BSDK_LIBS pointing at the SDK)
#   cmake --build platform/psx/build
# -> deadbird.exe (PS-EXE: load straight into an emulator) and deadbird.bin / deadbird.cue
#    (CD image: mount in DuckStation, PCSX-Redux, ePSXe, Mednafen...)
cmake_minimum_required(VERSION 3.21)

project(
	Deadbird
	LANGUAGES    C
	VERSION      0.3.0
	DESCRIPTION  "Deadbird - PS1 build"
)

set(ROOT ${PROJECT_SOURCE_DIR}/../..)
file(GLOB _engine ${ROOT}/engine/*.c)
list(FILTER _engine EXCLUDE REGEX "(gpu_soft|snd_mix|tim)\\.c$")   # the real GPU and SPU replace the software ones
file(GLOB _game ${ROOT}/game/*.c)

# The PC/Android builds generate the level, sounds and textures at start-up; the PS1 has no FPU,
# so tools/gen_dump (built with the HOST compiler from the same sources) precomputes them here.
set(BAKED ${CMAKE_CURRENT_BINARY_DIR}/baked)
file(GLOB _gen ${ROOT}/gen/*.c ${ROOT}/gen/*.h ${ROOT}/game/*.h ${ROOT}/engine/*.h ${ROOT}/assets/*.h)
file(GLOB _gen_src ${ROOT}/gen/*.c)
set(HOST_CC cc CACHE STRING "host C compiler for tools/gen_dump")
# -B/usr/bin/: the host linker, not the MIPS ld that the SDK puts first on PATH
add_custom_command(
	OUTPUT  ${BAKED}/baked.c ${BAKED}/sfx_adpcm.h
	COMMAND ${CMAKE_COMMAND} -E make_directory ${BAKED}
	COMMAND ${HOST_CC} -B/usr/bin/ -O2 -std=c99 -I${ROOT}/engine -I${ROOT}/game -I${ROOT}/gen -I${ROOT}/assets
	        ${ROOT}/tools/gen_dump.c ${ROOT}/engine/gpu_soft.c ${ROOT}/engine/gte.c ${ROOT}/game/assets.c ${_gen_src}
	        -o ${BAKED}/gen_dump -lm
	COMMAND ${BAKED}/gen_dump ${BAKED}
	DEPENDS ${ROOT}/tools/gen_dump.c ${_gen}
	COMMENT "Precomputing the level, sounds and textures for the PS1"
	VERBATIM
)

# psxgte only for InitGeom(): the hot vertex loops drive the GTE directly (engine/gte_hw.h)
set(PSN00BSDK_EXECUTABLE_LINK_LIBRARIES psxgte psxgpu psxspu psxetc psxapi c)

# the engine's PS1 layer (engine/platform/psx) + the engine + the game
set(PLAT ${ROOT}/engine/platform/psx)
psn00bsdk_add_executable(deadbird STATIC ${PLAT}/psx_main.c ${PLAT}/gpu_psx.c ${PLAT}/snd_psx.c ${_engine} ${_game} ${BAKED}/baked.c)
target_include_directories(deadbird PRIVATE ${ROOT}/engine ${ROOT}/game ${ROOT}/gen ${ROOT}/assets ${BAKED})
# PSX_GTE: use the real GTE; shorter fog = fewer polygons for the 33 MHz CPU
target_compile_definitions(deadbird PRIVATE PRIM_MAX=2200 PSX_GTE=1 FOG_NEAR=7000 FOG_FAR=14500 GEN_BAKED=1)
target_compile_options(deadbird PRIVATE -O2 -Wall -Wno-unused-parameter)

psn00bsdk_add_cd_image(
	iso          # target name
	deadbird     # output: deadbird.bin + deadbird.cue
	iso.xml
	DEPENDS deadbird system.cnf
)
