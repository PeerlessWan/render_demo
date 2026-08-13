cmake_minimum_required(VERSION 3.24)

add_executable(engine_unit_tests
  unit/main.cpp
  unit/test_math.cpp
  unit/test_config.cpp
  unit/test_path.cpp
  unit/test_assets_m3.cpp
  unit/test_m4_m19.cpp
  unit/test_m20_m25_headless.cpp
  unit/test_headless_integration.cpp
)
target_include_directories(engine_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(engine_unit_tests PRIVATE
  engine_core
  engine_assets
  engine_rhi
  engine_input
  engine_scene
  engine_debug
  engine_animation
  engine_post
  engine_physics
  engine_media
  engine_ui
  engine_render2d
  engine_net
  engine_mixed
  engine_gi
  engine_terrain
  engine_gpu_driven
  engine_rt
  engine_app
)
target_compile_features(engine_unit_tests PRIVATE cxx_std_20)

add_test(NAME unit.engine_unit_tests COMMAND engine_unit_tests)

# Headless-only filter: same binary, Catch-like tag not available; run full suite as headless-safe.
# Dedicated target alias for CI scripts: `ctest -R headless`
add_test(NAME headless.engine_unit_tests COMMAND engine_unit_tests)
set_tests_properties(headless.engine_unit_tests PROPERTIES LABELS "headless")
