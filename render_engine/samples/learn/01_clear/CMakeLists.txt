cmake_minimum_required(VERSION 3.24)

add_executable(sample_01_clear main.cpp)
target_link_libraries(sample_01_clear PRIVATE engine_app)
if(WIN32 AND TARGET engine_d3d12)
  target_link_libraries(sample_01_clear PRIVATE engine_d3d12)
endif()
target_compile_features(sample_01_clear PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(sample_01_clear PRIVATE /W4)
endif()

# Vulkan lit cube: DXC → SPIR-V next to the executable (optional --mesh).
if(ENGINE_WITH_VULKAN)
  include(ShaderCompile)
  engine_find_dxc()
  set(_vk_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  engine_compile_hlsl_spirv(
    TARGET sample_01_clear_vk_shaders
    SOURCE "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube_vk.hlsl"
    OUTPUT_DIR "${_vk_shader_out}"
    VS_ENTRY VSMain VS_OUT lit_cube_vk.vs.spv
    PS_ENTRY PSMain PS_OUT lit_cube_vk.ps.spv
  )
  set(_vk_sh "${_vk_shader_out}/shadow_vk.vs.spv")
  add_custom_command(
    OUTPUT "${_vk_sh}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ShadowVS -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -fvk-t-shift 2 0 -fvk-s-shift 2 0 -Fo "${_vk_sh}" "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube_vk.hlsl"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube_vk.hlsl"
    COMMENT "DXC SPIR-V compile ShadowVS (01_clear)"
    VERBATIM
  )
  add_custom_target(sample_01_clear_vk_shadow DEPENDS "${_vk_sh}")
  add_dependencies(sample_01_clear sample_01_clear_vk_shaders sample_01_clear_vk_shadow)
  add_custom_command(TARGET sample_01_clear POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:sample_01_clear>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_vk_shader_out}/lit_cube_vk.vs.spv"
      "${_vk_shader_out}/lit_cube_vk.ps.spv"
      "${_vk_sh}"
      "$<TARGET_FILE_DIR:sample_01_clear>/shaders"
    COMMENT "Copy Vulkan lit/shadow SPIR-V next to sample_01_clear"
  )
endif()
