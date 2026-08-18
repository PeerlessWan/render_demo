cmake_minimum_required(VERSION 3.24)

add_executable(sample_31_net_loopback main.cpp)
target_link_libraries(sample_31_net_loopback PRIVATE engine_net engine_core)
target_compile_features(sample_31_net_loopback PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_31_net_loopback PRIVATE /W4)
endif()
