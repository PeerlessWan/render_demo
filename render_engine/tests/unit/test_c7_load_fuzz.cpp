#include "mini_test.h"

#include "engine/assets/asset_system.h"
#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"
#include "engine/assets/manifest.h"
#include "engine/assets/path_resolver.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

TEST_CASE("C7 LoadGltfMeshFile missing path fails", "[assets][c7]") {
  auto loader = engine::assets::CreateDefaultImageLoader();
  REQUIRE(loader);
  const auto missing = std::filesystem::temp_directory_path() / "render_engine_no_such_mesh.glb";
  std::error_code ec;
  std::filesystem::remove(missing, ec);
  auto mesh = engine::assets::LoadGltfMeshFile(missing, *loader);
  REQUIRE_FALSE(mesh);
}

TEST_CASE("C7 LoadGltfMeshFile truncated glb fails", "[assets][c7]") {
  auto loader = engine::assets::CreateDefaultImageLoader();
  REQUIRE(loader);
  const auto bad = std::filesystem::temp_directory_path() / "render_engine_bad.glb";
  {
    std::ofstream out(bad, std::ios::binary);
    out << "glTF" << std::string(12, '\0') << "not-a-valid-chunk";
  }
  auto mesh = engine::assets::LoadGltfMeshFile(bad, *loader);
  REQUIRE_FALSE(mesh);
  std::error_code ec;
  std::filesystem::remove(bad, ec);
}

TEST_CASE("C7 AssetSystem rejects path escape via manifest", "[assets][c7]") {
  engine::assets::AssetSystem assets;
  assets.AddRoot(std::filesystem::temp_directory_path());
  engine::assets::Manifest man;
  engine::assets::ManifestEntry e;
  e.id = engine::assets::AssetId("evil");
  e.path = "../secret.bin";
  e.type = "raw";
  REQUIRE(man.Add(std::move(e)));
  REQUIRE(assets.SetManifest(std::move(man)));
  bool called = false;
  engine::Status cb_status = engine::Status::Ok();
  assets.RequestLoad(engine::assets::AssetId("evil"),
                     [&](engine::Status st, engine::assets::AssetHandle) {
                       called = true;
                       cb_status = st;
                     });
  for (int i = 0; i < 100 && !called; ++i) {
    assets.PumpAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(called);
  REQUIRE_FALSE(cb_status);
}

TEST_CASE("C7 is_safe_relative rejects nested escape", "[assets][c7]") {
  REQUIRE_FALSE(engine::assets::is_safe_relative("a/../../b"));
  REQUIRE_FALSE(engine::assets::is_safe_relative("..\\windows\\system32"));
  REQUIRE(engine::assets::is_safe_relative("models/DamagedHelmet.glb"));
}
