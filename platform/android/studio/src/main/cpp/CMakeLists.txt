cmake_minimum_required(VERSION 3.18)
project(studio C)

# Deadbird Studio: the game editor (studio/) on the engine, its pixel-art UI (engine/studio), the
# gameplay framework it plays projects with (engine/fw) and the sector world
# repo root: platform/android/studio/src/main/cpp -> ../../../../../..
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB ENGINE_SRC ${ROOT}/engine/*.c)
file(GLOB FW_SRC ${ROOT}/engine/fw/*.c)
file(GLOB UI_SRC ${ROOT}/engine/studio/*.c)
file(GLOB STUDIO_SRC ${ROOT}/studio/*.c)
file(GLOB GEN_SRC ${ROOT}/gen/*.c)

add_library(studio SHARED ${ROOT}/engine/platform/android/android_main.c ${ENGINE_SRC} ${FW_SRC} ${UI_SRC} ${STUDIO_SRC}
            ${ROOT}/examples/fps/sector_world.c ${ROOT}/game/level.c ${ROOT}/game/edmap.c ${GEN_SRC})
target_include_directories(studio PRIVATE ${ROOT}/engine ${ROOT}/engine/fw ${ROOT}/engine/studio ${ROOT}/studio
                           ${ROOT}/examples/fps ${ROOT}/game ${ROOT}/gen ${ROOT}/assets)
set_target_properties(studio PROPERTIES C_STANDARD 99)
target_compile_options(studio PRIVATE -O2 -Wall -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fvisibility=hidden)
target_compile_options(native_app_glue PRIVATE -Os -ffunction-sections -fdata-sections)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate -Wl,--gc-sections -Wl,--strip-all")
target_link_libraries(studio native_app_glue android EGL GLESv2 OpenSLES log m)
