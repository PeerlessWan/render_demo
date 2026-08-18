cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

add_executable(sample_39_w10_deepen main.cpp)
target_link_libraries(sample_39_w10_deepen PRIVATE
  engine_clothing
  engine_gameplay
  engine_terrain
  engine_assets
  engine_physics
  engine_core
)
target_compile_features(sample_39_w10_deepen PRIVATE cxx_std_20)
target_compile_definitions(sample_39_w10_deepen PRIVATE
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
)
if(MSVC)
  target_compile_options(sample_39_w10_deepen PRIVATE /W4)
endif()
