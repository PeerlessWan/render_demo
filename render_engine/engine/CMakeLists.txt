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
if(ENGINE_WITH_VULKAN)
  target_compile_definitions(engine_core PUBLIC ENGINE_WITH_VULKAN=1)
else()
  target_compile_definitions(engine_core PUBLIC ENGINE_WITH_VULKAN=0)
endif()

add_library(engine_assets STATIC
  assets/manifest.cpp
  assets/asset_system.cpp
  assets/streaming_budget.cpp
  assets/image_loader_stb.cpp
  assets/texture_loader_dds.cpp
  assets/gltf_loader.cpp
)
target_include_directories(engine_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_assets PUBLIC engine_core)
if(ENGINE_WITH_STB_IMAGE)
  if(NOT EXISTS "${ENGINE_STB_DIR}/stb_image.h")
    message(FATAL_ERROR "ENGINE_WITH_STB_IMAGE=ON but stb_image.h not found at ${ENGINE_STB_DIR}")
  endif()
  target_include_directories(engine_assets PRIVATE "${ENGINE_STB_DIR}")
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_STB_IMAGE=1)
  message(STATUS "stb_image enabled: ${ENGINE_STB_DIR}")
else()
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_STB_IMAGE=0)
endif()
if(ENGINE_WITH_DDSKTX)
  if(NOT EXISTS "${ENGINE_DDSKTX_DIR}/dds-ktx.h")
    message(FATAL_ERROR "ENGINE_WITH_DDSKTX=ON but dds-ktx.h not found at ${ENGINE_DDSKTX_DIR}")
  endif()
  target_include_directories(engine_assets PRIVATE "${ENGINE_DDSKTX_DIR}")
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_DDSKTX=1)
  message(STATUS "dds-ktx enabled: ${ENGINE_DDSKTX_DIR}")
else()
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_DDSKTX=0)
endif()
if(ENGINE_WITH_CGLTF)
  if(NOT EXISTS "${ENGINE_CGLTF_DIR}/cgltf.h")
    message(FATAL_ERROR "ENGINE_WITH_CGLTF=ON but cgltf.h not found at ${ENGINE_CGLTF_DIR}")
  endif()
  target_include_directories(engine_assets PRIVATE "${ENGINE_CGLTF_DIR}")
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_CGLTF=1)
  message(STATUS "cgltf enabled: ${ENGINE_CGLTF_DIR}")
else()
  target_compile_definitions(engine_assets PUBLIC ENGINE_WITH_CGLTF=0)
endif()

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
  render/local_lights.cpp
  render/quality.cpp
  backends/headless/headless_device.cpp
)
target_include_directories(engine_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_rhi PUBLIC engine_core engine_scene)

if(ENGINE_WITH_VULKAN)
  add_library(engine_vulkan STATIC
    backends/vulkan/vulkan_device.cpp
  )
  target_include_directories(engine_vulkan
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      "${ENGINE_VULKAN_SDK}/Include"
      ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan
  )
  target_link_libraries(engine_vulkan
    PUBLIC engine_rhi engine_platform
    PRIVATE "${ENGINE_VULKAN_SDK}/Lib/vulkan-1.lib"
  )
  if(WIN32)
    target_link_libraries(engine_vulkan PRIVATE user32)
  endif()
endif()

add_library(engine_physics STATIC
  physics/builtin_world.cpp
  physics/physics_factory.cpp
)
target_include_directories(engine_physics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_physics PUBLIC engine_core)
if(ENGINE_WITH_JOLT)
  # Keep Jolt from overriding parent flags / building samples.
  set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
  set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
  set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
  set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
  set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
  set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
  set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
  set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
  set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
  set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
  set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
  set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
  set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory("${ENGINE_JOLT_DIR}/Build" "${CMAKE_BINARY_DIR}/_deps/jolt")
  target_sources(engine_physics PRIVATE physics/jolt_world.cpp)
  target_link_libraries(engine_physics PRIVATE Jolt)
  # Jolt headers must stay private (ADR 0015 / 0017).
  target_include_directories(engine_physics PRIVATE "${ENGINE_JOLT_DIR}")
  target_compile_definitions(engine_physics PUBLIC ENGINE_WITH_JOLT=1)
  message(STATUS "Jolt enabled: ${ENGINE_JOLT_DIR}")
else()
  target_sources(engine_physics PRIVATE physics/jolt_world_stub.cpp)
  target_compile_definitions(engine_physics PUBLIC ENGINE_WITH_JOLT=0)
