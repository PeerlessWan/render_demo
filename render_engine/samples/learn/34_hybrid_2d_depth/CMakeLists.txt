cmake_minimum_required(VERSION 3.24)

add_executable(sample_34_hybrid_2d_depth main.cpp)
target_link_libraries(sample_34_hybrid_2d_depth PRIVATE engine_mixed engine_render2d engine_core)
target_compile_features(sample_34_hybrid_2d_depth PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_34_hybrid_2d_depth PRIVATE /W4)
endif()
