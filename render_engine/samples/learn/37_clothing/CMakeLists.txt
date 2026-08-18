cmake_minimum_required(VERSION 3.24)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

add_executable(sample_37_clothing main.cpp)
target_link_libraries(sample_37_clothing PRIVATE
  engine_clothing
  engine_gameplay
  engine_physics
  engine_assets
  engine_core
)
target_compile_features(sample_37_clothing PRIVATE cxx_std_20)
target_compile_definitions(sample_37_clothing PRIVATE
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
)
if(MSVC)
  target_compile_options(sample_37_clothing PRIVATE /W4)
endif()
