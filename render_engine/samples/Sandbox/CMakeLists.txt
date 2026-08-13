cmake_minimum_required(VERSION 3.24)

include(ShaderCompile)
engine_find_dxc()

set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${_shader_out}")
set(_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube.hlsl")
set(_ui_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/ui_imgui.hlsl")
set(_post_src "${CMAKE_SOURCE_DIR}/shaders/hlsl/post_ssao_taa.hlsl")

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

add_custom_command(
  OUTPUT "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}" "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}"
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
  DEPENDS "${_src}" "${_ui_src}" "${_post_src}"
  COMMENT "DXC compile lit/shadow/quad/ui/post shaders"
  VERBATIM
)
add_custom_target(sample_sandbox_shaders DEPENDS
  "${_lit_vs}" "${_lit_ps}" "${_sh_vs}" "${_sh_ps}" "${_qd_vs}" "${_qd_ps}" "${_ui_vs}" "${_ui_ps}" "${_post_vs}" "${_post_ps}"
)

file(TO_CMAKE_PATH "${_shader_out}" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

add_executable(sample_sandbox main.cpp)
add_dependencies(sample_sandbox sample_sandbox_shaders)
target_link_libraries(sample_sandbox PRIVATE
  engine_app engine_assets engine_physics engine_media engine_ui engine_mixed engine_render2d engine_net
)
target_compile_features(sample_sandbox PRIVATE cxx_std_20)
target_compile_definitions(sample_sandbox PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
)
if(MSVC)
  target_compile_options(sample_sandbox PRIVATE /W4)
endif()
