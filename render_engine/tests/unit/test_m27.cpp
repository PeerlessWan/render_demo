#include "mini_test.h"

#include "engine/animation/skeleton.h"
#include "engine/animation/state_machine.h"
#include "engine/assets/shader_hot_reload.h"
#include "engine/core/feature.h"
#include "engine/gi/probe_volume.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/terrain/heightmap.h"

#include <cmath>
#include <filesystem>
#include <fstream>

TEST_CASE("ProbeVolume RefineDensity densifies grid", "[m27][w6][gi]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {2, 2, 2}, 3, 3, 3);
  REQUIRE(vol.probes().size() == 27);
  vol.RefineDensity(2);
  REQUIRE(vol.grid_nx() == 5);
  REQUIRE(vol.grid_ny() == 5);
  REQUIRE(vol.grid_nz() == 5);
  REQUIRE(vol.probes().size() == 125);
}

TEST_CASE("AnimateWaterPatch displaces heights", "[m27][w6][c09]") {
  auto mesh = engine::terrain::BuildAnimatedWaterPatchMesh(4.f, 4, 0.5f, 0.2f);
  REQUIRE(mesh.positions.size() >= 3 * 25);
  float max_abs_y = 0.f;
  for (std::size_t i = 0; i < mesh.positions.size() / 3; ++i) {
    max_abs_y = (std::max)(max_abs_y, std::fabs(mesh.positions[i * 3 + 1]));
  }
  REQUIRE(max_abs_y > 0.05f);
  REQUIRE(mesh.normals.size() == mesh.positions.size());
}

TEST_CASE("AnimationStateMachine SampleBlend mixes clips", "[m27][w6][c10]") {
  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});

  engine::animation::AnimationClip idle;
  idle.name = "idle";
  idle.duration = 1.f;
  idle.tracks.resize(1);
  idle.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  idle.tracks[0].push_back({1.f, engine::Quat::Identity(), {0, 2, 0}});

  engine::animation::AnimationClip run;
  run.name = "run";
  run.duration = 1.f;
  run.tracks.resize(1);
  run.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  run.tracks[0].push_back({1.f, engine::Quat::Identity(), {2, 0, 0}});

  engine::animation::AnimationStateMachine sm;
  sm.AddState({"idle", idle, true});
  sm.AddState({"run", run, true});

  engine::animation::BlendLayer layers[2] = {{"idle", 0.5f, 1.f}, {"run", 0.5f, 1.f}};
  const auto pose = sm.SampleBlend(skel, layers);
  REQUIRE(pose.bone_matrices.size() == 1);
  REQUIRE(std::fabs(pose.bone_matrices[0].m[12] - 1.f) < 0.05f);
  REQUIRE(std::fabs(pose.bone_matrices[0].m[13] - 1.f) < 0.05f);
}

TEST_CASE("GpuSkinningAvailable Feature gated + stub skins", "[m27][w6][c12]") {
  engine::ClearFeatureOverrides();
  REQUIRE_FALSE(engine::animation::GpuSkinningAvailable());
  engine::SetFeatureOverride("gpu_skinning", true);
  REQUIRE(engine::animation::GpuSkinningAvailable());

  engine::animation::SkinPose pose;
  pose.bone_matrices.push_back(engine::Mat4::Translation({1, 0, 0}));
  std::vector<engine::Vec3> bind{{0, 0, 0}};
  std::vector<int> bones{0, 0, 0, 0};
  std::vector<float> weights{1.f, 0.f, 0.f, 0.f};
  std::vector<engine::Vec3> out;
  engine::animation::SkinVerticesGpuDispatchStub(bind, pose, bones, weights, out);
  REQUIRE(out.size() == 1);
  REQUIRE(std::fabs(out[0].x - 1.f) < 1e-4f);

  std::vector<engine::Vec3> out_ref;
  REQUIRE(engine::animation::SkinVerticesComputeCpuReference(bind, pose, bones, weights, out_ref));
  REQUIRE(out_ref.size() == 1);
  REQUIRE(std::fabs(out_ref[0].x - 1.f) < 1e-4f);

  // Dispatch may use D3D12 CS when skin_cs.cso exists; otherwise CPU fallback — still Ok.
  std::vector<engine::Vec3> out_dispatch;
  engine::animation::SkinVerticesGpuDispatch(bind, pose, bones, weights, out_dispatch);
  REQUIRE(out_dispatch.size() == 1);
  REQUIRE(std::fabs(out_dispatch[0].x - 1.f) < 1e-4f);
  engine::ClearFeatureOverrides();
}

TEST_CASE("MeshletPathAvailable Feature gated", "[m27][w6][c08]") {
  engine::ClearFeatureOverrides();
  REQUIRE_FALSE(engine::gpu_driven::MeshletPathAvailable());
  engine::SetFeatureOverride("meshlet", true);
  REQUIRE(engine::gpu_driven::MeshletPathAvailable());
  engine::ClearFeatureOverrides();
}

TEST_CASE("ShaderHotReload sets PSO rebuild request", "[m27][w6][c16]") {
  const auto dir = std::filesystem::temp_directory_path() / "engine_m27_shader_hot";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  {
    std::ofstream out(dir / "a.hlsl");
    out << "// v1\n";
  }
  engine::assets::ShaderHotReload hot;
  hot.SetShaderDir(dir);
  REQUIRE_FALSE(hot.Poll());
  REQUIRE_FALSE(hot.NeedsPsoRebuild());
  {
    std::ofstream out(dir / "b.cso");
    out << "cso";
  }
  REQUIRE(hot.Poll());
  REQUIRE(hot.NeedsPsoRebuild());
  REQUIRE(hot.ConsumePsoRebuildRequest());
  REQUIRE_FALSE(hot.NeedsPsoRebuild());
  std::filesystem::remove_all(dir, ec);
}
