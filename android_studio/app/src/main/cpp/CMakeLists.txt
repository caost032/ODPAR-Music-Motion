cmake_minimum_required(VERSION 3.22.1)
project(odpar_music_motion_studio C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/engine")
if(NOT EXISTS "${ENGINE_ROOT}/src")
  message(FATAL_ERROR "Canonical ODPAR engine not staged at ${ENGINE_ROOT}")
endif()

file(GLOB_RECURSE ODM_ENGINE_SOURCES CONFIGURE_DEPENDS "${ENGINE_ROOT}/src/*.c")
list(FILTER ODM_ENGINE_SOURCES EXCLUDE REGEX "/src/cli/")
list(LENGTH ODM_ENGINE_SOURCES ODM_ENGINE_SOURCE_COUNT)
if(NOT ODM_ENGINE_SOURCE_COUNT EQUAL 182)
  message(FATAL_ERROR "Engine surface guard: expected 182 production C sources, found ${ODM_ENGINE_SOURCE_COUNT}")
endif()

add_library(odpar_engine STATIC ${ODM_ENGINE_SOURCES})
target_include_directories(odpar_engine PUBLIC
  "${ENGINE_ROOT}/include"
  "${ENGINE_ROOT}/src/cli"
  "${ENGINE_ROOT}/src/spine"
  "${ENGINE_ROOT}/src/media"
  "${ENGINE_ROOT}/src/music_map"
  "${ENGINE_ROOT}/src/music_inference"
  "${ENGINE_ROOT}/src/ir"
  "${ENGINE_ROOT}/src/package"
  "${ENGINE_ROOT}/src/renderer"
  "${ENGINE_ROOT}/src/visual"
  "${ENGINE_ROOT}/src/visual_dynamics"
  "${ENGINE_ROOT}/src/theme"
  "${ENGINE_ROOT}/src/design"
  "${ENGINE_ROOT}/src/preview"
  "${ENGINE_ROOT}/src/master"
  "${ENGINE_ROOT}/src/studio"
  "${ENGINE_ROOT}/src/delivery"
  "${ENGINE_ROOT}/src/export"
  "${ENGINE_ROOT}/src/compositor"
  "${ENGINE_ROOT}/src/scene3d"
  "${ENGINE_ROOT}/src/reaction3d"
  "${ENGINE_ROOT}/src/presentation"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_compile_options(odpar_engine PRIVATE
  -O3 -fstack-protector-strong -ffunction-sections -fdata-sections
  -Wall -Wextra
)
target_compile_definitions(odpar_engine PRIVATE _FORTIFY_SOURCE=2)

set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng" "${CMAKE_CURRENT_BINARY_DIR}/libpng")
target_link_libraries(odpar_engine PUBLIC png_static)

target_include_directories(odpar_engine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl-install/include"
)
target_link_libraries(odpar_engine PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl-install/lib/libcrypto.a"
)

add_library(odpar_studio SHARED studio_native_bridge.c)
target_include_directories(odpar_studio PRIVATE
  "${ENGINE_ROOT}/include"
  "${ENGINE_ROOT}/src/cli"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_compile_options(odpar_studio PRIVATE -O3 -fstack-protector-strong -Wall -Wextra -Werror)

find_library(log-lib log)
find_library(android-lib android)
find_library(m-lib m)
find_library(z-lib z)
find_library(dl-lib dl)

target_link_libraries(odpar_studio PRIVATE
  odpar_engine
  ${log-lib}
  ${android-lib}
  ${m-lib}
  ${z-lib}
  ${dl-lib}
)
target_link_options(odpar_studio PRIVATE
  -Wl,--gc-sections -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
)
