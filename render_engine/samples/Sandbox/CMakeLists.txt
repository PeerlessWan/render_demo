cmake_minimum_required(VERSION 3.24)

include(ShaderCompile)
engine_find_dxc()

set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${_shader_out}")
set(_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube.hlsl")
set(_ui_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/ui_imgui.hlsl")
set(_post_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/post_ssao_taa.hlsl")
set(_dbg_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/debug_line.hlsl")

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

  add_custom_command(
    OUTPUT "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}"
           "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}" "${_dbg_vs}" "${_dbg_ps}"
           "${_shader_out}/instance_cull_cs.cso"
  COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_lit_vs}" "${_src}"
  COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_lit_ps}" "${_src}"
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
  COMMAND "${DXC_EXECUTABLE}" -T cs_6_0 -E CSMain -Fo "${_shader_out}/instance_cull_cs.cso"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_cs.hlsl"
  DEPENDS "${_src}" "${_ui_src}" "${_post_src}" "${_dbg_src}"
          "${CMAKE_SOURCE_DIR}/shaders/hlsl/instance_cull_cs.hlsl"
  COMMENT "DXC compile lit/shadow/quad/ui/post/debug/cull shaders"
  VERBATIM
)
add_custom_target(sample_sandbox_shaders DEPENDS
  "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}"
  "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}" "${_dbg_vs}" "${_dbg_ps}"
  "${_shader_out}/instance_cull_cs.cso"
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
)
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
  set(_vk_post_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/post_tonemap_vk.hlsl")
  engine_compile_hlsl_spirv(
    TARGET sample_sandbox_vk_post
    SOURCE "${_vk_post_src}"
    OUTPUT_DIR "${_shader_out}"
    VS_ENTRY VSMain VS_OUT post_tonemap_vk.vs.spv
    PS_ENTRY PSMain PS_OUT post_tonemap_vk.ps.spv
  )
  add_custom_command(
    OUTPUT "${_vk_sh_vs}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ShadowVS -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -fvk-t-shift 2 0 -fvk-s-shift 2 0 -Fo "${_vk_sh_vs}" "${_vk_src}"
    DEPENDS "${_vk_src}"
    COMMENT "DXC SPIR-V compile ShadowVS"
    VERBATIM
  )
  add_custom_target(sample_sandbox_vk_shadow DEPENDS "${_vk_sh_vs}")
  add_dependencies(sample_sandbox sample_sandbox_vk_lit sample_sandbox_vk_post sample_sandbox_vk_shadow)
endif()
