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
list(FILTER _engine EXCLUDE REGEX "gpu_soft\\.c$")     # the real GPU replaces the software one
file(GLOB _game ${ROOT}/game/*.c)

# only the libraries Deadbird uses (the engine has its own maths, so not psxgte)
set(PSN00BSDK_EXECUTABLE_LINK_LIBRARIES psxgpu psxapi psxetc c)

psn00bsdk_add_executable(deadbird STATIC main.c gpu_psx.c ${_engine} ${_game})
target_include_directories(deadbird PRIVATE ${ROOT}/engine ${ROOT}/game ${ROOT}/assets)
target_compile_definitions(deadbird PRIVATE PRIM_MAX=3000)
target_compile_options(deadbird PRIVATE -O2 -Wall -Wno-unused-parameter)

psn00bsdk_add_cd_image(
	iso          # target name
	deadbird     # output: deadbird.bin + deadbird.cue
	iso.xml
	DEPENDS deadbird system.cnf
)
