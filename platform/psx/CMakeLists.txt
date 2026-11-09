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
list(FILTER _engine EXCLUDE REGEX "(gpu_soft|snd_mix)\\.c$")   # the real GPU and SPU replace the software ones
file(GLOB _game ${ROOT}/game/*.c)

# psxgte only for InitGeom(): the hot vertex loops drive the GTE directly (engine/gte_hw.h)
set(PSN00BSDK_EXECUTABLE_LINK_LIBRARIES psxgte psxgpu psxspu psxetc psxapi c)

psn00bsdk_add_executable(deadbird STATIC main.c gpu_psx.c snd_psx.c ${_engine} ${_game})
target_include_directories(deadbird PRIVATE ${ROOT}/engine ${ROOT}/game ${ROOT}/assets)
# PSX_GTE: use the real GTE; shorter fog = fewer polygons for the 33 MHz CPU
target_compile_definitions(deadbird PRIVATE PRIM_MAX=2200 PSX_GTE=1 FOG_NEAR=7000 FOG_FAR=14500)
target_compile_options(deadbird PRIVATE -O2 -Wall -Wno-unused-parameter)

psn00bsdk_add_cd_image(
	iso          # target name
	deadbird     # output: deadbird.bin + deadbird.cue
	iso.xml
	DEPENDS deadbird system.cnf
)
