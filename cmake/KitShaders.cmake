# Compile the engine HLSL set used by game_kit samples and editor (D3D12 CSO).

function(kit_setup_shaders)
  include(ShaderCompile)
  engine_find_dxc()

  set(_src_root "${RENDER_ENGINE_ROOT}/shaders/hlsl")
  set(_out "${CMAKE_BINARY_DIR}/kit_shaders")
  file(MAKE_DIRECTORY "${_out}")

  set(_src "${_src_root}/lit_cube.hlsl")
  set(_ui_src "${_src_root}/ui_imgui.hlsl")
  set(_post_src "${_src_root}/post_ssao_taa.hlsl")
  set(_dbg_src "${_src_root}/debug_line.hlsl")
  set(_sky_src "${_src_root}/skybox.hlsl")

  set(_outputs
    "${_out}/lit_cube.vs.cso" "${_out}/lit_cube.ps.cso"
    "${_out}/shadow.vs.cso" "${_out}/shadow.ps.cso"
    "${_out}/quad.vs.cso" "${_out}/quad.ps.cso"
    "${_out}/ui_imgui.vs.cso" "${_out}/ui_imgui.ps.cso"
    "${_out}/post_ssao_taa.vs.cso" "${_out}/post_ssao_taa.ps.cso"
    "${_out}/debug_line.vs.cso" "${_out}/debug_line.ps.cso"
    "${_out}/skybox.vs.cso" "${_out}/skybox.ps.cso"
  )

  add_custom_command(
    OUTPUT ${_outputs}
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_out}/lit_cube.vs.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_6 -E PSMain -Fo "${_out}/lit_cube.ps.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ShadowVS -Fo "${_out}/shadow.vs.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E ShadowPS -Fo "${_out}/shadow.ps.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E QuadVS -Fo "${_out}/quad.vs.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E QuadPS -Fo "${_out}/quad.ps.cso" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_out}/ui_imgui.vs.cso" "${_ui_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_out}/ui_imgui.ps.cso" "${_ui_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_out}/post_ssao_taa.vs.cso" "${_post_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_out}/post_ssao_taa.ps.cso" "${_post_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_out}/debug_line.vs.cso" "${_dbg_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_out}/debug_line.ps.cso" "${_dbg_src}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E VSMain -Fo "${_out}/skybox.vs.cso" "${_sky_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E PSMain -Fo "${_out}/skybox.ps.cso" "${_sky_src}"
    DEPENDS "${_src}" "${_ui_src}" "${_post_src}" "${_dbg_src}" "${_sky_src}"
    COMMENT "DXC compile kit shaders"
    VERBATIM
  )
  add_custom_target(kit_shaders DEPENDS ${_outputs})

  file(TO_CMAKE_PATH "${_out}" _out_def)
  string(REPLACE "\\" "/" _out_def "${_out_def}")
  set(KIT_SHADER_DIR "${_out_def}" PARENT_SCOPE)
endfunction()

function(kit_use_shaders target)
  add_dependencies(${target} kit_shaders)
  target_compile_definitions(${target} PRIVATE ENGINE_SHADER_DIR_A="${KIT_SHADER_DIR}")
endfunction()
