cmake_minimum_required(VERSION 3.24)

add_executable(sample_18_upscale main.cpp)
target_link_libraries(sample_18_upscale PRIVATE engine_media engine_core)
target_compile_features(sample_18_upscale PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_18_upscale PRIVATE /W4)
endif()
