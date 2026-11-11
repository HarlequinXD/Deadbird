cmake_minimum_required(VERSION 3.18)
project(deadbird C)

# repo root: platform/android/app/src/main/cpp -> ../../../../../..
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB ENGINE_SRC ${ROOT}/engine/*.c)
file(GLOB GAME_SRC ${ROOT}/game/*.c)
file(GLOB GEN_SRC ${ROOT}/gen/*.c)

# the engine and its Android platform layer, then the game
add_library(deadbird SHARED ${ROOT}/engine/platform/android/android_main.c ${ENGINE_SRC} ${GAME_SRC} ${GEN_SRC})
target_include_directories(deadbird PRIVATE ${ROOT}/engine ${ROOT}/game ${ROOT}/gen ${ROOT}/assets)
set_target_properties(deadbird PROPERTIES C_STANDARD 99)
# built for size: the level, sounds and textures are generated at start-up (gen/)
target_compile_options(deadbird PRIVATE -Os -Wall -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fvisibility=hidden)
target_compile_options(native_app_glue PRIVATE -Os -ffunction-sections -fdata-sections)
# keep the NativeActivity entry point from the static glue library
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate -Wl,--gc-sections -Wl,--strip-all")
target_link_libraries(deadbird native_app_glue android EGL GLESv2 OpenSLES log)
