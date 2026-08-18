#include "engine/animation/skeleton.h"
#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/gpu_driven/meshlet.h"
#include "engine/net/quic.h"
#include "engine/render/weather.h"
#include "engine/vfx/particles.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

void ParseHeadless(int argc, char** argv, int& headless_frames) {
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      headless = true;
      if (headless_frames <= 0) {
        headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      headless_frames = std::atoi(argv[++i]);
    }
  }
  if (headless && headless_frames <= 0) {
    headless_frames = 2;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int headless_frames = 0;
  ParseHeadless(argc, argv, headless_frames);

  engine::LogInfo("Learn 36 — Mega-W9 deepen smoke (ADR 0036)");

  // C08 meshlet cook + mesh shader probe
  std::vector<engine::Vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  std::vector<std::uint32_t> indices{0, 1, 2, 0, 2, 3};
  const auto cooked = engine::gpu_driven::MeshletizePreferMeshoptimizer(positions, indices, 2);
  engine::LogInfo("Meshlets=" + std::to_string(cooked.meshlets.size()));
  const auto ms = engine::gpu_driven::TryMeshShaderPath();
  engine::LogInfo(std::string("TryMeshShaderPath=") +
                  (ms ? "Ok" : ("Unavailable: " + ms.message())));
  const auto vk_ms = engine::gpu_driven::ProbeMeshShaderSupportVk();
  engine::LogInfo(std::string("ProbeMeshShaderSupportVk=") +
                  (vk_ms ? "Ok" : ("Unavailable: " + vk_ms.message())));

  // C12 GPU skinning availability
  engine::LogInfo(std::string("GpuSkinningAvailable=") +
                  (engine::animation::GpuSkinningAvailable() ? "true" : "false"));

  // Weather curtain (W8/W9)
  engine::render::WeatherSystem weather;
  weather.SetState(engine::render::WeatherState::Rain, 0.8f);
  weather.Update(1.f / 60.f);
  weather.UpdateCurtain(1.f / 60.f, {0.f, 1.5f, 0.f});
  engine::LogInfo("Weather curtain particles=" + std::to_string(weather.curtain().size()));

  engine::vfx::ParticleEmitter precip;
  weather.ConfigurePrecipEmitter(precip, {0.f, 1.5f, 0.f});
  precip.EmitBurst(8);
  precip.Step(1.f / 60.f);
  engine::LogInfo("Precip emitter particles=" + std::to_string(precip.particles().size()));

  // MsQuic loopback
  (void)engine::net::ProbeAndSetQuicFeature();
  if (auto st = engine::net::TryQuicLoopbackReliableSendRecv(); !st) {
    engine::LogInfo("TryQuicLoopbackReliableSendRecv SKIP/Unavailable: " + st.message());
  } else {
    engine::LogInfo("TryQuicLoopbackReliableSendRecv Ok: " + st.message());
  }

  const auto features = engine::QueryFeatures();
  engine::LogInfo(std::string("Features virtual_texture=") +
                  (features.virtual_texture ? "true" : "false") +
                  " quic=" + (features.quic ? "true" : "false"));

  (void)headless_frames;
  return 0;
}
