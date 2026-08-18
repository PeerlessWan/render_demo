cmake_minimum_required(VERSION 3.24)

add_executable(sample_36_w9_deepen main.cpp)
target_link_libraries(sample_36_w9_deepen PRIVATE
  engine_gpu_driven
  engine_animation
  engine_rhi
  engine_vfx
  engine_net
  engine_core
)
target_compile_features(sample_36_w9_deepen PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(sample_36_w9_deepen PRIVATE /W4)
endif()
