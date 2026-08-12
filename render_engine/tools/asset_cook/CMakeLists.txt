cmake_minimum_required(VERSION 3.24)
add_executable(asset_cook main.cpp)
target_link_libraries(asset_cook PRIVATE engine_assets)
target_compile_features(asset_cook PRIVATE cxx_std_20)
