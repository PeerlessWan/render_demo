cmake_minimum_required(VERSION 3.24)

add_executable(sample_23_occlusion_culling main.cpp)
target_link_libraries(sample_23_occlusion_culling PRIVATE engine_rhi engine_core)
target_compile_features(sample_23_occlusion_culling PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_23_occlusion_culling PRIVATE /W4)
endif()
