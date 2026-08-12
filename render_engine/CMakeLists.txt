cmake_minimum_required(VERSION 3.24)
project(render_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EngineOptions)

add_subdirectory(engine)

if(ENGINE_BUILD_SAMPLES)
  add_subdirectory(samples/learn/01_clear)
  add_subdirectory(samples/learn/02_triangle)
  add_subdirectory(samples/Sandbox)
endif()

option(ENGINE_BUILD_TOOLS "Build offline tools" ON)
if(ENGINE_BUILD_TOOLS)
  add_subdirectory(tools/ibl_baker)
  add_subdirectory(tools/asset_cook)
endif()

if(ENGINE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
