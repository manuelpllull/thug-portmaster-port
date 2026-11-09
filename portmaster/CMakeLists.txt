cmake_minimum_required(VERSION 3.10)
project(thug_portmaster CXX C ASM)

set(THUG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

set(CMAKE_CXX_STANDARD 98)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98 -fpermissive -fno-exceptions -fno-rtti -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2 -fno-strict-aliasing -w")
set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} -std=gnu99 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -O2 -fno-strict-aliasing -w")

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
)

include("${THUG_ROOT}/vita/engine.cmake")

add_executable(thug.armhf ${PORTMASTER_SOURCES} ${THUG_SOURCES})

target_include_directories(thug.armhf PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${THUG_ROOT}/Code"
  "${THUG_ROOT}/Code/Sk"
  "${THUG_ROOT}/Code/Gfx/Vita"
  "${THUG_ROOT}/vita/gen"
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
