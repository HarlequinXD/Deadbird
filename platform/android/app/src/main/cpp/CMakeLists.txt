cmake_minimum_required(VERSION 3.18)
project(deadbird C)

# repo root: platform/android/app/src/main/cpp -> ../../../../../..
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)

file(GLOB ENGINE_SRC ${ROOT}/engine/*.c)
file(GLOB GAME_SRC ${ROOT}/game/*.c)

add_library(deadbird SHARED android_main.c ${ENGINE_SRC} ${GAME_SRC})
target_include_directories(deadbird PRIVATE ${ROOT}/engine ${ROOT}/game ${ROOT}/assets)
set_target_properties(deadbird PROPERTIES C_STANDARD 99)
target_compile_options(deadbird PRIVATE -O2 -Wall)
# keep the NativeActivity entry point from the static glue library
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
target_link_libraries(deadbird native_app_glue android EGL GLESv2 log m)
