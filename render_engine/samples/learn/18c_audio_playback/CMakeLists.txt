cmake_minimum_required(VERSION 3.24)

add_executable(sample_18c_audio_playback main.cpp)
target_link_libraries(sample_18c_audio_playback PRIVATE engine_media engine_core)
target_compile_features(sample_18c_audio_playback PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_18c_audio_playback PRIVATE /W4)
endif()
