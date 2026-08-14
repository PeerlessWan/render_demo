#include "engine/render/ibl_pack.h"

#include <fstream>

namespace engine::render {
namespace {

std::uint32_t ReadU32(const std::uint8_t*& p, const std::uint8_t* end) {
  if (p + 4 > end) {
    return 0;
  }
  const std::uint32_t v = static_cast<std::uint32_t>(p[0]) |
                          (static_cast<std::uint32_t>(p[1]) << 8) |
                          (static_cast<std::uint32_t>(p[2]) << 16) |
                          (static_cast<std::uint32_t>(p[3]) << 24);
  p += 4;
  return v;
}

}  // namespace

Result<IblPack> LoadIblPack(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Result<IblPack>::Fail("Cannot open IBL pack: " + path.string());
  }
  const auto sz = in.tellg();
  if (sz < 16) {
    return Result<IblPack>::Fail("IBL pack too small");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes.data()), sz);
  const std::uint8_t* p = bytes.data();
  const std::uint8_t* end = bytes.data() + bytes.size();
  if (p[0] != 'I' || p[1] != 'B' || p[2] != 'L' || p[3] != '1') {
    return Result<IblPack>::Fail("Bad IBL magic");
  }
  p += 4;
  IblPack pack;
  pack.face_size = static_cast<int>(ReadU32(p, end));
  pack.lut_w = static_cast<int>(ReadU32(p, end));
  pack.lut_h = static_cast<int>(ReadU32(p, end));
  if (pack.face_size <= 0 || pack.lut_w <= 0 || pack.lut_h <= 0) {
    return Result<IblPack>::Fail("Invalid IBL dimensions");
  }
  const std::size_t face_bytes =
      static_cast<std::size_t>(6) * pack.face_size * pack.face_size * 4;
  const std::size_t lut_bytes = static_cast<std::size_t>(pack.lut_w) * pack.lut_h * 4;
  if (static_cast<std::size_t>(end - p) < face_bytes * 2 + lut_bytes) {
    return Result<IblPack>::Fail("IBL pack truncated");
  }
  pack.irradiance_rgba.assign(p, p + face_bytes);
  p += face_bytes;
  pack.prefilter_rgba.assign(p, p + face_bytes);
  p += face_bytes;
  pack.brdf_lut_rgba.assign(p, p + lut_bytes);
  return Result<IblPack>::Ok(std::move(pack));
}

Result<SkyCubemap> LoadSkyCubemap(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) {
    return Result<SkyCubemap>::Fail("Cannot open sky cubemap: " + path.string());
  }
  const auto sz = in.tellg();
  if (sz < 8) {
    return Result<SkyCubemap>::Fail("Sky cubemap too small");
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sz));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes.data()), sz);
  const std::uint8_t* p = bytes.data();
  const std::uint8_t* end = bytes.data() + bytes.size();
  if (p[0] != 'S' || p[1] != 'K' || p[2] != 'Y' || p[3] != '1') {
    return Result<SkyCubemap>::Fail("Bad SKY1 magic");
  }
  p += 4;
  SkyCubemap sky;
  sky.face_size = static_cast<int>(ReadU32(p, end));
  if (sky.face_size <= 0) {
    return Result<SkyCubemap>::Fail("Invalid sky face size");
  }
  const std::size_t face_bytes =
      static_cast<std::size_t>(6) * sky.face_size * sky.face_size * 4;
  if (static_cast<std::size_t>(end - p) < face_bytes) {
    return Result<SkyCubemap>::Fail("Sky cubemap truncated");
  }
  sky.rgba_faces.assign(p, p + face_bytes);
  return Result<SkyCubemap>::Ok(std::move(sky));
}

}  // namespace engine::render
