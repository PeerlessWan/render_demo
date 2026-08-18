cmake_minimum_required(VERSION 3.24)

add_executable(sample_14_skinning main.cpp)
target_link_libraries(sample_14_skinning PRIVATE engine_animation engine_core)
target_compile_features(sample_14_skinning PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_14_skinning PRIVATE /W4)
endif()
