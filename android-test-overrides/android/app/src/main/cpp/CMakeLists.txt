cmake_minimum_required(VERSION 3.22.1)
project(odpar_authoring3d LANGUAGES C)

set(ODPAR_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")

add_library(odpar_authoring3d SHARED
    native_bridge.c
    ${ODPAR_ROOT}/src/compositor/background.c
    ${ODPAR_ROOT}/src/compositor/core.c
    ${ODPAR_ROOT}/src/compositor/field.c
    ${ODPAR_ROOT}/src/compositor/hud.c
    ${ODPAR_ROOT}/src/compositor/layout.c
    ${ODPAR_ROOT}/src/compositor/math.c
    ${ODPAR_ROOT}/src/compositor/reaction.c
    ${ODPAR_ROOT}/src/compositor/render.c
    ${ODPAR_ROOT}/src/compositor/supersample.c
    ${ODPAR_ROOT}/src/core/fixed.c
    ${ODPAR_ROOT}/src/core/hash.c
    ${ODPAR_ROOT}/src/core/job.c
    ${ODPAR_ROOT}/src/core/memory_budget.c
    ${ODPAR_ROOT}/src/core/ownership.c
    ${ODPAR_ROOT}/src/core/progress.c
    ${ODPAR_ROOT}/src/core/status.c
    ${ODPAR_ROOT}/src/core/time.c
    ${ODPAR_ROOT}/src/core/trig_fixed.c
    ${ODPAR_ROOT}/src/core/wire.c
    ${ODPAR_ROOT}/src/presentation/app_authoring3d.c
    ${ODPAR_ROOT}/src/presentation/authoring_node3d.c
    ${ODPAR_ROOT}/src/presentation/camera_follow3d.c
    ${ODPAR_ROOT}/src/presentation/direct_manipulation3d.c
    ${ODPAR_ROOT}/src/presentation/editor_locator3d.c
    ${ODPAR_ROOT}/src/presentation/editor_view3d.c
    ${ODPAR_ROOT}/src/presentation/mesh_authoring.c
    ${ODPAR_ROOT}/src/presentation/mesh_authoring_surface.c
    ${ODPAR_ROOT}/src/presentation/model_import.c
    ${ODPAR_ROOT}/src/presentation/model_socket3d.c
    ${ODPAR_ROOT}/src/presentation/motion_path3d.c
    ${ODPAR_ROOT}/src/presentation/spatial_grounding3d.c
    ${ODPAR_ROOT}/src/renderer/color.c
    ${ODPAR_ROOT}/src/scene3d/camera.c
    ${ODPAR_ROOT}/src/scene3d/graph.c
    ${ODPAR_ROOT}/src/scene3d/light_transport.c
    ${ODPAR_ROOT}/src/scene3d/material.c
    ${ODPAR_ROOT}/src/scene3d/math3d.c
    ${ODPAR_ROOT}/src/scene3d/mesh.c
    ${ODPAR_ROOT}/src/scene3d/quality.c
    ${ODPAR_ROOT}/src/scene3d/raster.c
    ${ODPAR_ROOT}/src/scene3d/shadow.c
)

target_include_directories(odpar_authoring3d PRIVATE
    ${ODPAR_ROOT}/include
    ${ODPAR_ROOT}/src/core
    ${ODPAR_ROOT}/src/compositor
    ${ODPAR_ROOT}/src/ir
    ${ODPAR_ROOT}/src/presentation
    ${ODPAR_ROOT}/src/renderer
    ${ODPAR_ROOT}/src/scene3d
)

target_compile_features(odpar_authoring3d PRIVATE c_std_11)
target_compile_definitions(odpar_authoring3d PRIVATE ODM_SHA256_DISABLE_ACCEL=1)
target_compile_options(odpar_authoring3d PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -ffunction-sections -fdata-sections -fvisibility=hidden
)
target_link_options(odpar_authoring3d PRIVATE
    -Wl,--gc-sections
    -Wl,--no-undefined
    -Wl,-z,relro
    -Wl,-z,now
    -Wl,-z,noexecstack
    -Wl,-z,max-page-size=16384
)
target_link_libraries(odpar_authoring3d PRIVATE android log m)
