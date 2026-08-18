#include "engine/hlod/billboard_impostor.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace engine::hlod {

LodMode BillboardImpostor::SwitchLod(float distance) const {
  if (distance < distance_threshold) {
    return LodMode::NearMesh;
  }
  return LodMode::Impostor;
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

}  // namespace engine::hlod
