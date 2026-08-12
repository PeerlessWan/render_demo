cmake_minimum_required(VERSION 3.24)

add_executable(sample_01_clear main.cpp)
target_link_libraries(sample_01_clear PRIVATE engine_app engine_d3d12)
target_compile_features(sample_01_clear PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(sample_01_clear PRIVATE /W4)
endif()
