#include "mini_test.h"

#include "engine/assets/asset_handle.h"
#include "engine/assets/asset_id.h"
#include "engine/assets/asset_system.h"
#include "engine/assets/image_loader.h"
#include "engine/assets/manifest.h"
#include "engine/assets/texture_loader.h"
#include "engine/render/frame_graph.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::filesystem::path MakeTempRoot(const char* name) {
  auto root = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  return root;
}

void WriteFile(const std::filesystem::path& path, std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << contents;
}

}  // namespace

TEST_CASE("AssetHandle refcount copy and move", "[assets]") {
  auto record = std::make_shared<engine::assets::AssetRecord>();
  record->id = engine::assets::AssetId("tex.albedo");
  {
    engine::assets::AssetHandle a(record);
    REQUIRE(a.refcount() == 1);
    {
      engine::assets::AssetHandle b = a;
      REQUIRE(a.refcount() == 2);
      REQUIRE(b.refcount() == 2);
    }
    REQUIRE(a.refcount() == 1);
    engine::assets::AssetHandle c = std::move(a);
    REQUIRE(c.refcount() == 1);
    REQUIRE_FALSE(a.valid());
  }
  REQUIRE(record->refcount == 0);
}

TEST_CASE("Manifest loads entries and rejects missing dep file", "[assets]") {
  const auto root = MakeTempRoot("render_engine_manifest_test");
  WriteFile(root / "meshes" / "box.bin", "mesh");
  // texture missing on purpose for dep diagnostic via AssetSystem

  const auto manifest_path = root / "manifest.json";
  WriteFile(manifest_path, R"({
  "assets": [
    {"id":"mesh.box","type":"mesh","path":"meshes/box.bin","deps":["tex.missing"]},
    {"id":"tex.missing","type":"texture","path":"textures/missing.png","deps":[]}
  ]
})");

  auto loaded = engine::assets::Manifest::LoadFromFile(manifest_path);
  REQUIRE(loaded);
  REQUIRE(loaded->Find(engine::assets::AssetId("mesh.box")) != nullptr);
  REQUIRE(loaded->Find(engine::assets::AssetId("tex.missing")) != nullptr);

  engine::assets::AssetSystem system;
  system.AddRoot(root);
  REQUIRE(system.SetManifest(std::move(loaded.value())));

  bool called = false;
  engine::Status cb_status = engine::Status::Ok();
  auto handle = system.RequestLoad(engine::assets::AssetId("mesh.box"),
                                   [&](engine::Status st, engine::assets::AssetHandle) {
                                     called = true;
                                     cb_status = st;
                                   });

  // Callback must not run before PumpAsync.
  for (int i = 0; i < 50 && handle.state() == engine::assets::AssetState::Pending; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE_FALSE(called);

  for (int i = 0; i < 100 && !called; ++i) {
    system.PumpAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(called);
  REQUIRE_FALSE(cb_status);
  REQUIRE(handle.state() == engine::assets::AssetState::Failed);

  std::filesystem::remove_all(root);
}

TEST_CASE("AssetSystem PumpAsync delivers ready bytes", "[assets]") {
  const auto root = MakeTempRoot("render_engine_asset_ready_test");
  WriteFile(root / "data" / "a.bin", "ABCD");
  WriteFile(root / "manifest.json", R"({
  "assets": [
    {"id":"data.a","type":"raw","path":"data/a.bin","deps":[]}
  ]
})");

  auto manifest = engine::assets::Manifest::LoadFromFile(root / "manifest.json");
  REQUIRE(manifest);

  engine::assets::AssetSystem system;
  system.AddRoot(root);
  REQUIRE(system.SetManifest(std::move(manifest.value())));

  bool called = false;
  auto handle = system.RequestLoad(engine::assets::AssetId("data.a"),
                                   [&](engine::Status st, engine::assets::AssetHandle h) {
                                     called = true;
                                     REQUIRE(st);
                                     REQUIRE(h.is_ready());
                                     REQUIRE(h.bytes().size() == 4);
                                     REQUIRE(h.bytes()[0] == 'A');
                                   });

  REQUIRE_FALSE(called);
  for (int i = 0; i < 100 && !called; ++i) {
    system.PumpAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(called);
  REQUIRE(handle.is_ready());

  std::filesystem::remove_all(root);
}

TEST_CASE("AssetSystem Cancel before completion", "[assets]") {
  const auto root = MakeTempRoot("render_engine_asset_cancel_test");
  WriteFile(root / "data" / "a.bin", "ABCD");
  WriteFile(root / "manifest.json", R"({
  "assets": [
    {"id":"data.a","type":"raw","path":"data/a.bin","deps":[]}
  ]
})");

  auto manifest = engine::assets::Manifest::LoadFromFile(root / "manifest.json");
  REQUIRE(manifest);

  engine::assets::AssetSystem system;
  system.AddRoot(root);
  REQUIRE(system.SetManifest(std::move(manifest.value())));

  bool called = false;
  engine::Status cb_status = engine::Status::Ok();
  auto handle = system.RequestLoad(engine::assets::AssetId("data.a"),
                                   [&](engine::Status st, engine::assets::AssetHandle) {
                                     called = true;
                                     cb_status = st;
                                   });
  system.Cancel(handle);

  for (int i = 0; i < 100 && !called; ++i) {
    system.PumpAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  REQUIRE(called);
  REQUIRE_FALSE(cb_status);
  REQUIRE(handle.state() == engine::assets::AssetState::Cancelled);

  std::filesystem::remove_all(root);
}

TEST_CASE("FrameGraph topo-sorts writer before reader", "[render]") {
  engine::render::FrameGraph fg;
  std::vector<std::string> order;

  fg.AddPass("shadow", {}, {"ShadowMap"}, [&] { order.push_back("shadow"); });
  fg.AddPass("opaque", {"ShadowMap"}, {"Color"}, [&] { order.push_back("opaque"); });
  fg.AddPass("ui", {"Color"}, {"Color"}, [&] { order.push_back("ui"); });

  REQUIRE(fg.Compile());
  REQUIRE(fg.Execute());
  REQUIRE(order.size() == 3);
  REQUIRE(order[0] == "shadow");
  REQUIRE(order[1] == "opaque");
  REQUIRE(order[2] == "ui");
}

TEST_CASE("FrameGraph detects cycle", "[render]") {
  engine::render::FrameGraph fg;
  const auto a = fg.AddPass("a", {}, {"A"}, [] {});
  const auto b = fg.AddPass("b", {}, {"B"}, [] {});
  REQUIRE(fg.AddDependency(a, b));
  REQUIRE(fg.AddDependency(b, a));
  REQUIRE_FALSE(fg.Compile());
}

TEST_CASE("IImageLoader decodes PNG albedo", "[assets][image]") {
  auto loader = engine::assets::CreateDefaultImageLoader();
#if defined(ENGINE_WITH_STB_IMAGE) && ENGINE_WITH_STB_IMAGE
  REQUIRE(std::string(loader->backend_name()) == "stb_image");
#ifndef ENGINE_CONTENT_DIR_A
#error "ENGINE_CONTENT_DIR_A must be set by CMake"
#endif
  const auto path =
      std::filesystem::path(ENGINE_CONTENT_DIR_A) / "textures" / "albedo_brick.png";
  auto img = loader->LoadFile(path);
  REQUIRE(img);
  REQUIRE(img->width == 64);
  REQUIRE(img->height == 64);
  REQUIRE(img->rgba.size() == 64u * 64u * 4u);
  // Warm brick pixel should be reddish.
  REQUIRE(img->rgba[0] > img->rgba[2]);
#else
  REQUIRE(std::string(loader->backend_name()) == "null");
  REQUIRE_FALSE(loader->LoadMemory(std::span<const std::uint8_t>{}));
#endif
}

namespace {

void WriteLe32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xffu));
}

