// Lightmap baker: procedural corner-dark / center-bright grayscale RGBA8 raw.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::uint8_t CornerDarkCenterBright(int x, int y, int size) {
  const float u = (x + 0.5f) / static_cast<float>(size);
  const float v = (y + 0.5f) / static_cast<float>(size);
  const float cx = u - 0.5f;
  const float cy = v - 0.5f;
  const float corner = std::sqrt(cx * cx + cy * cy) * 1.4f;
  const float center = 1.f - std::min(1.f, std::sqrt(cx * cx + cy * cy) * 2.f);
  const float g = std::clamp(0.25f + 0.55f * center - 0.35f * corner, 0.f, 1.f);
  return static_cast<std::uint8_t>(g * 255.f + 0.5f);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: lightmap_baker <out_dir> [size]\n";
    return 2;
  }
  const std::string out_dir = argv[1];
  int size = argc >= 3 ? std::atoi(argv[2]) : 64;
  if (size != 64 && size != 128) {
    size = (size < 96) ? 64 : 128;
  }

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(size * size * 4));
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      const std::uint8_t g = CornerDarkCenterBright(x, y, size);
      const std::size_t i = static_cast<std::size_t>((y * size + x) * 4);
      rgba[i + 0] = g;
      rgba[i + 1] = g;
      rgba[i + 2] = g;
      rgba[i + 3] = 255;
    }
  }

  const std::string out_path = out_dir + "/lightmap.rgba";
  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::cerr << "cannot write " << out_path << "\n";
    return 1;
  }
  out.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));

  std::ofstream meta(out_dir + "/lightmap.meta.txt");
  meta << "w=" << size << "\nh=" << size << "\nformat=rgba8\n";

  std::ofstream(out_dir + "/lightmap.marker") << "lightmap.rgba\n";

  std::cout << "wrote " << out_path << " (" << size << "x" << size << ")\n";
  return 0;
}
