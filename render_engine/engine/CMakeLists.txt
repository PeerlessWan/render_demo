# engine static libraries + public headers under include/engine/

add_library(engine_core STATIC
  core/log.cpp
  core/config.cpp
  core/clock.cpp
  core/path_resolver.cpp
  core/math.cpp
  core/feature.cpp
)
target_include_directories(engine_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
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
  assets/gltf_loader_parse.cpp
  assets/gltf_loader_mesh.cpp
  assets/gltf_loader_skin.cpp
  assets/shader_hot_reload.cpp
  assets/asset_hot_reload.cpp
  assets/shader_compile_hook.cpp
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
  platform/linux/window_x11.cpp
  platform/linux/window_wayland.cpp
)
if(WIN32)
  target_sources(engine_platform PRIVATE platform/win32/window_win32.cpp)
endif()
target_include_directories(engine_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_platform PUBLIC engine_core)
if(WIN32)
  target_link_libraries(engine_platform PRIVATE user32 gdi32 imm32)
endif()
# Mega-W11: real Xlib path when building on Linux with libX11 (Windows keeps stubs).
# W15/W16: Wayland optional alongside X11 (X11 remains CI baseline).
if(UNIX AND NOT APPLE)
  find_package(X11)
  if(X11_FOUND)
    target_include_directories(engine_platform PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(engine_platform PRIVATE ${X11_LIBRARIES})
    target_compile_definitions(engine_platform PRIVATE ENGINE_HAS_X11=1)
    message(STATUS "engine_platform: X11 enabled (ENGINE_HAS_X11); Window::Create → window_x11")
  else()
    message(STATUS "engine_platform: X11 not found; CreateX11WindowStub / Window::Create non-headless Unavailable")
  endif()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
    if(WAYLAND_CLIENT_FOUND)
      target_sources(engine_platform PRIVATE platform/linux/xdg_shell_minimal.c)
      target_include_directories(engine_platform PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR})
      target_link_libraries(engine_platform PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
      target_compile_definitions(engine_platform PRIVATE ENGINE_HAS_WAYLAND=1)
      message(STATUS "engine_platform: Wayland enabled (ENGINE_HAS_WAYLAND); W16 xdg-shell")
    else()
      message(STATUS "engine_platform: wayland-client not found; Wayland stub Unavailable")
    endif()
  endif()
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
  debug/sandbox_harness.cpp
)
target_include_directories(engine_debug PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_debug PUBLIC engine_core)

add_library(engine_animation STATIC
  animation/skeleton.cpp
  animation/state_machine.cpp
  animation/blend_tree.cpp
  animation/two_bone_ik.cpp
  animation/gpu_skin_d3d12.cpp
  animation/gpu_skin_vk.cpp
  animation/gpu_skin_main.cpp
  animation/anim_serialize.cpp
)
target_include_directories(engine_animation PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_animation PUBLIC engine_core)
if(WIN32)
  target_link_libraries(engine_animation PRIVATE d3d12 dxgi)
endif()
if(ENGINE_WITH_VULKAN)
  target_include_directories(engine_animation PRIVATE "${ENGINE_VULKAN_INCLUDE_DIR}")
  target_link_libraries(engine_animation PRIVATE "${ENGINE_VULKAN_LIBRARY}")
endif()
# Resolve skin_cs.cso / skin_cs_vk.cs.spv next to Sandbox shader outputs.
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _anim_shader_dir)
string(REPLACE "\\" "/" _anim_shader_dir "${_anim_shader_dir}")
target_compile_definitions(engine_animation PRIVATE ENGINE_SHADER_DIR_A="${_anim_shader_dir}")

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
  render/light_function.cpp
  render/atmosphere.cpp
  render/weather.cpp
  render/ies_profile.cpp
  render/quality.cpp
  render/ibl_pack.cpp
  render/instance_draw.cpp
  render/occlusion.cpp
  backends/headless/headless_device.cpp
)
target_include_directories(engine_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_rhi PUBLIC engine_core engine_scene)

