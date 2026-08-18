cmake_minimum_required(VERSION 3.24)

add_executable(editor_app
  app/main.cpp
  cmd/session.cpp
  mcp/protocol.cpp
  mcp/live_io.cpp
  editing/undo.cpp
  editing/voxel_undo.cpp
  editing/gizmo.cpp
  editing/ray.cpp
  editing/snap.cpp
  editing/ops.cpp
  editing/viewport_layout.cpp
  editing/terrain_edit.cpp
  editing/tile_edit.cpp
  editing/anim_edit.cpp
  editing/light_bake.cpp
  io/scene_io.cpp
  io/content_browser.cpp
  io/dep_graph.cpp
  io/scene_ext.cpp
  play/scene_play.cpp
  ui/editor_ui.cpp
  ui/voxel_ui.cpp
  ../games/minecraft/ui/hud.cpp
)
target_include_directories(editor_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../games/minecraft
)
target_link_libraries(editor_app PRIVATE
  game_kit
  minecraft_core
  engine_app
  engine_d3d12
  engine_ui
  engine_mixed
  engine_debug
  engine_physics
  engine_assets
  engine_terrain
  engine_render2d
  engine_animation
)
kit_use_shaders(editor_app)
target_compile_features(editor_app PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(editor_app PRIVATE /W4)
endif()
if(WIN32)
  target_link_libraries(editor_app PRIVATE user32 ws2_32)
endif()

# ED0–ED6 smoke: no Device / Application — scene IO, undo, snap, gizmo, content.
add_executable(editor_smoke_tests
  tests/main.cpp
  tests/test_smoke.cpp
  tests/test_unit.cpp
  tests/test_integration.cpp
  tests/test_automation.cpp
  cmd/session.cpp
  mcp/protocol.cpp
  editing/undo.cpp
  editing/snap.cpp
  editing/ops.cpp
  editing/ray.cpp
  editing/gizmo.cpp
  editing/viewport_layout.cpp
  editing/terrain_edit.cpp
  editing/tile_edit.cpp
  editing/anim_edit.cpp
  editing/light_bake.cpp
  io/scene_io.cpp
  io/content_browser.cpp
  io/dep_graph.cpp
  io/scene_ext.cpp
  play/scene_play.cpp
)
target_include_directories(editor_smoke_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
  ${CMAKE_CURRENT_SOURCE_DIR}/../game_kit/tests
)
target_link_libraries(editor_smoke_tests PRIVATE
  game_kit engine_scene engine_core engine_debug engine_assets engine_terrain
  engine_render2d engine_animation engine_rhi)
target_compile_features(editor_smoke_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(editor_smoke_tests PRIVATE /W4)
endif()

add_test(NAME editor_smoke_tests COMMAND editor_smoke_tests)
add_test(NAME editor_unit COMMAND editor_smoke_tests "[unit]")
add_test(NAME editor_integration COMMAND editor_smoke_tests "[integration]")
add_test(NAME editor_automation COMMAND editor_smoke_tests "[automation]")

add_executable(editor_mcp
  mcp/main.cpp
  mcp/protocol.cpp
  mcp/live_io.cpp
  cmd/session.cpp
  editing/undo.cpp
  editing/ops.cpp
  editing/snap.cpp
  editing/terrain_edit.cpp
  editing/tile_edit.cpp
  editing/anim_edit.cpp
  editing/light_bake.cpp
  play/scene_play.cpp
  io/scene_io.cpp
  io/scene_ext.cpp
  io/content_browser.cpp
  io/dep_graph.cpp
)
target_include_directories(editor_mcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(editor_mcp PRIVATE
  game_kit engine_scene engine_core engine_debug engine_assets engine_terrain
  engine_render2d engine_animation engine_rhi)
if(WIN32)
  target_link_libraries(editor_mcp PRIVATE ws2_32)
endif()
target_compile_features(editor_mcp PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(editor_mcp PRIVATE /W4)
endif()
