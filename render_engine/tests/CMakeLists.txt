cmake_minimum_required(VERSION 3.24)

add_executable(engine_unit_tests
  unit/main.cpp
  unit/test_math.cpp
  unit/test_config.cpp
  unit/test_path.cpp
  unit/test_assets_m3.cpp
  unit/test_c7_load_fuzz.cpp
  unit/test_m4_m19.cpp
  unit/test_m20_m25_headless.cpp
  unit/test_m26.cpp
  unit/test_m27.cpp
  unit/test_m28.cpp
  unit/test_m29.cpp
  unit/test_m30.cpp
  unit/test_m31.cpp
  unit/test_m32.cpp
  unit/test_m33.cpp
  unit/test_m34.cpp
  unit/test_m35.cpp
  unit/test_m36.cpp
  unit/test_w20.cpp
  unit/test_w21.cpp
  unit/test_w22.cpp
  unit/test_product_wave_stubs.cpp
  unit/test_headless_integration.cpp
  unit/test_usable.cpp
  unit/test_gpu_headless_regression.cpp
)
target_include_directories(engine_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(engine_unit_tests PRIVATE
  engine_core
  engine_assets
  engine_rhi
  engine_input
  engine_scene
  engine_debug
  engine_animation
  engine_post
  engine_physics
  engine_gameplay
  engine_clothing
  engine_media
  engine_ui
  engine_render2d
  engine_net
  engine_mixed
  engine_gi
  engine_terrain
  engine_ocean
  engine_vfx
  engine_gpu_driven
  engine_vt
  engine_hlod
  engine_rt
  engine_app
)
target_compile_features(engine_unit_tests PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(engine_unit_tests PRIVATE /FS)
endif()

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/content" _content_dir_def)
string(REPLACE "\\" "/" _content_dir_def "${_content_dir_def}")

# Same CSO dir as sample_sandbox (gpu regression loads lit/post shaders).
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}/samples/Sandbox/shaders" _shader_dir_def)
string(REPLACE "\\" "/" _shader_dir_def "${_shader_dir_def}")

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/tests/certs" _certs_dir_def)
string(REPLACE "\\" "/" _certs_dir_def "${_certs_dir_def}")

target_compile_definitions(engine_unit_tests PRIVATE
  ENGINE_CONTENT_DIR_A="${_content_dir_def}"
  ENGINE_SHADER_DIR_A="${_shader_dir_def}"
  ENGINE_TEST_CERTS_DIR_A="${_certs_dir_def}"
)

if(ENGINE_WITH_HTTPLIB)
  target_include_directories(engine_unit_tests PRIVATE "${ENGINE_HTTPLIB_DIR}")
endif()
if(ENGINE_WITH_OPENSSL AND OpenSSL_FOUND)
  target_link_libraries(engine_unit_tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Rebuild shaders before GPU regression when Sandbox target exists.
if(TARGET sample_sandbox_shaders)
  add_dependencies(engine_unit_tests sample_sandbox_shaders)
endif()

add_test(NAME unit.engine_unit_tests COMMAND engine_unit_tests)

# Headless-only filter: same binary, Catch-like tag not available; run full suite as headless-safe.
# Dedicated target alias for CI scripts: `ctest -R headless`
add_test(NAME headless.engine_unit_tests COMMAND engine_unit_tests)
set_tests_properties(headless.engine_unit_tests PROPERTIES LABELS "headless")

add_test(NAME gpu_headless.sandbox
  COMMAND sample_sandbox --gpu-headless --headless_frames=4 --backend=d3d12)
set_tests_properties(gpu_headless.sandbox PROPERTIES LABELS "gpu_headless;headless")

# Windowed swapchain path (auto-close): covers post-then-DrawLitInstanced scale path skipped by gpu-headless.
add_test(NAME windowed.sandbox_scale_path
  COMMAND sample_sandbox --backend=d3d12 --headless_frames=90)
set_tests_properties(windowed.sandbox_scale_path PROPERTIES LABELS "gpu;windowed")

if(ENGINE_GOLDEN_TESTS)
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    add_test(NAME golden.sandbox_gpu_headless
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/tests/scripts/run_golden.py"
        --build-dir "${CMAKE_BINARY_DIR}"
        --config "$<CONFIG>")
    set_tests_properties(golden.sandbox_gpu_headless PROPERTIES
      LABELS "golden;gpu_headless;headless")
  else()
    message(STATUS "ENGINE_GOLDEN_TESTS=ON but Python3 not found — golden CTest skipped")
  endif()
endif()
