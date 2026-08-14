cmake_minimum_required(VERSION 3.24)

add_executable(sample_25_physics main.cpp)
target_link_libraries(sample_25_physics PRIVATE engine_physics engine_core)
target_compile_features(sample_25_physics PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_25_physics PRIVATE /W4)
endif()
