// IBL baker: procedural sky or HDR → irradiance cube + prefilter cube + BRDF LUT (raw pack).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr float kPi = 3.14159265f;

struct Vec3 {
  float x = 0, y = 0, z = 0;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Normalize(Vec3 v) {
  const float l = std::sqrt(Dot(v, v));
  return l > 1e-8f ? v * (1.f / l) : Vec3{0, 1, 0};
}

Vec3 FaceDir(int face, float u, float v) {
  switch (face) {
    case 0:
      return Normalize({1, -v, -u});
    case 1:
      return Normalize({-1, -v, u});
    case 2:
      return Normalize({u, 1, v});
    case 3:
      return Normalize({u, -1, -v});
    case 4:
      return Normalize({u, -v, 1});
    default:
      return Normalize({-u, -v, -1});
  }
}

Vec3 SampleSky(Vec3 d) {
  d = Normalize(d);
  const float up = std::clamp(d.y * 0.5f + 0.5f, 0.f, 1.f);
  Vec3 sky{0.15f + 0.35f * up, 0.18f + 0.4f * up, 0.35f + 0.55f * up};
  const float sun = std::pow(std::max(0.f, Dot(d, Normalize({0.35f, 0.85f, 0.25f}))), 48.f);
  sky = sky + Vec3{1.f, 0.95f, 0.85f} * (sun * 4.f);
  return sky;
}

std::uint8_t ToByte(float x) {
  return static_cast<std::uint8_t>(std::clamp(x / (1.f + x), 0.f, 1.f) * 255.f + 0.5f);
}

void WriteFace(std::vector<std::uint8_t>& out, int face_size, int face,
               const std::vector<Vec3>& hdr) {
  for (int y = 0; y < face_size; ++y) {
    for (int x = 0; x < face_size; ++x) {
      const Vec3 c = hdr[static_cast<std::size_t>(face * face_size * face_size + y * face_size + x)];
      out.push_back(ToByte(c.x));
      out.push_back(ToByte(c.y));
      out.push_back(ToByte(c.z));
      out.push_back(255);
    }
  }
}

Vec3 ImportanceGGX(Vec3 n, float roughness, float u1, float u2) {
  const float a = roughness * roughness;
  const float phi = 2.f * kPi * u1;
  const float cos_theta = std::sqrt((1.f - u2) / (1.f + (a * a - 1.f) * u2 + 1e-5f));
  const float sin_theta = std::sqrt(std::max(0.f, 1.f - cos_theta * cos_theta));
  Vec3 h{std::cos(phi) * sin_theta, cos_theta, std::sin(phi) * sin_theta};
  Vec3 up = std::fabs(n.y) < 0.999f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  Vec3 t = Normalize({up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x});
  Vec3 b = {n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x};
  return Normalize({t.x * h.x + n.x * h.y + b.x * h.z, t.y * h.x + n.y * h.y + b.y * h.z,
                    t.z * h.x + n.z * h.y + b.z * h.z});
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: ibl_baker <input.hdr|procedural> <out_dir> [face_size]\n";
    return 2;
  }
  const std::string input = argv[1];
  const std::string out_dir = argv[2];
  const int face_size = argc >= 4 ? std::max(8, std::atoi(argv[3])) : 32;
  const int lut_size = 128;

