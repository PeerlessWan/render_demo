#include "engine/hlod/billboard_impostor.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <system_error>

namespace engine::hlod {

LodMode BillboardImpostor::SwitchLod(float distance) {
  // W20: enter/exit hysteresis. Not multi-view — single billboard axis only.
  const float enter = distance_threshold;
  float exit = exit_distance;
  if (exit <= 0.f || exit >= enter) {
    exit = enter * 0.85f;
  }
  if (current_mode_ == LodMode::NearMesh) {
    if (distance >= enter) {
      current_mode_ = LodMode::Impostor;
    }
  } else {
    if (distance < exit) {
      current_mode_ = LodMode::NearMesh;
    }
  }
  return current_mode_;
}

std::string BakeImpostorPlaceholder(const ColorRgba& color) {
  // Deterministic id from quantized RGBA (8-bit) for test equality.
  char buf[96];
  const int r = static_cast<int>(std::clamp(color.r, 0.f, 1.f) * 255.f + 0.5f);
  const int g = static_cast<int>(std::clamp(color.g, 0.f, 1.f) * 255.f + 0.5f);
  const int b = static_cast<int>(std::clamp(color.b, 0.f, 1.f) * 255.f + 0.5f);
  const int a = static_cast<int>(std::clamp(color.a, 0.f, 1.f) * 255.f + 0.5f);
  std::snprintf(buf, sizeof(buf), "impostor/solid_%02x%02x%02x%02x", r, g, b, a);
  return std::string(buf);
}

Status SerializeBakeToFile(const BillboardImpostor& impostor, const ColorRgba& bake_color,
                           const std::filesystem::path& path) {
  if (path.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "SerializeBakeToFile: empty path");
  }
  const std::string bake_id = BakeImpostorPlaceholder(bake_color);
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "SerializeBakeToFile: open failed");
  }
  out << "hlod_impostor_bake v1\n";
  out << "near_mesh=" << impostor.near_mesh_id << "\n";
  out << "distance_threshold=" << impostor.distance_threshold << "\n";
  out << "impostor_tex=" << (impostor.impostor_tex_id.empty() ? bake_id : impostor.impostor_tex_id)
      << "\n";
  out << "bake_id=" << bake_id << "\n";
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "SerializeBakeToFile: write failed");
  }
  return Status::Ok("hlod-bake-file");
}

Status WriteSolidRgba8Bake(const ColorRgba& color, const std::filesystem::path& path) {
  if (path.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "WriteSolidRgba8Bake: empty path");
  }
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteSolidRgba8Bake: open failed");
  }
  const auto q = [](float c) -> unsigned char {
    return static_cast<unsigned char>(std::clamp(c, 0.f, 1.f) * 255.f + 0.5f);
  };
  const unsigned char px[4] = {q(color.r), q(color.g), q(color.b), q(color.a)};
  // 2×2 solid block (16 bytes) for UploadLitAlbedoRgba-style hosts.
  for (int i = 0; i < 4; ++i) {
    out.write(reinterpret_cast<const char*>(px), 4);
  }
  if (!out) {
    return Status::Fail(ErrorCode::Failed, "WriteSolidRgba8Bake: write failed");
  }
  return Status::Ok("hlod-rgba8-bake");
}

Status LoadBakeFromFile(BillboardImpostor& out, const std::filesystem::path& path) {
  if (path.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "LoadBakeFromFile: empty path");
  }
  std::ifstream in(path);
  if (!in) {
    return Status::Fail(ErrorCode::NotFound, "LoadBakeFromFile: open failed");
  }
  std::string line;
  if (!std::getline(in, line) || line.find("hlod_impostor_bake") == std::string::npos) {
    return Status::Fail(ErrorCode::Failed, "LoadBakeFromFile: bad header");
  }
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);
    if (key == "near_mesh") {
      out.near_mesh_id = val;
    } else if (key == "distance_threshold") {
      out.distance_threshold = std::strtof(val.c_str(), nullptr);
    } else if (key == "impostor_tex" || key == "bake_id") {
      out.impostor_tex_id = val;
    }
  }
  return Status::Ok("hlod-bake-load");
}

}  // namespace engine::hlod
