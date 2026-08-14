cmake_minimum_required(VERSION 3.24)
add_executable(lightmap_baker main.cpp)
target_compile_features(lightmap_baker PRIVATE cxx_std_20)
