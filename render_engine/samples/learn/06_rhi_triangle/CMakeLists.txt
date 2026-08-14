cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/learn/02_triangle/shaders" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

add_executable(sample_06_rhi_triangle main.cpp)
if(TARGET sample_02_triangle_shaders)
  add_dependencies(sample_06_rhi_triangle sample_02_triangle_shaders)
endif()
target_link_libraries(sample_06_rhi_triangle PRIVATE engine_rhi engine_d3d12 engine_platform engine_core)
if(TARGET engine_vulkan)
  target_link_libraries(sample_06_rhi_triangle PRIVATE engine_vulkan)
endif()
target_compile_features(sample_06_rhi_triangle PRIVATE cxx_std_20)
target_compile_definitions(sample_06_rhi_triangle PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
)
if(MSVC)
  target_compile_options(sample_06_rhi_triangle PRIVATE /W4)
endif()
