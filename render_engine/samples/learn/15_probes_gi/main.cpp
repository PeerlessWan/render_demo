#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/gi/lightmap.h"
#include "engine/gi/probe_volume.h"
#include "engine/gi/reflection_probe.h"
#include "engine/material/material.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif
#ifndef ENGINE_CONTENT_DIR_A
#error "ENGINE_CONTENT_DIR_A must be set by CMake"
#endif

namespace {

void ParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc) {
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
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    }
  }
}

engine::render::RenderSystemDesc LitDesc() {
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
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 15 — Probes + GI";
  ParseHeadless(argc, argv, desc);
  if (desc.headless_frames <= 0) {
    desc.headless_frames = 2;
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  auto cube = a.world().CreateNode("cube");
  {
    engine::scene::Transform t;
    t.position = {0.f, 0.5f, 0.f};
    a.world().set_local_transform(cube, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(cube, mesh);
  }

  engine::gi::ProbeVolume probes;
  probes.Configure({0.f, 0.f, 0.f}, {2.f, 2.f, 2.f}, 2, 2, 2);
  engine::gi::ProbeLight light;
  light.position = {0.5f, 0.5f, 0.5f};
  light.color = {1.f, 0.4f, 0.2f, 1.f};
  light.intensity = 6.f;
  light.range = 4.f;
  probes.UpdateFromLights({&light, 1});
  const auto gi = probes.Sample({0.4f, 0.4f, 0.4f});
  engine::LogInfo("Probe GI sample r=" + std::to_string(gi.r));

  engine::gi::ReflectionProbe reflection;
  reflection.Configure({0.f, 1.f, 0.f}, 32);
  reflection.UpdateFromEnvironment({0.3f, -1.f, 0.2f}, {1.f, 0.95f, 0.9f, 1.f}, 2.f,
                                   {0.1f, 0.12f, 0.15f, 1.f});
  (void)a.device().UploadReflectionCubemap(reflection.rgba_faces().data(), reflection.face_size());

  const auto lightmap_path =
      std::filesystem::path(ENGINE_CONTENT_DIR_A) / "ibl" / "lightmap.rgba";
  engine::gi::LightmapImage lightmap;
  engine::material::PbrMaterial lightmap_mat;
  lightmap_mat.use_lightmap = true;
  lightmap_mat.albedo_tex = "ibl/lightmap.rgba";
  if (auto st = engine::gi::LoadLightmapRgba(lightmap_path, lightmap); !st) {
    engine::LogError(st.message());
    return 1;
  }
  const auto lm_center = engine::gi::SampleLightmap(lightmap, 0.5f, 0.5f);
  engine::LogInfo("Lightmap " + std::to_string(lightmap.width) + "x" +
                  std::to_string(lightmap.height) + " center r=" + std::to_string(lm_center.r) +
                  " use_lightmap=" + (lightmap_mat.use_lightmap ? "true" : "false"));

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  // Minimal lit path: flat albedo × lightmap bake, then UploadLitAlbedoRgba.
  std::vector<std::uint8_t> albedo(
      static_cast<std::size_t>(lightmap.width * lightmap.height * 4), 200);
  for (std::size_t i = 3; i < albedo.size(); i += 4) {
    albedo[i] = 255;
  }
  engine::gi::MultiplyAlbedoByLightmap(albedo, lightmap.width, lightmap.height, lightmap);
  if (auto st = a.device().UploadLitAlbedoRgba(albedo.data(), lightmap.width, lightmap.height, 0);
      !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
