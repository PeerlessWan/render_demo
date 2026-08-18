cmake_minimum_required(VERSION 3.24)

add_executable(sample_18b_video_texture main.cpp)
target_link_libraries(sample_18b_video_texture PRIVATE engine_media engine_core)
target_compile_features(sample_18b_video_texture PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_18b_video_texture PRIVATE /W4)
endif()
