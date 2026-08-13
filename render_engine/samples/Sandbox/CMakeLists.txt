cmake_minimum_required(VERSION 3.24)

include(ShaderCompile)

set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
engine_compile_hlsl(
  TARGET sample_sandbox_shaders
  SOURCE "${CMAKE_SOURCE_DIR}/shaders/hlsl/lit_cube.hlsl"
  OUTPUT_DIR "${_shader_out}"
  VS_ENTRY VSMain VS_OUT lit_cube.vs.cso
  PS_ENTRY PSMain PS_OUT lit_cube.ps.cso
)

file(TO_CMAKE_PATH "${_shader_out}" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

add_executable(sample_sandbox main.cpp)
add_dependencies(sample_sandbox sample_sandbox_shaders)
target_link_libraries(sample_sandbox PRIVATE
  engine_app engine_physics engine_media engine_ui engine_mixed engine_render2d engine_net
)
target_compile_features(sample_sandbox PRIVATE cxx_std_20)
target_compile_definitions(sample_sandbox PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
)
if(MSVC)
  target_compile_options(sample_sandbox PRIVATE /W4)
endif()
