cmake_minimum_required(VERSION 3.24)

include(ShaderCompile)

set(_shader_out "${CMAKE_CURRENT_BINARY_DIR}/shaders")
engine_compile_hlsl(
  TARGET sample_02_triangle_shaders
  SOURCE "${CMAKE_SOURCE_DIR}/shaders/hlsl/triangle.hlsl"
  OUTPUT_DIR "${_shader_out}"
  VS_ENTRY VSMain VS_OUT triangle.vs.cso
  PS_ENTRY PSMain PS_OUT triangle.ps.cso
)

file(TO_CMAKE_PATH "${_shader_out}" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

add_executable(sample_02_triangle main.cpp)
add_dependencies(sample_02_triangle sample_02_triangle_shaders)
target_link_libraries(sample_02_triangle PRIVATE engine_app engine_d3d12)
target_compile_features(sample_02_triangle PRIVATE cxx_std_20)
target_compile_definitions(sample_02_triangle PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
)

if(MSVC)
  target_compile_options(sample_02_triangle PRIVATE /W4)
endif()
