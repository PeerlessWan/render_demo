cmake_minimum_required(VERSION 3.24)

add_executable(sample_19_dxr_intro main.cpp)
target_link_libraries(sample_19_dxr_intro PRIVATE engine_rt engine_core)
target_compile_features(sample_19_dxr_intro PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_19_dxr_intro PRIVATE /W4)
endif()
