# Point engine CACHE paths at render_engine/ when the workspace root is CMAKE_SOURCE_DIR.

set(RENDER_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/render_engine" CACHE PATH "render_engine root")

set(ENGINE_IMGUI_DIR "${RENDER_ENGINE_ROOT}/third_party/imgui-v1.91.8" CACHE PATH "Dear ImGui root")
set(ENGINE_STB_DIR "${RENDER_ENGINE_ROOT}/third_party/stb" CACHE PATH "stb headers directory")
set(ENGINE_HTTPLIB_DIR "${RENDER_ENGINE_ROOT}/third_party/cpp-httplib" CACHE PATH "cpp-httplib directory")
set(ENGINE_RMLUI_DIR "${RENDER_ENGINE_ROOT}/third_party/RmlUi" CACHE PATH "RmlUi root")
set(ENGINE_DDSKTX_DIR "${RENDER_ENGINE_ROOT}/third_party/dds-ktx" CACHE PATH "dds-ktx directory")
set(ENGINE_CGLTF_DIR "${RENDER_ENGINE_ROOT}/third_party/cgltf" CACHE PATH "cgltf directory")
set(ENGINE_JOLT_DIR "${RENDER_ENGINE_ROOT}/third_party/JoltPhysics" CACHE PATH "Jolt Physics root")

set(ENGINE_BUILD_SAMPLES OFF CACHE BOOL "Build engine samples")
set(ENGINE_BUILD_LEARN_SAMPLES OFF CACHE BOOL "Build learn samples")
set(ENGINE_BUILD_TESTS OFF CACHE BOOL "Build engine unit tests")
set(ENGINE_BUILD_TOOLS OFF CACHE BOOL "Build engine offline tools")
