cmake_minimum_required(VERSION 3.24)

include(ShaderCompile)
engine_find_dxc()

set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${_shader_out}")
set(_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube.hlsl")
set(_ui_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/ui_imgui.hlsl")
set(_post_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/post_ssao_taa.hlsl")
set(_dbg_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/debug_line.hlsl")
set(_sky_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/skybox.hlsl")

set(_lit_vs "${_shader_out}/lit_cube.vs.cso")
set(_lit_ps "${_shader_out}/lit_cube.ps.cso")
set(_sh_vs "${_shader_out}/shadow.vs.cso")
set(_sh_ps "${_shader_out}/shadow.ps.cso")
set(_qd_vs "${_shader_out}/quad.vs.cso")
set(_qd_ps "${_shader_out}/quad.ps.cso")
set(_ui_vs "${_shader_out}/ui_imgui.vs.cso")
set(_ui_ps "${_shader_out}/ui_imgui.ps.cso")
set(_post_vs "${_shader_out}/post_ssao_taa.vs.cso")
set(_post_ps "${_shader_out}/post_ssao_taa.ps.cso")
set(_dbg_vs "${_shader_out}/debug_line.vs.cso")
set(_dbg_ps "${_shader_out}/debug_line.ps.cso")
set(_sky_vs "${_shader_out}/skybox.vs.cso")
set(_sky_ps "${_shader_out}/skybox.ps.cso")

  add_custom_command(
    OUTPUT "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}"
           "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}" "${_dbg_vs}" "${_dbg_ps}"
           "${_sky_vs}" "${_sky_ps}"
           "${_shader_out}/instance_cull_cs.cso"
           "${_shader_out}/skin_cs.cso"
           "${_shader_out}/light_tile_cull_cs.cso"
           "${_shader_out}/dxr_shadow_lib.cso"
           "${_shader_out}/meshlet_ms.ms.cso"
           "${_shader_out}/meshlet_ms.ps.cso"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_lit_vs}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_6 -E PSMain -Fo "${_lit_ps}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ShadowVS -Fo "${_sh_vs}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E ShadowPS -Fo "${_sh_ps}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E QuadVS -Fo "${_qd_vs}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E QuadPS -Fo "${_qd_ps}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_ui_vs}" "${_ui_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_ui_ps}" "${_ui_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_post_vs}" "${_post_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_post_ps}" "${_post_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_dbg_vs}" "${_dbg_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_dbg_ps}" "${_dbg_src}"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_sky_vs}" "${_sky_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_sky_ps}" "${_sky_src}"
  COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -Fo "${_shader_out}/instance_cull_cs.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_cs.hlsl"
  COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -Fo "${_shader_out}/skin_cs.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/skin_cs.hlsl"
  COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -Fo "${_shader_out}/light_tile_cull_cs.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/light_tile_cull_cs.hlsl"
  COMMAND "${DXC_EXECUTABLE}" -T lib_6_3 -Fo "${_shader_out}/dxr_shadow_lib.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/dxr_shadow_lib.hlsl"
  COMMAND "${DXC_EXECUTABLE}" -T ms_6_5 -E MSMain -Fo "${_shader_out}/meshlet_ms.ms.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/meshlet_ms.hlsl"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_shader_out}/meshlet_ms.ps.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/meshlet_ms.hlsl"
  DEPENDS "${_src}" "${_ui_src}" "${_post_src}" "${_dbg_src}" "${_sky_src}"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_cs.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/skin_cs.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/light_tile_cull_cs.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/dxr_shadow_lib.hlsl"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/meshlet_ms.hlsl"
  COMMENT "DXC compile lit/shadow/quad/ui/post/debug/sky/cull/skin/tile/dxr/meshlet shaders"
  VERBATIM
)
add_custom_target(sample_sandbox_shaders DEPENDS
  "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}"
  "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}" "${_dbg_vs}" "${_dbg_ps}"
  "${_sky_vs}" "${_sky_ps}"
  "${_shader_out}/instance_cull_cs.cso"
  "${_shader_out}/skin_cs.cso"
  "${_shader_out}/light_tile_cull_cs.cso"
  "${_shader_out}/dxr_shadow_lib.cso"
  "${_shader_out}/meshlet_ms.ms.cso"
  "${_shader_out}/meshlet_ms.ps.cso"
)

file(TO_CMAKE_PATH "${_shader_out}" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

add_executable(sample_sandbox main.cpp)
add_dependencies(sample_sandbox sample_sandbox_shaders)
target_link_libraries(sample_sandbox PRIVATE
  engine_app engine_assets engine_physics engine_media engine_ui engine_mixed engine_render2d
  engine_net engine_gi engine_terrain engine_vfx engine_animation engine_debug engine_gpu_driven
  engine_gameplay engine_clothing engine_vt engine_rt engine_ocean engine_hlod
)
if(WIN32)
  target_link_libraries(sample_sandbox PRIVATE psapi)
endif()
target_compile_features(sample_sandbox PRIVATE cxx_std_20)
target_compile_definitions(sample_sandbox PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
)
if(MSVC)
  target_compile_options(sample_sandbox PRIVATE /W4)
endif()

# Vulkan CSM lit path: SPIR-V beside DXIL outputs (Sandbox --backend=vulkan).
if(ENGINE_WITH_VULKAN)
  set(_vk_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube_vk.hlsl")
  set(_vk_vs "${_shader_out}/lit_cube_vk.vs.spv")
  set(_vk_ps "${_shader_out}/lit_cube_vk.ps.spv")
  set(_vk_sh_vs "${_shader_out}/shadow_vk.vs.spv")
  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_lit
    SOURCE "${_vk_src}"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT lit_cube_vk.vs.spv
    PS_ENTRY PSMain PS_OUT lit_cube_vk.ps.spv
  )
  set(_vk_post_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/post_ssao_taa_vk.hlsl")
  set(_vk_post_vs "${_shader_out}/post_ssao_taa_vk.vs.spv")
  set(_vk_post_ps "${_shader_out}/post_ssao_taa_vk.ps.spv")
  # No t/s shift: explicit [[vk::binding]] in shader.
  add_custom_command(
    OUTPUT "${_vk_post_vs}" "${_vk_post_ps}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vk_post_vs}" "${_vk_post_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vk_post_ps}" "${_vk_post_src}"
    DEPENDS "${_vk_post_src}"
    COMMENT "DXC SPIR-V compile post_ssao_taa_vk (SSAO/TAA/fog/bloom/tonemap)"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_post DEPENDS "${_vk_post_vs}" "${_vk_post_ps}")
  add_custom_command(
    OUTPUT "${_vk_sh_vs}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ShadowVS -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -fvk-t-shift 2 0 -fvk-s-shift 2 0 -Fo "${_vk_sh_vs}" "${_vk_src}"
    DEPENDS "${_vk_src}"
    COMMENT "DXC SPIR-V compile ShadowVS"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_shadow DEPENDS "${_vk_sh_vs}")

  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_sky
    SOURCE "${_sky_src}"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT skybox_vk.vs.spv
    PS_ENTRY PSMain PS_OUT skybox_vk.ps.spv
  )
  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_ui
    SOURCE "${CMAKE_SOURCE_DIR}/shaders/hlsl/ui_imgui_vk.hlsl"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT ui_imgui_vk.vs.spv
    PS_ENTRY PSMain PS_OUT ui_imgui_vk.ps.spv
  )
  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_debug
    SOURCE "${_dbg_src}"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT debug_line_vk.vs.spv
    PS_ENTRY PSMain PS_OUT debug_line_vk.ps.spv
  )
  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_quad
    SOURCE "${CMAKE_SOURCE_DIR}/shaders/hlsl/quad_vk.hlsl"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT quad_vk.vs.spv
    PS_ENTRY PSMain PS_OUT quad_vk.ps.spv
  )
  # Instance cull CS → SPIR-V (Wave 3b; always-visible InterlockedAdd InstanceCount).
  set(_vk_cull_cs "${_shader_out}/instance_cull_vk.cs.spv")
  add_custom_command(
    OUTPUT "${_vk_cull_cs}"
    COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vk_cull_cs}"
            "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_vk.hlsl"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_vk.hlsl"
    COMMENT "DXC SPIR-V compile instance_cull_vk CS"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_cull DEPENDS "${_vk_cull_cs}")
  # Mega-W8 C12: GPU skin CS → SPIR-V
  set(_vk_skin_cs "${_shader_out}/skin_cs_vk.cs.spv")
  add_custom_command(
    OUTPUT "${_vk_skin_cs}"
    COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vk_skin_cs}"
            "${CMAKE_SOURCE_DIR}/shaders/hlsl/skin_cs_vk.hlsl"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/hlsl/skin_cs_vk.hlsl"
    COMMENT "DXC SPIR-V compile skin_cs_vk CS"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_skin DEPENDS "${_vk_skin_cs}")
  # Mega-W9 C02: light tile cull CS → SPIR-V
  set(_vk_tile_cs "${_shader_out}/light_tile_cull_cs_vk.cs.spv")
  add_custom_command(
    OUTPUT "${_vk_tile_cs}"
    COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vk_tile_cs}"
            "${CMAKE_SOURCE_DIR}/shaders/hlsl/light_tile_cull_cs_vk.hlsl"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/hlsl/light_tile_cull_cs_vk.hlsl"
    COMMENT "DXC SPIR-V compile light_tile_cull_cs_vk CS"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_tile DEPENDS "${_vk_tile_cs}")
  add_dependencies(sample_sandbox
    sample_sandbox_vk_lit sample_sandbox_vk_post sample_sandbox_vk_shadow
    sample_sandbox_vk_sky sample_sandbox_vk_ui sample_sandbox_vk_debug sample_sandbox_vk_quad
    sample_sandbox_vk_cull sample_sandbox_vk_skin sample_sandbox_vk_tile)
endif()
