cmake_minimum_required(VERSION 3.24)

cmake_policy(SET CMP0169 OLD)
include(FetchContent)

set(_lua_ok FALSE)
FetchContent_Declare(lua54
  URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)
FetchContent_GetProperties(lua54)
if(NOT lua54_POPULATED)
  FetchContent_Populate(lua54)
endif()
if(EXISTS "${lua54_SOURCE_DIR}/src/lua.h")
  set(_lua_src "${lua54_SOURCE_DIR}/src")
  add_library(lua_static STATIC
    ${_lua_src}/lapi.c ${_lua_src}/lauxlib.c ${_lua_src}/lbaselib.c ${_lua_src}/lcode.c
    ${_lua_src}/lcorolib.c ${_lua_src}/lctype.c ${_lua_src}/ldblib.c ${_lua_src}/ldebug.c
    ${_lua_src}/ldo.c ${_lua_src}/ldump.c ${_lua_src}/lfunc.c ${_lua_src}/lgc.c
    ${_lua_src}/linit.c ${_lua_src}/liolib.c ${_lua_src}/llex.c ${_lua_src}/lmathlib.c
    ${_lua_src}/lmem.c ${_lua_src}/loadlib.c ${_lua_src}/lobject.c ${_lua_src}/lopcodes.c
    ${_lua_src}/loslib.c ${_lua_src}/lparser.c ${_lua_src}/lstate.c ${_lua_src}/lstring.c
    ${_lua_src}/lstrlib.c ${_lua_src}/ltable.c ${_lua_src}/ltablib.c ${_lua_src}/ltm.c
    ${_lua_src}/lundump.c ${_lua_src}/lutf8lib.c ${_lua_src}/lvm.c ${_lua_src}/lzio.c
  )
  target_include_directories(lua_static PRIVATE ${_lua_src})
  set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
  if(MSVC)
    target_compile_options(lua_static PRIVATE /W3)
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  set(_lua_ok TRUE)
  message(STATUS "game_kit: Lua 5.4 from ${lua54_SOURCE_DIR}")
else()
  message(WARNING "game_kit: Lua 5.4 sources missing; ScriptVm stub only")
endif()

set(_recast_ok FALSE)
FetchContent_Declare(recastnav
  URL https://github.com/recastnavigation/recastnavigation/archive/refs/tags/v1.6.0.tar.gz
)
FetchContent_GetProperties(recastnav)
if(NOT recastnav_POPULATED)
  FetchContent_Populate(recastnav)
endif()
if(EXISTS "${recastnav_SOURCE_DIR}/Recast/Include/Recast.h" AND EXISTS "${recastnav_SOURCE_DIR}/Detour/Include/DetourNavMesh.h")
  file(GLOB _recast_src CONFIGURE_DEPENDS "${recastnav_SOURCE_DIR}/Recast/Source/*.cpp")
  file(GLOB _detour_src CONFIGURE_DEPENDS "${recastnav_SOURCE_DIR}/Detour/Source/*.cpp")
  add_library(recast_static STATIC ${_recast_src} ${_detour_src})
  target_include_directories(recast_static PUBLIC
    "${recastnav_SOURCE_DIR}/Recast/Include"
    "${recastnav_SOURCE_DIR}/Detour/Include"
  )
  if(MSVC)
    target_compile_options(recast_static PRIVATE /W3)
    target_compile_definitions(recast_static PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  set(_recast_ok TRUE)
  message(STATUS "game_kit: Recast from ${recastnav_SOURCE_DIR}")
else()
  message(WARNING "game_kit: Recast sources missing; Steer-only nav")
endif()

add_library(game_kit STATIC
  runtime/level_flow.cpp
  runtime/timer.cpp
  runtime/event_bus.cpp
  runtime/entity.cpp
  runtime/save.cpp
  runtime/scene_document.cpp
  runtime/prefab.cpp
  runtime/runtime.cpp
  runtime/trigger.cpp
  runtime/player_controller.cpp
  runtime/anim_player.cpp
  runtime/audio_mixer.cpp
  runtime/nav.cpp
  runtime/timeline.cpp
  runtime/snapshot.cpp
  runtime/script_fields.cpp
  script/vm/lua_vm.cpp
  script/bindings/lua_api.cpp
  script/script_hot_reload.cpp
  script/script_component.cpp
  script/coroutine_scheduler.cpp
  script/script_host.cpp
)
target_include_directories(game_kit PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(game_kit PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/script
)
target_link_libraries(game_kit PUBLIC engine_app engine_scene engine_core engine_physics engine_ui engine_media engine_animation engine_input)
target_compile_features(game_kit PUBLIC cxx_std_20)
if(_lua_ok)
  target_link_libraries(game_kit PUBLIC lua_static)
  target_include_directories(game_kit PRIVATE "${lua54_SOURCE_DIR}/src")
  target_compile_definitions(game_kit PUBLIC GAME_KIT_WITH_LUA=1)
else()
  target_compile_definitions(game_kit PUBLIC GAME_KIT_WITH_LUA=0)
endif()
if(_recast_ok)
  target_link_libraries(game_kit PUBLIC recast_static)
  target_compile_definitions(game_kit PUBLIC GAME_KIT_WITH_RECAST=1)
else()
  target_compile_definitions(game_kit PUBLIC GAME_KIT_WITH_RECAST=0)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/script/bindings/gen_lua_api.py" --check
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    RESULT_VARIABLE _lua_api_check
  )
  if(NOT _lua_api_check EQUAL 0)
    message(WARNING "game_kit: lua_api_reg.inc does not match api.json; run script/bindings/gen_lua_api.py")
  endif()
endif()

add_executable(hello_playable samples/hello_playable/main.cpp)
target_include_directories(hello_playable PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/samples)
target_link_libraries(hello_playable PRIVATE game_kit engine_d3d12)
target_compile_definitions(hello_playable PRIVATE "GAME_KIT_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/samples\"")
kit_use_shaders(hello_playable)

add_executable(third_person samples/third_person/main.cpp)
target_include_directories(third_person PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/samples)
target_link_libraries(third_person PRIVATE game_kit engine_d3d12 engine_physics engine_ui)
target_compile_definitions(third_person PRIVATE "GAME_KIT_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/samples\"")
kit_use_shaders(third_person)

add_executable(prefab_place samples/prefab_place/main.cpp)
target_include_directories(prefab_place PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/samples)
target_link_libraries(prefab_place PRIVATE game_kit engine_d3d12)
target_compile_definitions(prefab_place PRIVATE "GAME_KIT_SAMPLE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/samples\"")
kit_use_shaders(prefab_place)

add_executable(game_kit_tests
  tests/main.cpp
  tests/test_runtime.cpp
  tests/test_gameplay.cpp
  tests/test_integration.cpp
)
target_include_directories(game_kit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(game_kit_tests PRIVATE game_kit)
enable_testing()
add_test(NAME game_kit.unit COMMAND game_kit_tests)

if(MSVC)
  target_compile_options(game_kit PRIVATE /W4)
  target_compile_options(hello_playable PRIVATE /W4)
  target_compile_options(third_person PRIVATE /W4)
  target_compile_options(prefab_place PRIVATE /W4)
  target_compile_options(game_kit_tests PRIVATE /W4)
endif()
