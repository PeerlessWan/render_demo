cmake_minimum_required(VERSION 3.24)

add_executable(sample_37_clothing main.cpp)
target_link_libraries(sample_37_clothing PRIVATE
  engine_clothing
  engine_physics
  engine_core
)
target_compile_features(sample_37_clothing PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_37_clothing PRIVATE /W4)
endif()
