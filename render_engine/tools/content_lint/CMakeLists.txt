cmake_minimum_required(VERSION 3.24)
add_executable(content_lint main.cpp)
target_link_libraries(content_lint PRIVATE engine_assets)
target_compile_features(content_lint PRIVATE cxx_std_20)
