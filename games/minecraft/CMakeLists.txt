cmake_minimum_required(VERSION 3.24)

add_library(minecraft_core STATIC
  sim/blocks.cpp
  sim/inventory.cpp
  sim/crafting.cpp
  sim/player.cpp
  sim/time.cpp
  sim/mobs.cpp
  sim/containers.cpp
  sim/gameplay.cpp
  sim/sfx.cpp
  world/chunk.cpp
  world/world.cpp
  world/gen.cpp
  world/batch.cpp
  world/trace.cpp
  world/tick.cpp
  io/world_save.cpp
)
target_include_directories(minecraft_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(minecraft_core PUBLIC game_kit engine_app engine_rhi engine_scene engine_core engine_media)
target_compile_features(minecraft_core PUBLIC cxx_std_20)

add_executable(minecraft_survival
  app/main.cpp
  ui/hud.cpp
)
target_link_libraries(minecraft_survival PRIVATE minecraft_core engine_d3d12 engine_ui)
kit_use_shaders(minecraft_survival)

if(MSVC)
  target_compile_options(minecraft_core PRIVATE /W4)
  target_compile_options(minecraft_survival PRIVATE /W4)
endif()
