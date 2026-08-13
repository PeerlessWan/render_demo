# Find DXC and expose engine_compile_hlsl()

function(engine_find_dxc)
  if(DXC_EXECUTABLE)
    return()
  endif()

  find_program(DXC_EXECUTABLE
    NAMES dxc dxc.exe
    HINTS
      "$ENV{WindowsSdkVerBinPath}x64"
      "$ENV{WindowsSdkVerBinPath}/x64"
      "$ENV{VULKAN_SDK}/Bin"
    PATHS
      "C:/Program Files (x86)/Windows Kits/10/bin"
    PATH_SUFFIXES
      x64
      10.0.26100.0/x64
      10.0.22621.0/x64
      10.0.19041.0/x64
  )

  if(NOT DXC_EXECUTABLE)
    message(FATAL_ERROR
      "dxc.exe not found. Install Windows SDK (with DXC) or set -DDXC_EXECUTABLE=...")
  endif()
  message(STATUS "DXC: ${DXC_EXECUTABLE}")
endfunction()

# engine_compile_hlsl(
#   TARGET <name>
#   SOURCE <file.hlsl>
#   OUTPUT_DIR <dir>
#   VS_ENTRY <name> VS_OUT <name.vs.cso>
#   PS_ENTRY <name> PS_OUT <name.ps.cso>
# )
function(engine_compile_hlsl)
  cmake_parse_arguments(ARG "" "TARGET;SOURCE;OUTPUT_DIR;VS_ENTRY;VS_OUT;PS_ENTRY;PS_OUT" "" ${ARGN})
  engine_find_dxc()

  set(_src "${ARG_SOURCE}")
  if(NOT IS_ABSOLUTE "${_src}")
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
  endif()
  file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

  set(_vs "${ARG_OUTPUT_DIR}/${ARG_VS_OUT}")
  set(_ps "${ARG_OUTPUT_DIR}/${ARG_PS_OUT}")

  add_custom_command(
    OUTPUT "${_vs}" "${_ps}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ${ARG_VS_ENTRY} -Fo "${_vs}" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E ${ARG_PS_ENTRY} -Fo "${_ps}" "${_src}"
    DEPENDS "${_src}"
    COMMENT "DXC compile ${_src}"
    VERBATIM
  )

  add_custom_target(${ARG_TARGET} DEPENDS "${_vs}" "${_ps}")
  set_property(TARGET ${ARG_TARGET} PROPERTY ENGINE_SHADER_VS "${_vs}")
  set_property(TARGET ${ARG_TARGET} PROPERTY ENGINE_SHADER_PS "${_ps}")
endfunction()

# engine_compile_hlsl_spirv(
#   TARGET <name>
#   SOURCE <file.hlsl>
#   OUTPUT_DIR <dir>
#   VS_ENTRY <name> VS_OUT <name.vs.spv>
#   PS_ENTRY <name> PS_OUT <name.ps.spv>
# )
# DXC HLSL → SPIR-V (Vulkan 1.1). Prefer Vulkan SDK dxc when available.
function(engine_compile_hlsl_spirv)
  cmake_parse_arguments(ARG "" "TARGET;SOURCE;OUTPUT_DIR;VS_ENTRY;VS_OUT;PS_ENTRY;PS_OUT" "" ${ARGN})
  engine_find_dxc()

  set(_src "${ARG_SOURCE}")
  if(NOT IS_ABSOLUTE "${_src}")
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
  endif()
  file(MAKE_DIRECTORY "${ARG_OUTPUT_DIR}")

  set(_vs "${ARG_OUTPUT_DIR}/${ARG_VS_OUT}")
  set(_ps "${ARG_OUTPUT_DIR}/${ARG_PS_OUT}")

  # Quote -fspv-target-env so generators do not truncate "vulkan1.1".
  add_custom_command(
    OUTPUT "${_vs}" "${_ps}"
    COMMAND "${DXC_EXECUTABLE}" -T vs_6_0 -E ${ARG_VS_ENTRY} -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_vs}" "${_src}"
    COMMAND "${DXC_EXECUTABLE}" -T ps_6_0 -E ${ARG_PS_ENTRY} -spirv -fspv-target-env=vulkan1.1
            -fvk-use-dx-layout -Fo "${_ps}" "${_src}"
    DEPENDS "${_src}"
    COMMENT "DXC SPIR-V compile ${_src}"
    VERBATIM
  )

  add_custom_target(${ARG_TARGET} DEPENDS "${_vs}" "${_ps}")
  set_property(TARGET ${ARG_TARGET} PROPERTY ENGINE_SHADER_VS_SPV "${_vs}")
  set_property(TARGET ${ARG_TARGET} PROPERTY ENGINE_SHADER_PS_SPV "${_ps}")
endfunction()
