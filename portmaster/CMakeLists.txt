cmake_minimum_required(VERSION 3.13)
project(thug_portmaster CXX C ASM)

set(THUG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(CMAKE_CXX_STANDARD 98)
# R36S (RK3326): Cortex-A35 cores. THUG_TUNE_A35=OFF builds for generic ARMv7 as before.
option(THUG_TUNE_A35 "Tune code generation for the Cortex-A35 (R36S)" ON)
if(THUG_TUNE_A35)
  set(THUG_CPU_FLAGS "-mcpu=cortex-a35 -mfpu=neon-fp-armv8 -mfloat-abi=hard")
else()
  set(THUG_CPU_FLAGS "-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard")
endif()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98 -fpermissive -fno-exceptions -fno-rtti ${THUG_CPU_FLAGS} -O2 -fno-strict-aliasing -w")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=gnu99 ${THUG_CPU_FLAGS} -O2 -fno-strict-aliasing -w")

add_compile_definitions(
  __PLAT_WN32__
  __PLAT_VITA__
  VITA
  PORTMASTER
  KISAK_ORIGINAL_ALLOCATOR
  GSI_NO_THREADS
  THUG_RELEASE
)

set(PORTMASTER_SOURCES
  src/portmaster_platform.cpp
  src/portmaster_shader.cpp
  src/portmaster_present.cpp
  src/portmaster_profile.cpp
  src/portmaster_glrec.cpp
)

include("${THUG_ROOT}/vita/engine.cmake")

include(FetchContent)
FetchContent_Declare(gl4es
  GIT_REPOSITORY https://github.com/ptitSeb/gl4es.git
  GIT_TAG 1e5d45cb7448b45f2b9fad361d43d092e7f78aa9
  # R36S: user GLSL programs skip gl4es' per-draw custom-program path when
  # only irrelevant fixed state is set (runtime off switch THUG_GL4ES_USERPROG=0).
  PATCH_COMMAND git apply --ignore-whitespace "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gl4es-userprog-state.patch"
)
FetchContent_GetProperties(gl4es)
if(NOT gl4es_POPULATED)
  FetchContent_Populate(gl4es)
endif()
# SDL owns the EGL context. GL4ES supplies the Vita renderer's fixed-function API.
set(NOEGL ON CACHE BOOL "" FORCE)
set(NOX11 ON CACHE BOOL "" FORCE)
set(NO_LOADER ON CACHE BOOL "" FORCE)
set(NO_INIT_CONSTRUCTOR ON CACHE BOOL "" FORCE)
set(STATICLIB ON CACHE BOOL "" FORCE)
# Keep the Debian 10 cross toolchain usable with GL4ES's newer CMake check.
if(CMAKE_VERSION VERSION_LESS 3.19)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
endif()
add_subdirectory("${gl4es_SOURCE_DIR}" "${gl4es_BINARY_DIR}" EXCLUDE_FROM_ALL)
set_target_properties(GL PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "${gl4es_BINARY_DIR}")

add_executable(thug.armhf ${PORTMASTER_SOURCES} ${THUG_SOURCES})
set_target_properties(thug.armhf PROPERTIES SKIP_BUILD_RPATH TRUE)

target_include_directories(thug.armhf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${THUG_ROOT}/Code"
  "${THUG_ROOT}/Code/Sk"
  "${THUG_ROOT}/Code/Gfx/Vita"
  "${THUG_ROOT}/vita/gen"
  "${gl4es_SOURCE_DIR}/include"
  "/opt/r36s/include"
  "/opt/r36s/include/SDL2"
)

target_link_directories(thug.armhf PRIVATE
  "/opt/r36s/lib"
  "/usr/lib/arm-linux-gnueabihf"
)

target_link_libraries(thug.armhf
  SDL2
  GL
  pthread
  z
  m
  dl
)

set(THUG_FFMPEG_BINK_DIR "" CACHE PATH "Prefix of the PortMaster FFmpeg Bink build")
if(THUG_FFMPEG_BINK_DIR)
  set(THUG_MOVIES_DIR "${THUG_ROOT}/Code/Gel/Movies")
  target_sources(thug.armhf PRIVATE "${THUG_MOVIES_DIR}/Vita/vita_bink.c")
  set_source_files_properties(
    "${THUG_MOVIES_DIR}/Movies.cpp"
    "${THUG_MOVIES_DIR}/Vita/p_movies.cpp"
    PROPERTIES COMPILE_DEFINITIONS THUG_FMV
  )
  set_source_files_properties("${THUG_MOVIES_DIR}/Vita/vita_bink.c" PROPERTIES INCLUDE_DIRECTORIES "${THUG_FFMPEG_BINK_DIR}/include")
  target_link_directories(thug.armhf PRIVATE "${THUG_FFMPEG_BINK_DIR}/lib")
  target_link_libraries(thug.armhf avformat avcodec avutil)
endif()
