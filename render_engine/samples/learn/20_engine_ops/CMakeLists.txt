cmake_minimum_required(VERSION 3.24)

add_executable(sample_20_engine_ops main.cpp)
target_link_libraries(sample_20_engine_ops PRIVATE engine_assets engine_debug engine_core)
target_compile_features(sample_20_engine_ops PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_20_engine_ops PRIVATE /W4)
endif()