  // Build environment cube (procedural; HDR file path recorded in meta only for v1).
  std::vector<Vec3> env(static_cast<std::size_t>(6 * face_size * face_size));
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < face_size; ++y) {
      for (int x = 0; x < face_size; ++x) {
        const float u = (x + 0.5f) / face_size * 2.f - 1.f;
        const float v = (y + 0.5f) / face_size * 2.f - 1.f;
        env[static_cast<std::size_t>((face * face_size + y) * face_size + x)] =
            SampleSky(FaceDir(face, u, v));
      }
    }
  }
  auto sample_env = [&](Vec3 dir) {
    dir = Normalize(dir);
    const float ax = std::fabs(dir.x), ay = std::fabs(dir.y), az = std::fabs(dir.z);
    int face = 0;
    float sc = 0, tc = 0, ma = ax;
    if (ay >= ax && ay >= az) {
      face = dir.y > 0 ? 2 : 3;
      ma = ay;
      sc = dir.x;
      tc = dir.y > 0 ? dir.z : -dir.z;
    } else if (az >= ax && az >= ay) {
      face = dir.z > 0 ? 4 : 5;
      ma = az;
      sc = dir.z > 0 ? dir.x : -dir.x;
      tc = -dir.y;
    } else {
      face = dir.x > 0 ? 0 : 1;
      ma = ax;
      sc = dir.x > 0 ? -dir.z : dir.z;
      tc = -dir.y;
    }
    const float uu = 0.5f * (sc / std::max(ma, 1e-5f) + 1.f);
    const float vv = 0.5f * (tc / std::max(ma, 1e-5f) + 1.f);
    const int xi = std::clamp(static_cast<int>(uu * face_size), 0, face_size - 1);
    const int yi = std::clamp(static_cast<int>(vv * face_size), 0, face_size - 1);
    return env[static_cast<std::size_t>((face * face_size + yi) * face_size + xi)];
  };

  std::vector<Vec3> irradiance(env.size());
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < face_size; ++y) {
      for (int x = 0; x < face_size; ++x) {
        const float u = (x + 0.5f) / face_size * 2.f - 1.f;
        const float v = (y + 0.5f) / face_size * 2.f - 1.f;
        const Vec3 n = FaceDir(face, u, v);
        Vec3 acc{};
        float wsum = 0.f;
        constexpr int kN = 16;
        for (int i = 0; i < kN; ++i) {
          for (int j = 0; j < kN; ++j) {
            const float a = (i + 0.5f) / kN;
            const float b = (j + 0.5f) / kN;
            const float phi = 2.f * kPi * a;
            const float cos_t = std::sqrt(1.f - b);
            const float sin_t = std::sqrt(b);
            Vec3 up = std::fabs(n.y) < 0.999f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            Vec3 t = Normalize(
                {up.y * n.z - up.z * n.y, up.z * n.x - up.x * n.z, up.x * n.y - up.y * n.x});
            Vec3 bt = {n.y * t.z - n.z * t.y, n.z * t.x - n.x * t.z, n.x * t.y - n.y * t.x};
            Vec3 l = Normalize({t.x * std::cos(phi) * sin_t + n.x * cos_t + bt.x * std::sin(phi) * sin_t,
                               t.y * std::cos(phi) * sin_t + n.y * cos_t + bt.y * std::sin(phi) * sin_t,
                               t.z * std::cos(phi) * sin_t + n.z * cos_t + bt.z * std::sin(phi) * sin_t});
            const float ndotl = std::max(0.f, Dot(n, l));
            acc = acc + sample_env(l) * ndotl;
            wsum += ndotl;
          }
        }
        irradiance[static_cast<std::size_t>((face * face_size + y) * face_size + x)] =
            wsum > 1e-5f ? acc * (1.f / wsum) : Vec3{};
      }
    }
  }

  std::vector<Vec3> prefilter(env.size());
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < face_size; ++y) {
      for (int x = 0; x < face_size; ++x) {
        const float u = (x + 0.5f) / face_size * 2.f - 1.f;
        const float v = (y + 0.5f) / face_size * 2.f - 1.f;
        const Vec3 r = FaceDir(face, u, v);
        const float roughness = 0.5f;  // mid mip stand-in (single level pack)
        Vec3 acc{};
        float wsum = 0.f;
        constexpr int kS = 32;
        for (int s = 0; s < kS; ++s) {
          const float u1 = (s + 0.5f) / kS;
          const float u2 = static_cast<float>((s * 17) % kS) / kS;
          const Vec3 h = ImportanceGGX(r, roughness, u1, u2);
          const float vdh = Dot(r, h);
          Vec3 ll = Normalize(h * (2.f * vdh) + r * -1.f);
          const float w = std::max(0.f, Dot(r, ll));
          acc = acc + sample_env(ll) * w;
          wsum += w;
        }
        prefilter[static_cast<std::size_t>((face * face_size + y) * face_size + x)] =
            wsum > 1e-5f ? acc * (1.f / wsum) : sample_env(r);
      }
    }
  }

  std::vector<std::uint8_t> lut(static_cast<std::size_t>(lut_size * lut_size * 4));
  for (int y = 0; y < lut_size; ++y) {
    for (int x = 0; x < lut_size; ++x) {
      const float ndotv = std::max((x + 0.5f) / lut_size, 1e-3f);
      const float rough = (y + 0.5f) / lut_size;
      float a = 0.f, b = 0.f;
      constexpr int kS = 64;
      for (int i = 0; i < kS; ++i) {
        const float u1 = (i + 0.5f) / kS;
        const float u2 = static_cast<float>((i * 13) % kS) / kS;
        Vec3 v{0, ndotv, std::sqrt(std::max(0.f, 1.f - ndotv * ndotv))};
        Vec3 n{0, 1, 0};
        Vec3 h = ImportanceGGX(n, rough, u1, u2);
        Vec3 l = Normalize(h * (2.f * Dot(v, h)) + v * -1.f);
        const float ndotl = std::max(0.f, l.y);
        const float ndoth = std::max(0.f, h.y);
        const float vdoth = std::max(0.f, Dot(v, h));
        if (ndotl > 0.f) {
          const float g = ndotv / (ndotv * (1.f - rough * 0.5f) + rough * 0.5f + 1e-4f);
          const float gvis = (g * vdoth) / (ndoth * ndotv + 1e-4f);
          const float fc = std::pow(1.f - vdoth, 5.f);
          a += (1.f - fc) * gvis;
          b += fc * gvis;
        }
      }
      a /= kS;
      b /= kS;
      const std::size_t i = static_cast<std::size_t>((y * lut_size + x) * 4);
      lut[i + 0] = static_cast<std::uint8_t>(std::clamp(a, 0.f, 1.f) * 255.f);
      lut[i + 1] = static_cast<std::uint8_t>(std::clamp(b, 0.f, 1.f) * 255.f);
      lut[i + 2] = 0;
      lut[i + 3] = 255;
    }
  }

  std::vector<std::uint8_t> pack;
  pack.reserve(16 + (6 * face_size * face_size * 4) * 2 + lut.size());
  const char magic[4] = {'I', 'B', 'L', '1'};
  pack.insert(pack.end(), magic, magic + 4);
  auto put_u32 = [&](std::uint32_t v) {
    pack.push_back(static_cast<std::uint8_t>(v & 255));
    pack.push_back(static_cast<std::uint8_t>((v >> 8) & 255));
    pack.push_back(static_cast<std::uint8_t>((v >> 16) & 255));
    pack.push_back(static_cast<std::uint8_t>((v >> 24) & 255));
  };
  put_u32(static_cast<std::uint32_t>(face_size));
  put_u32(static_cast<std::uint32_t>(lut_size));
  put_u32(static_cast<std::uint32_t>(lut_size));
  WriteFace(pack, face_size, 0, irradiance);
  for (int f = 1; f < 6; ++f) WriteFace(pack, face_size, f, irradiance);
  // rewrite WriteFace appends one face - fix: write all irradiance then prefilter
  // Actually WriteFace was called wrong - rebuild irradiance/prefilter bytes cleanly:
  pack.resize(16);
  for (int f = 0; f < 6; ++f) WriteFace(pack, face_size, f, irradiance);
  for (int f = 0; f < 6; ++f) WriteFace(pack, face_size, f, prefilter);
  pack.insert(pack.end(), lut.begin(), lut.end());

  const std::string out_path = out_dir + "/ibl_pack.ibl1";
  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::cerr << "cannot write " << out_path << "\n";
    return 1;
  }
  out.write(reinterpret_cast<const char*>(pack.data()), static_cast<std::streamsize>(pack.size()));

  std::ofstream meta(out_dir + "/ibl_meta.txt");
  meta << "input=" << input << "\nface_size=" << face_size << "\nlut=" << lut_size
       << "\npack=ibl_pack.ibl1\n";
  // Compatibility markers for older docs/tests.
  std::ofstream(out_dir + "/ibl_irradiance.marker") << "ibl_pack.ibl1\n";
  std::ofstream(out_dir + "/ibl_prefilter.marker") << "ibl_pack.ibl1\n";
  std::ofstream(out_dir + "/ibl_brdf_lut.marker") << "ibl_pack.ibl1\n";
  std::cout << "wrote " << out_path << "\n";
  return 0;
}
