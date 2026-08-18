cmake_minimum_required(VERSION 3.24)

add_executable(sample_28_hdr_color_sandbox main.cpp)
target_link_libraries(sample_28_hdr_color_sandbox PRIVATE engine_rhi engine_core)
target_compile_features(sample_28_hdr_color_sandbox PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_28_hdr_color_sandbox PRIVATE /W4)
endif()
