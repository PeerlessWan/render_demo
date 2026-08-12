option(ENGINE_BUILD_TESTS "Build Catch2 unit tests" ON)
option(ENGINE_BUILD_SAMPLES "Build samples (01_clear, …)" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()
