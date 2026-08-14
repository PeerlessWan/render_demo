cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _shader_out_def)
string(REPLACE "\\" "/" _shader_out_def "${_shader_out_def}")

add_executable(sample_30_pixel_hybrid main.cpp)
if(TARGET sample_sandbox_shaders)
  add_dependencies(sample_30_pixel_hybrid sample_sandbox_shaders)
endif()
target_link_libraries(sample_30_pixel_hybrid PRIVATE engine_app engine_d3d12 engine_render2d)
target_compile_features(sample_30_pixel_hybrid PRIVATE cxx_std_20)
target_compile_definitions(sample_30_pixel_hybrid PRIVATE
  ENGINE_SHADER_DIR_A="${_shader_out_def}"
)
if(MSVC)
  target_compile_options(sample_30_pixel_hybrid PRIVATE /W4)
endif()
