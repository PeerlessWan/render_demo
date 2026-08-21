cmake_minimum_required(VERSION 3.24)

add_executable(sample_41_platform_2d main.cpp)
target_link_libraries(sample_41_platform_2d PRIVATE engine_physics engine_render2d engine_core)
target_compile_features(sample_41_platform_2d PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_41_platform_2d PRIVATE /W4)
endif()
