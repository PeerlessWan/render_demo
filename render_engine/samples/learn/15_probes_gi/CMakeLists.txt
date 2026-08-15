cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

add_executable(sample_15_probes_gi main.cpp)
if(TARGET sample_sandbox_shaders)
  add_dependencies(sample_15_probes_gi sample_sandbox_shaders)
endif()
target_link_libraries(sample_15_probes_gi PRIVATE engine_app engine_d3d12 engine_gi)
target_compile_features(sample_15_probes_gi PRIVATE cxx_std_20)
target_compile_definitions(sample_15_probes_gi PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
)
if(MSVC)
  target_compile_options(sample_15_probes_gi PRIVATE /W4)
endif()
