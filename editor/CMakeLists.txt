cmake_minimum_required(VERSION 3.24)

add_executable(editor_app
  app/main.cpp
  editing/undo.cpp
  editing/voxel_undo.cpp
  editing/gizmo.cpp
  io/scene_io.cpp
  ui/editor_ui.cpp
  ui/voxel_ui.cpp
)
target_include_directories(editor_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_app PRIVATE
  game_kit
  minecraft_core
  engine_app
  engine_d3d12
  engine_ui
  engine_mixed
  engine_debug
)
kit_use_shaders(editor_app)
target_compile_features(editor_app PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(editor_app PRIVATE /W4)
endif()
if(WIN32)
  target_link_libraries(editor_app PRIVATE user32)
endif()

# ED0–ED5 smoke: no Device / Application — scene IO, undo, Play snapshot restore.
add_executable(editor_smoke_tests
  tests/main.cpp
  tests/test_smoke.cpp
  editing/undo.cpp
  io/scene_io.cpp
)
target_include_directories(editor_smoke_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/../game_kit/tests
)
target_link_libraries(editor_smoke_tests PRIVATE game_kit engine_scene engine_core)
target_compile_features(editor_smoke_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(editor_smoke_tests PRIVATE /W4)
endif()
