#pragma once

#include "engine/app/application.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

inline void KitParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    }
  }
}

inline engine::render::RenderSystemDesc KitLitDesc() {
  const auto dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::render::RenderSystemDesc r{};
  r.lit_vs = dir / "lit_cube.vs.cso";
  r.lit_ps = dir / "lit_cube.ps.cso";
  r.shadow_vs = dir / "shadow.vs.cso";
  r.shadow_ps = dir / "shadow.ps.cso";
  r.quad_vs = dir / "quad.vs.cso";
  r.quad_ps = dir / "quad.ps.cso";
  r.post_vs = dir / "post_ssao_taa.vs.cso";
  r.post_ps = dir / "post_ssao_taa.ps.cso";
  r.debug_vs = dir / "debug_line.vs.cso";
  r.debug_ps = dir / "debug_line.ps.cso";
  r.sky_vs = dir / "skybox.vs.cso";
  r.sky_ps = dir / "skybox.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

inline std::filesystem::path KitShaderDir() { return std::filesystem::path(ENGINE_SHADER_DIR_A); }
