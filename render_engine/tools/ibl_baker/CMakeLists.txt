cmake_minimum_required(VERSION 3.24)
add_executable(ibl_baker main.cpp)
target_compile_features(ibl_baker PRIVATE cxx_std_20)
