cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

add_executable(sample_11_frame_graph main.cpp)
if(TARGET sample_sandbox_shaders)
  add_dependencies(sample_11_frame_graph sample_sandbox_shaders)
endif()
target_link_libraries(sample_11_frame_graph PRIVATE engine_app engine_d3d12)
target_compile_features(sample_11_frame_graph PRIVATE cxx_std_20)
target_compile_definitions(sample_11_frame_graph PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
)
if(MSVC)
  target_compile_options(sample_11_frame_graph PRIVATE /W4)
endif()
