cmake_minimum_required(VERSION 3.24)

add_executable(sample_07b_input_actions main.cpp)
target_link_libraries(sample_07b_input_actions PRIVATE engine_app engine_d3d12)
target_compile_features(sample_07b_input_actions PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_07b_input_actions PRIVATE /W4)
endif()
