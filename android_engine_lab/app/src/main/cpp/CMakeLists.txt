cmake_minimum_required(VERSION 3.22.1)
project(odpar_music_motion_lab C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/engine")
if(NOT EXISTS "${ENGINE_ROOT}/src")
  message(FATAL_ERROR "Official ODPAR engine not staged at ${ENGINE_ROOT}")
endif()

file(GLOB_RECURSE ODM_ENGINE_SOURCES CONFIGURE_DEPENDS "${ENGINE_ROOT}/src/*.c")
list(FILTER ODM_ENGINE_SOURCES EXCLUDE REGEX "/src/cli/")
list(LENGTH ODM_ENGINE_SOURCES ODM_ENGINE_SOURCE_COUNT)
if(NOT ODM_ENGINE_SOURCE_COUNT EQUAL 182)
  message(FATAL_ERROR "Surface guard: expected 182 production C sources from official 0.52 ZIP, found ${ODM_ENGINE_SOURCE_COUNT}")
endif()

# OBJECT library + explicit object injection into the final .so. This makes all
# 182 production translation units part of the APK instead of allowing archive
# extraction to omit modules that the current UI does not call yet.
add_library(odpar_engine OBJECT ${ODM_ENGINE_SOURCES})
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
  "${CMAKE_CURRENT_SOURCE_DIR}/generated")
target_compile_options(odpar_engine PRIVATE -O3 -fstack-protector-strong)
target_compile_definitions(odpar_engine PRIVATE _FORTIFY_SOURCE=2 ODM_SHA256_OPENSSL_ACCEL=1 ODM_PLATFORM_ANDROID=1)

# Native PNG capability is preserved and built for the same ARM64 target.
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng" "${CMAKE_CURRENT_BINARY_DIR}/libpng")
# png.c is one of the 182 ODPAR production objects, so its compile command must
# see both libpng's public headers and the generated pnglibconf.h. Linking
# png_static only at the final shared object is too late for that translation
# unit. Make the compile-time dependency explicit; do not remove png.c.
target_include_directories(odpar_engine PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libpng"
  "${CMAKE_CURRENT_BINARY_DIR}/libpng")

# Ed25519 package signing and accelerated SHA-256 remain intact. OpenSSL is
# cross-compiled by CI for android-arm64; a missing crypto library is fatal.
set(OPENSSL_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl")
if(NOT EXISTS "${OPENSSL_ANDROID_ROOT}/libcrypto.a")
  message(FATAL_ERROR "Android OpenSSL libcrypto.a missing; refusing to drop ODPAR crypto capabilities")
endif()
if(NOT EXISTS "${OPENSSL_ANDROID_ROOT}/include/openssl/evp.h")
  message(FATAL_ERROR "Android OpenSSL headers missing; refusing to drop ODPAR crypto capabilities")
endif()
add_library(odpar_crypto STATIC IMPORTED GLOBAL)
set_target_properties(odpar_crypto PROPERTIES IMPORTED_LOCATION "${OPENSSL_ANDROID_ROOT}/libcrypto.a")
target_include_directories(odpar_engine PUBLIC "${OPENSSL_ANDROID_ROOT}/include")

add_library(odpar_lab SHARED
  native_bridge.c
  odpar_lab_scene.c
  "${ENGINE_ROOT}/src/cli/selftest.c"
  $<TARGET_OBJECTS:odpar_engine>)
target_include_directories(odpar_lab PRIVATE
  "${ENGINE_ROOT}/include" "${ENGINE_ROOT}/src/cli" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(odpar_lab PRIVATE -O3 -fstack-protector-strong)
find_library(log-lib log)
find_library(android-lib android)
find_library(m-lib m)
find_library(z-lib z)
find_library(dl-lib dl)
target_link_libraries(odpar_lab PRIVATE
  png_static odpar_crypto
  ${log-lib} ${android-lib} ${m-lib} ${z-lib} ${dl-lib})
target_link_options(odpar_lab PRIVATE -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
