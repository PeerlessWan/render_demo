cmake_minimum_required(VERSION 3.24)

add_executable(engine_unit_tests
  unit/main.cpp
  unit/test_math.cpp
  unit/test_config.cpp
  unit/test_path.cpp
  unit/test_assets_m3.cpp
  unit/test_m4_m19.cpp
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
)
target_compile_features(engine_unit_tests PRIVATE cxx_std_20)

add_test(NAME unit.engine_unit_tests COMMAND engine_unit_tests)
