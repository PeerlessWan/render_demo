cmake_minimum_required(VERSION 3.24)

add_executable(sample_17_vfx main.cpp)
target_link_libraries(sample_17_vfx PRIVATE engine_vfx engine_debug engine_core)
target_compile_features(sample_17_vfx PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_17_vfx PRIVATE /W4)
endif()