// Minimal 4x4 uncompressed RGBA8 DDS (no mipmaps).
std::vector<std::uint8_t> MakeTinyRgba8Dds() {
  constexpr int kW = 4;
  constexpr int kH = 4;
  std::vector<std::uint8_t> dds;
  dds.reserve(128 + kW * kH * 4);
  dds.push_back('D');
  dds.push_back('D');
  dds.push_back('S');
  dds.push_back(' ');
  WriteLe32(dds, 124);                                  // dwSize
  WriteLe32(dds, 0x1u | 0x2u | 0x4u | 0x8u | 0x1000u);  // caps|height|width|pitch|pixelformat
  WriteLe32(dds, static_cast<std::uint32_t>(kH));
  WriteLe32(dds, static_cast<std::uint32_t>(kW));
  WriteLe32(dds, static_cast<std::uint32_t>(kW * 4));  // pitch
  WriteLe32(dds, 0);                                  // depth
  WriteLe32(dds, 1);                                  // mipmap count
  for (int i = 0; i < 11; ++i) {
    WriteLe32(dds, 0);
  }
  // DDS_PIXELFORMAT
  WriteLe32(dds, 32);
  WriteLe32(dds, 0x41);  // DDPF_RGB | DDPF_ALPHAPIXELS
  WriteLe32(dds, 0);     // fourCC
  WriteLe32(dds, 32);    // RGB bit count
  WriteLe32(dds, 0x000000ffu);
  WriteLe32(dds, 0x0000ff00u);
  WriteLe32(dds, 0x00ff0000u);
  WriteLe32(dds, 0xff000000u);
  WriteLe32(dds, 0x1000);  // DDSCAPS_TEXTURE
  WriteLe32(dds, 0);
  WriteLe32(dds, 0);
  WriteLe32(dds, 0);
  WriteLe32(dds, 0);
  for (int i = 0; i < kW * kH; ++i) {
    dds.push_back(0xff);  // R
    dds.push_back(0x40);  // G
    dds.push_back(0x20);  // B
    dds.push_back(0xff);  // A
  }
  return dds;
}

}  // namespace

TEST_CASE("ITextureLoader decodes tiny RGBA8 DDS", "[assets][texture][dds]") {
  auto loader = engine::assets::CreateDefaultTextureLoader();
  REQUIRE(loader);
  const auto bytes = MakeTinyRgba8Dds();
#if defined(ENGINE_WITH_DDSKTX) && ENGINE_WITH_DDSKTX
  auto tex = loader->LoadMemory(bytes);
  REQUIRE(tex);
  REQUIRE(tex->w == 4);
  REQUIRE(tex->h == 4);
  REQUIRE(tex->format == engine::assets::TextureFormat::Rgba8);
  REQUIRE(tex->bytes.size() == 4u * 4u * 4u);
  REQUIRE(tex->bytes[0] == 0xff);
  REQUIRE(tex->bytes[3] == 0xff);
#else
  REQUIRE_FALSE(loader->LoadMemory(bytes));
#endif
}