endif()

add_library(engine_media STATIC
  media/media.cpp
)
target_include_directories(engine_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_media PUBLIC engine_core)
if(WIN32)
  target_link_libraries(engine_media PRIVATE winmm)
endif()

add_library(engine_ui STATIC
  ui/retained_ui.cpp
  ui/immediate_ui.cpp
  ui/rml_ui_stub.cpp
)
target_include_directories(engine_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_ui PUBLIC engine_core engine_rhi engine_platform)

if(ENGINE_WITH_IMGUI)
  if(NOT EXISTS "${ENGINE_IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "ENGINE_WITH_IMGUI=ON but imgui.h not found at ${ENGINE_IMGUI_DIR}")
  endif()
  add_library(imgui STATIC
    "${ENGINE_IMGUI_DIR}/imgui.cpp"
    "${ENGINE_IMGUI_DIR}/imgui_draw.cpp"
    "${ENGINE_IMGUI_DIR}/imgui_tables.cpp"
    "${ENGINE_IMGUI_DIR}/imgui_widgets.cpp"
  )
  target_include_directories(imgui PUBLIC "${ENGINE_IMGUI_DIR}")
  if(MSVC)
    target_compile_options(imgui PRIVATE /W3)
  endif()
  target_link_libraries(engine_ui PUBLIC imgui)
  target_compile_definitions(engine_ui PUBLIC ENGINE_WITH_IMGUI=1)
  message(STATUS "Dear ImGui enabled: ${ENGINE_IMGUI_DIR}")
else()
  target_compile_definitions(engine_ui PUBLIC ENGINE_WITH_IMGUI=0)
  message(STATUS "Dear ImGui disabled")
endif()

add_library(engine_render2d STATIC
  render2d/sprite.cpp
  render2d/tilemap_stream.cpp
)
target_include_directories(engine_render2d PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_render2d PUBLIC engine_core)

add_library(engine_mixed STATIC
  mixed/pick.cpp
)
target_include_directories(engine_mixed PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_mixed PUBLIC engine_core engine_rhi engine_render2d)

add_library(engine_gi STATIC
  gi/probe_volume.cpp
)
target_include_directories(engine_gi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_gi PUBLIC engine_core)

add_library(engine_terrain STATIC
  terrain/heightmap.cpp
)
target_include_directories(engine_terrain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_terrain PUBLIC engine_core)

add_library(engine_gpu_driven STATIC
  gpu_driven/path.cpp
)
target_include_directories(engine_gpu_driven PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_gpu_driven PUBLIC engine_core)

add_library(engine_rt STATIC
  rt/raytracing.cpp
)
target_include_directories(engine_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_rt PUBLIC engine_core engine_rhi)

add_library(engine_net STATIC
  net/net_system.cpp
  net/http_httplib.cpp
)
target_include_directories(engine_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_net PUBLIC engine_core)
if(ENGINE_WITH_HTTPLIB)
  if(NOT EXISTS "${ENGINE_HTTPLIB_DIR}/httplib.h")
    message(FATAL_ERROR "ENGINE_WITH_HTTPLIB=ON but httplib.h not found at ${ENGINE_HTTPLIB_DIR}")
  endif()
  target_include_directories(engine_net PRIVATE "${ENGINE_HTTPLIB_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/net")
  target_compile_definitions(engine_net PUBLIC ENGINE_WITH_HTTPLIB=1)
  if(WIN32)
    target_link_libraries(engine_net PRIVATE ws2_32 crypt32)
  endif()
  if(ENGINE_WITH_OPENSSL AND OpenSSL_FOUND)
    target_compile_definitions(engine_net PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT ENGINE_WITH_OPENSSL=1)
    target_link_libraries(engine_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "cpp-httplib HTTPS via OpenSSL enabled")
  else()
    target_compile_definitions(engine_net PRIVATE ENGINE_WITH_OPENSSL=0)
  endif()
  message(STATUS "cpp-httplib enabled: ${ENGINE_HTTPLIB_DIR}")
else()
  target_include_directories(engine_net PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/net")
  target_compile_definitions(engine_net PUBLIC ENGINE_WITH_HTTPLIB=0)
endif()

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
  render/render_system.cpp
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
    engine_post
)
if(ENGINE_WITH_VULKAN)
  target_link_libraries(engine_app PUBLIC engine_vulkan)
endif()