if(ENGINE_WITH_VULKAN)
  add_library(engine_vulkan STATIC
    backends/vulkan/vulkan_device.cpp
    backends/vulkan/vulkan_device_core.cpp
    backends/vulkan/vulkan_device_compute.cpp
    backends/vulkan/vulkan_device_lit.cpp
    backends/vulkan/vulkan_device_post.cpp
    backends/vulkan/vulkan_device_resources.cpp
    backends/vulkan/gpu_compute_oneshot_vk.cpp
  )
  target_include_directories(engine_vulkan
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
      "${ENGINE_VULKAN_INCLUDE_DIR}"
      ${CMAKE_CURRENT_SOURCE_DIR}/backends/vulkan
  )
  target_link_libraries(engine_vulkan
    PUBLIC engine_rhi engine_platform
    PRIVATE "${ENGINE_VULKAN_LIBRARY}"
  )
  if(MSVC)
    target_compile_options(engine_vulkan PRIVATE /FS)
  endif()
  if(WIN32)
    target_link_libraries(engine_vulkan PRIVATE user32)
  endif()
  if(UNIX AND NOT APPLE)
    find_package(X11)
    if(X11_FOUND)
      target_include_directories(engine_vulkan PRIVATE ${X11_INCLUDE_DIR})
      target_link_libraries(engine_vulkan PRIVATE ${X11_LIBRARIES})
      target_compile_definitions(engine_vulkan PRIVATE ENGINE_HAS_X11=1)
    endif()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
      if(WAYLAND_CLIENT_FOUND)
        target_include_directories(engine_vulkan PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
        target_link_libraries(engine_vulkan PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
        target_compile_definitions(engine_vulkan PRIVATE ENGINE_HAS_WAYLAND=1)
      endif()
    endif()
  endif()
endif()

add_library(engine_physics STATIC
  physics/builtin_world.cpp
  physics/physics_factory.cpp
  physics/buoyancy.cpp
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
  media/upscaler.cpp
  media/upscaler_backends.cpp
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

if(ENGINE_WITH_RMLUI)
  target_include_directories(engine_ui PRIVATE "${ENGINE_RMLUI_DIR}/Include")
  target_compile_definitions(engine_ui PUBLIC ENGINE_WITH_RMLUI=1)
  message(STATUS "RmlUi vendor enabled (thin RetainedUi adapter): ${ENGINE_RMLUI_DIR}")
else()
  target_compile_definitions(engine_ui PUBLIC ENGINE_WITH_RMLUI=0)
endif()

add_library(engine_render2d STATIC
  render2d/sprite.cpp
  render2d/tilemap_stream.cpp
  render2d/atlas.cpp
  render2d/fx2d.cpp
  render2d/bmfont.cpp
  render2d/world_text.cpp
  render2d/path2d.cpp
  render2d/nineslice.cpp
  render2d/rich_text.cpp
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
  gi/reflection_probe.cpp
  gi/scene_capture.cpp
  gi/lightmap.cpp
)
target_include_directories(engine_gi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_gi PUBLIC engine_core)

add_library(engine_terrain STATIC
  terrain/heightmap.cpp
  terrain/chunk_stream.cpp
)
target_include_directories(engine_terrain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_terrain PUBLIC engine_core engine_assets)

add_library(engine_ocean STATIC
  ocean/fft_ocean.cpp
)
target_include_directories(engine_ocean PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_ocean PUBLIC engine_core engine_terrain)
target_link_libraries(engine_physics PUBLIC engine_ocean)

# Mega-W10: demo cape/skirt (ADR 0037) — not a DCC clothing pipeline.
add_library(engine_clothing STATIC
  clothing/garment_cloth.cpp
)
target_include_directories(engine_clothing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_clothing PUBLIC engine_core engine_physics)

# Mega-W10: possess walk/jump + third-person camera helper.
# Mega-W11: CharacterAsset glTF-or-capsule (ADR 0038).
add_library(engine_gameplay STATIC
  gameplay/possess_controller.cpp
  gameplay/character_asset.cpp
)
target_include_directories(engine_gameplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_gameplay PUBLIC engine_core engine_assets)

add_library(engine_vfx STATIC
  vfx/particles.cpp
  vfx/gpu_particles.cpp
  vfx/gpu_particles_d3d12.cpp
  vfx/gpu_particles_vk.cpp
  vfx/trail_ribbon.cpp
)
target_include_directories(engine_vfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_vfx PUBLIC engine_core engine_debug)
if(WIN32)
  target_link_libraries(engine_vfx PRIVATE d3d12 dxgi)
endif()
if(ENGINE_WITH_VULKAN)
  target_include_directories(engine_vfx PRIVATE "${ENGINE_VULKAN_INCLUDE_DIR}")
  target_link_libraries(engine_vfx PRIVATE "${ENGINE_VULKAN_LIBRARY}")
  target_compile_definitions(engine_vfx PRIVATE ENGINE_WITH_VULKAN=1)
endif()
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _vfx_shader_dir)
string(REPLACE "\\" "/" _vfx_shader_dir "${_vfx_shader_dir}")
target_compile_definitions(engine_vfx PRIVATE ENGINE_SHADER_DIR_A="${_vfx_shader_dir}")
# WeatherSystem (engine_rhi) drives ParticleEmitter for rain/snow curtains.
target_link_libraries(engine_rhi PUBLIC engine_vfx)

add_library(engine_gpu_driven STATIC
  gpu_driven/path.cpp
  gpu_driven/indirect_draw.cpp
  gpu_driven/meshlet.cpp
)
target_include_directories(engine_gpu_driven PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_gpu_driven PUBLIC engine_core engine_rhi)
if(WIN32)
  target_link_libraries(engine_gpu_driven PRIVATE d3d12 dxgi)
endif()
if(ENGINE_WITH_VULKAN)
  target_include_directories(engine_gpu_driven PRIVATE "${ENGINE_VULKAN_INCLUDE_DIR}")
  target_link_libraries(engine_gpu_driven PRIVATE "${ENGINE_VULKAN_LIBRARY}")
endif()
# W17 ADR 0041: meshoptimizer when vendored under third_party/meshoptimizer/src
set(_meshopt_root "${CMAKE_SOURCE_DIR}/third_party/meshoptimizer")
if(EXISTS "${_meshopt_root}/src/meshoptimizer.h" AND EXISTS "${_meshopt_root}/src/clusterizer.cpp")
  add_library(meshoptimizer_vendor STATIC
    "${_meshopt_root}/src/allocator.cpp"
    "${_meshopt_root}/src/clusterizer.cpp"
  )
  target_include_directories(meshoptimizer_vendor PUBLIC "${_meshopt_root}/src")
  target_link_libraries(engine_gpu_driven PRIVATE meshoptimizer_vendor)
  target_compile_definitions(engine_gpu_driven PRIVATE ENGINE_WITH_MESHOPTIMIZER=1)
  message(STATUS "meshoptimizer enabled: ${_meshopt_root}/src")
else()
  target_compile_definitions(engine_gpu_driven PRIVATE ENGINE_WITH_MESHOPTIMIZER=0)
  message(STATUS "meshoptimizer not found; MeshletizePreferMeshoptimizer → AABB")
endif()
# Resolve meshlet_ms.*.cso next to Sandbox shader outputs (Mega-W9 C08).
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _meshlet_shader_dir)
string(REPLACE "\\" "/" _meshlet_shader_dir "${_meshlet_shader_dir}")
target_compile_definitions(engine_gpu_driven PRIVATE ENGINE_SHADER_DIR_A="${_meshlet_shader_dir}")

add_library(engine_vt STATIC
  vt/virtual_texture.cpp
)
target_include_directories(engine_vt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_vt PUBLIC engine_core)

add_library(engine_hlod STATIC
  hlod/billboard_impostor.cpp
)
target_include_directories(engine_hlod PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_hlod PUBLIC engine_core)

add_library(engine_rt STATIC
  rt/raytracing.cpp
  rt/raytracing_gate.cpp
  rt/raytracing_dxr_demo.cpp
  rt/raytracing_soft_shadow.cpp
  rt/raytracing_vk.cpp
)
target_include_directories(engine_rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(engine_rt PUBLIC engine_core engine_rhi)
if(WIN32)
  target_link_libraries(engine_rt PRIVATE d3d12 dxgi)
endif()
if(ENGINE_WITH_VULKAN)
  target_include_directories(engine_rt PRIVATE "${ENGINE_VULKAN_INCLUDE_DIR}")
  target_link_libraries(engine_rt PRIVATE "${ENGINE_VULKAN_LIBRARY}")
endif()
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _rt_shader_dir)
string(REPLACE "\\" "/" _rt_shader_dir "${_rt_shader_dir}")
target_compile_definitions(engine_rt PRIVATE ENGINE_SHADER_DIR_A="${_rt_shader_dir}")

add_library(engine_net STATIC
  net/net_system.cpp
  net/http_httplib.cpp
  net/quic.cpp
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
    target_compile_definitions(engine_net PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT ENGINE_WITH_OPENSSL=1)
    target_link_libraries(engine_net PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    message(STATUS "cpp-httplib HTTPS via OpenSSL enabled")
  else()
    target_compile_definitions(engine_net PUBLIC ENGINE_WITH_OPENSSL=0)
  endif()
  message(STATUS "cpp-httplib enabled: ${ENGINE_HTTPLIB_DIR}")
else()
  target_include_directories(engine_net PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/net")
  target_compile_definitions(engine_net PUBLIC ENGINE_WITH_HTTPLIB=0)
endif()

# MsQuic: optional probe/link. Never silent-install. Default OFF unless headers+lib found.
if(ENGINE_WITH_MSQUIC)
  target_compile_definitions(engine_net PUBLIC ENGINE_WITH_MSQUIC=1)
  if(ENGINE_MSQUIC_INCLUDE_DIR)
    target_include_directories(engine_net PRIVATE "${ENGINE_MSQUIC_INCLUDE_DIR}")
  endif()
  if(ENGINE_MSQUIC_LIB)
    target_link_libraries(engine_net PRIVATE "${ENGINE_MSQUIC_LIB}")
  endif()
  if(ENGINE_MSQUIC_DLL)
    file(TO_CMAKE_PATH "${ENGINE_MSQUIC_DLL}" _msquic_dll)
    string(REPLACE "\\" "/" _msquic_dll "${_msquic_dll}")
    target_compile_definitions(engine_net PRIVATE "ENGINE_MSQUIC_DLL_PATH=\"${_msquic_dll}\"")
  endif()
  message(STATUS "MsQuic optional enable: ENGINE_WITH_MSQUIC=ON")
else()
  target_compile_definitions(engine_net PUBLIC ENGINE_WITH_MSQUIC=0)
endif()

# Mega-W11: D3D12 is Windows-only; Linux builds Vulkan + X11 instead.
if(WIN32)
  add_library(engine_d3d12 STATIC
    backends/d3d12/d3d12_device.cpp
    backends/d3d12/d3d12_device_core.cpp
    backends/d3d12/d3d12_device_compute.cpp
    backends/d3d12/d3d12_device_lit.cpp
    backends/d3d12/d3d12_device_post.cpp
    backends/d3d12/d3d12_device_resources.cpp
    backends/d3d12/gpu_compute_oneshot_d3d12.cpp
  )
  target_include_directories(engine_d3d12
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backends/d3d12
  )
  target_link_libraries(engine_d3d12
    PUBLIC engine_rhi engine_platform
    PRIVATE d3d12 dxgi dxguid d3dcompiler
  )
  # W20 MS hot path: resolve meshlet_ms.*.cso next to Sandbox shader outputs.
  file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _d3d12_shader_dir)
  string(REPLACE "\\" "/" _d3d12_shader_dir "${_d3d12_shader_dir}")
  target_compile_definitions(engine_d3d12 PRIVATE ENGINE_SHADER_DIR_A="${_d3d12_shader_dir}")
  target_compile_options(engine_d3d12 PRIVATE /FS)
endif()

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
    engine_post
)
if(WIN32 AND TARGET engine_d3d12)
  target_link_libraries(engine_app PUBLIC engine_d3d12)
endif()
if(ENGINE_WITH_VULKAN)
  target_link_libraries(engine_app PUBLIC engine_vulkan)
endif()
