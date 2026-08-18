cmake_minimum_required(VERSION 3.24)

add_executable(sample_40_possess_third_person main.cpp)
target_link_libraries(sample_40_possess_third_person PRIVATE
  engine_gameplay
  engine_terrain
  engine_core
)
target_compile_features(sample_40_possess_third_person PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_40_possess_third_person PRIVATE /W4)
endif()
