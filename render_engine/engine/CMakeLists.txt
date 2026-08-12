# engine static libraries + public headers under include/engine/

add_library(engine_core STATIC
  core/log.cpp
  core/config.cpp
  core/clock.cpp
  core/path_resolver.cpp
  core/math.cpp
  core/feature.cpp
)
target_include_directories(engine_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(engine_core PUBLIC cxx_std_20)

add_library(engine_assets STATIC
  assets/manifest.cpp
  assets/asset_system.cpp
  assets/streaming_budget.cpp
)
target_include_directories(engine_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_assets PUBLIC engine_core)
target_compile_features(engine_assets PUBLIC cxx_std_20)

add_library(engine_platform STATIC
  platform/win32/window_win32.cpp
)
target_include_directories(engine_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_platform PUBLIC engine_core)
if(WIN32)
  target_link_libraries(engine_platform PRIVATE user32 gdi32)
endif()

add_library(engine_input STATIC
  input/input_system.cpp
  input/input_stub.cpp
)
target_include_directories(engine_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_input PUBLIC engine_core)

add_library(engine_scene STATIC
  scene/world.cpp
  scene/serialization.cpp
)
target_include_directories(engine_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_scene PUBLIC engine_core)

add_library(engine_debug STATIC
  debug/debug_draw.cpp
  debug/console.cpp
)
target_include_directories(engine_debug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_debug PUBLIC engine_core)

add_library(engine_animation STATIC
  animation/skeleton.cpp
)
target_include_directories(engine_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_animation PUBLIC engine_core)

add_library(engine_post STATIC
  post/post_stack.cpp
)
target_include_directories(engine_post PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_post PUBLIC engine_core)

add_library(engine_rhi STATIC
  render/rhi/rhi_types.cpp
  render/rhi/backend.cpp
  render/rhi/submit_config.cpp
  render/frame_graph.cpp
  render/camera.cpp
  render/render_scene.cpp
  render/shadow_csm.cpp
  render/shadow_atlas.cpp
  render/quality.cpp
)
target_include_directories(engine_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_rhi PUBLIC engine_core engine_scene)

add_library(engine_physics STATIC
  physics/builtin_world.cpp
)
target_include_directories(engine_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_physics PUBLIC engine_core)

add_library(engine_media STATIC
  media/media.cpp
)
target_include_directories(engine_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_media PUBLIC engine_core)

add_library(engine_ui STATIC
  ui/retained_ui.cpp
)
target_include_directories(engine_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_ui PUBLIC engine_core)

add_library(engine_render2d STATIC
  render2d/sprite.cpp
)
target_include_directories(engine_render2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_render2d PUBLIC engine_core)

add_library(engine_net STATIC
  net/net_system.cpp
)
target_include_directories(engine_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_net PUBLIC engine_core)

add_library(engine_d3d12 STATIC
  backends/d3d12/d3d12_device.cpp
)
target_include_directories(engine_d3d12
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d12
)
target_link_libraries(engine_d3d12
  PUBLIC engine_rhi engine_platform
  PRIVATE d3d12 dxgi dxguid d3dcompiler
)

add_library(engine_app STATIC
  app/application.cpp
  app/module_system.cpp
)
target_include_directories(engine_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_app
  PUBLIC
    engine_core
    engine_platform
    engine_rhi
    engine_input
    engine_assets
    engine_scene
    engine_debug
    engine_net
    engine_d3d12
)
