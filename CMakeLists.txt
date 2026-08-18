cmake_minimum_required(VERSION 3.24)
project(render_demo_kits LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/utf-8 /permissive- /Zc:__cplusplus)
endif()

enable_testing()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnginePaths.cmake")
list(APPEND CMAKE_MODULE_PATH "${RENDER_ENGINE_ROOT}/cmake")

add_subdirectory(render_engine)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/KitShaders.cmake")
kit_setup_shaders()

add_subdirectory(game_kit)
add_subdirectory(games/minecraft)
add_subdirectory(editor)
