#include "engine/ocean/fft_ocean.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace engine::ocean {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kG = 9.81f;

bool IsPow2(int n) { return n >= 2 && (n & (n - 1)) == 0; }

void BitReversePermute(std::complex<float>* a, int n) {
  int j = 0;
  for (int i = 1; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) {
      j ^= bit;
    }
    j ^= bit;
    if (i < j) {
      std::swap(a[i], a[j]);
    }
  }
}

// In-place radix-2 Cooley–Tukey. inverse=true → IFFT (no 1/n; caller scales).
void Fft1D(std::complex<float>* a, int n, bool inverse) {
  BitReversePermute(a, n);
  for (int len = 2; len <= n; len <<= 1) {
    const float ang = 2.f * kPi / static_cast<float>(len) * (inverse ? 1.f : -1.f);
    const std::complex<float> wlen{std::cos(ang), std::sin(ang)};
    for (int i = 0; i < n; i += len) {
      std::complex<float> w{1.f, 0.f};
      for (int j = 0; j < len / 2; ++j) {
        const std::complex<float> u = a[i + j];
        const std::complex<float> v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wlen;
      }
    }
  }
}

float Phillips(float kx, float kz, float wind_speed, const Vec2& wind_dir, float amplitude) {
  const float k2 = kx * kx + kz * kz;
  if (k2 < 1e-8f) {
    return 0.f;
  }
  const float k_len = std::sqrt(k2);
  const float k_hat_x = kx / k_len;
  const float k_hat_z = kz / k_len;
  const float wlen = std::sqrt(wind_dir.x * wind_dir.x + wind_dir.y * wind_dir.y);
  const float wx = wlen > 1e-6f ? wind_dir.x / wlen : 1.f;
  const float wz = wlen > 1e-6f ? wind_dir.y / wlen : 0.f;
  const float kdotw = k_hat_x * wx + k_hat_z * wz;
  if (kdotw < 0.f) {
    return 0.f;
  }
  const float L = wind_speed * wind_speed / kG;
  const float L2 = L * L;
  float p = amplitude * std::exp(-1.f / (k2 * L2)) / (k2 * k2);
  p *= kdotw * kdotw;
  // Suppress tiny waves.
  const float l = L * 0.001f;
  p *= std::exp(-k2 * l * l);
  return p;
}

float Gaussian(std::uint32_t& rng) {
  // Box-Muller
  rng = rng * 1664525u + 1013904223u;
  const float u1 = std::max(1e-6f, static_cast<float>(rng & 0x00FFFFFFu) / float(0x01000000u));
  rng = rng * 1664525u + 1013904223u;
  const float u2 = static_cast<float>(rng & 0x00FFFFFFu) / float(0x01000000u);
  return std::sqrt(-2.f * std::log(u1)) * std::cos(2.f * kPi * u2);
}

}  // namespace

void FftOcean::Configure(const FftOceanDesc& desc) {
  n_ = IsPow2(desc.resolution) ? desc.resolution : 64;
  world_size_ = std::max(1.f, desc.world_size);
  amplitude_ = std::max(0.f, desc.amplitude);
  wind_speed_ = std::max(0.5f, desc.wind_speed);
  wind_dir_ = desc.wind_dir;
  chop_ = std::clamp(desc.chop, 0.f, 2.f);
  time_ = 0.f;
  origin_ = {};

  const std::size_t n2 = static_cast<std::size_t>(n_) * static_cast<std::size_t>(n_);
  h0_.assign(n2 * 2, 0.f);
  spectrum_.assign(n2 * 2, 0.f);
  height_.width = n_;
  height_.height = n_;
  height_.cell = world_size_ / static_cast<float>(n_);
  height_.samples.assign(n2, 0.f);
  foam_.assign(n2, 0.f);
  disp_x_.assign(n2, 0.f);
  disp_z_.assign(n2, 0.f);
  BuildSpectrumSeed();
  EvolveSpectrum();
  Ifft2DHeight();
  RebuildDerived();
}

void FftOcean::BuildSpectrumSeed() {
  std::uint32_t rng = 0xC0FFEEu;
  const float dk = 2.f * kPi / world_size_;
  for (int z = 0; z < n_; ++z) {
    for (int x = 0; x < n_; ++x) {
      const int kx_i = (x < n_ / 2) ? x : x - n_;
      const int kz_i = (z < n_ / 2) ? z : z - n_;
      const float kx = static_cast<float>(kx_i) * dk;
      const float kz = static_cast<float>(kz_i) * dk;
      const float p = Phillips(kx, kz, wind_speed_, wind_dir_, amplitude_);
      const float sigma = std::sqrt(std::max(p, 0.f) * 0.5f);
      const float er = Gaussian(rng) * sigma;
      const float ei = Gaussian(rng) * sigma;
      const std::size_t i = static_cast<std::size_t>(z * n_ + x) * 2;
      h0_[i + 0] = er;
      h0_[i + 1] = ei;
    }
  }
}

void FftOcean::EvolveSpectrum() {
  const float dk = 2.f * kPi / world_size_;
  for (int z = 0; z < n_; ++z) {
    for (int x = 0; x < n_; ++x) {
      const int kx_i = (x < n_ / 2) ? x : x - n_;
      const int kz_i = (z < n_ / 2) ? z : z - n_;
      const float kx = static_cast<float>(kx_i) * dk;
      const float kz = static_cast<float>(kz_i) * dk;
      const float k_len = std::sqrt(kx * kx + kz * kz);
      const float omega = std::sqrt(kG * k_len);

      const std::size_t i = static_cast<std::size_t>(z * n_ + x) * 2;
      const int xn = (n_ - x) % n_;
      const int zn = (n_ - z) % n_;
      const std::size_t in = static_cast<std::size_t>(zn * n_ + xn) * 2;

      const std::complex<float> h0{h0_[i], h0_[i + 1]};
      const std::complex<float> h0c{h0_[in], -h0_[in + 1]};
      const std::complex<float> e{std::cos(omega * time_), std::sin(omega * time_)};
      const std::complex<float> ht = h0 * e + h0c * std::conj(e);
      spectrum_[i + 0] = ht.real();
      spectrum_[i + 1] = ht.imag();
    }
  }
}

void FftOcean::Ifft2DHeight() {
  const int n = n_;
  std::vector<std::complex<float>> grid(static_cast<std::size_t>(n * n));
  for (int z = 0; z < n; ++z) {
    for (int x = 0; x < n; ++x) {
      const std::size_t i = static_cast<std::size_t>(z * n + x);
      grid[i] = {spectrum_[i * 2], spectrum_[i * 2 + 1]};
    }
  }
  // Rows
  for (int z = 0; z < n; ++z) {
    Fft1D(grid.data() + static_cast<std::size_t>(z * n), n, true);
  }
  // Cols
  std::vector<std::complex<float>> col(static_cast<std::size_t>(n));
  for (int x = 0; x < n; ++x) {
    for (int z = 0; z < n; ++z) {
      col[static_cast<std::size_t>(z)] = grid[static_cast<std::size_t>(z * n + x)];
    }
    Fft1D(col.data(), n, true);
    for (int z = 0; z < n; ++z) {
      grid[static_cast<std::size_t>(z * n + x)] = col[static_cast<std::size_t>(z)];
    }
  }
  const float inv = 1.f / static_cast<float>(n * n);
  for (int z = 0; z < n; ++z) {
    for (int x = 0; x < n; ++x) {
      const std::size_t i = static_cast<std::size_t>(z * n + x);
      // Sign flip for FFT centering.
      const float sign = ((x + z) & 1) ? -1.f : 1.f;
      height_.samples[i] = grid[i].real() * inv * sign;
    }
  }
}

void FftOcean::RebuildDerived() {
  const int n = n_;
  const float cell = height_.cell;
  foam_.assign(static_cast<std::size_t>(n * n), 0.f);
  for (int z = 0; z < n; ++z) {
    for (int x = 0; x < n; ++x) {
      const auto at = [&](int ix, int iz) {
        ix = (ix + n) % n;
        iz = (iz + n) % n;
        return height_.samples[static_cast<std::size_t>(iz * n + ix)];
      };
      const float hL = at(x - 1, z);
      const float hR = at(x + 1, z);
      const float hD = at(x, z - 1);
      const float hU = at(x, z + 1);
      const float dx = (hR - hL) / (2.f * cell);
      const float dz = (hU - hD) / (2.f * cell);
      const float slope = std::sqrt(dx * dx + dz * dz);
      const std::size_t i = static_cast<std::size_t>(z * n + x);
      foam_[i] = std::clamp(slope * 2.5f - 0.15f, 0.f, 1.f);
      // Lightweight chop displacement from slope (visual only).
      disp_x_[i] = -dx * chop_ * cell;
      disp_z_[i] = -dz * chop_ * cell;
    }
  }
}

void FftOcean::Update(float dt) {
  if (n_ < 2) {
    return;
  }
  time_ += std::max(0.f, dt);
  EvolveSpectrum();
  Ifft2DHeight();
  RebuildDerived();
}

void FftOcean::SnapOriginToCamera(const Vec3& camera_pos) {
  if (world_size_ <= 1e-4f) {
    return;
  }
  const float half = world_size_ * 0.5f;
  const float ox = std::floor((camera_pos.x - half) / world_size_) * world_size_;
  const float oz = std::floor((camera_pos.z - half) / world_size_) * world_size_;
  origin_ = {ox, 0.f, oz};
}

float FftOcean::WrapLocal(float v) const {
  float t = std::fmod(v, world_size_);
  if (t < 0.f) {
    t += world_size_;
  }
  return t;
}

float FftOcean::SampleHeight(float x, float z) const {
  if (n_ < 2 || height_.samples.empty()) {
    return 0.f;
  }
  const float lx = WrapLocal(x - origin_.x) / height_.cell;
  const float lz = WrapLocal(z - origin_.z) / height_.cell;
  int x0 = static_cast<int>(std::floor(lx)) % n_;
  int z0 = static_cast<int>(std::floor(lz)) % n_;
  if (x0 < 0) {
    x0 += n_;
  }
  if (z0 < 0) {
    z0 += n_;
  }
  const int x1 = (x0 + 1) % n_;
  const int z1 = (z0 + 1) % n_;
  const float fx = lx - std::floor(lx);
  const float fz = lz - std::floor(lz);
  const auto at = [&](int ix, int iz) {
    return height_.samples[static_cast<std::size_t>(iz * n_ + ix)];
  };
  const float h00 = at(x0, z0);
  const float h10 = at(x1, z0);
  const float h01 = at(x0, z1);
  const float h11 = at(x1, z1);
  const float hx0 = h00 + (h10 - h00) * fx;
  const float hx1 = h01 + (h11 - h01) * fx;
  return hx0 + (hx1 - hx0) * fz;
}

Vec3 FftOcean::SampleNormal(float x, float z) const {
  const float eps = height_.cell;
  const float hL = SampleHeight(x - eps, z);
  const float hR = SampleHeight(x + eps, z);
  const float hD = SampleHeight(x, z - eps);
  const float hU = SampleHeight(x, z + eps);
  return Normalize(Vec3{hL - hR, 2.f * eps, hD - hU});
}

float FftOcean::SampleFoam(float x, float z) const {
  if (foam_.empty() || n_ < 2) {
    return 0.f;
  }
  const float lx = WrapLocal(x - origin_.x) / height_.cell;
  const float lz = WrapLocal(z - origin_.z) / height_.cell;
  const int x0 = std::clamp(static_cast<int>(std::floor(lx)), 0, n_ - 2);
  const int z0 = std::clamp(static_cast<int>(std::floor(lz)), 0, n_ - 2);
  const float fx = lx - static_cast<float>(x0);
  const float fz = lz - static_cast<float>(z0);
  const auto at = [&](int ix, int iz) {
    return foam_[static_cast<std::size_t>(iz * n_ + ix)];
  };
  const float f00 = at(x0, z0);
  const float f10 = at(x0 + 1, z0);
  const float f01 = at(x0, z0 + 1);
  const float f11 = at(x0 + 1, z0 + 1);
  const float fx0 = f00 + (f10 - f00) * fx;
  const float fx1 = f01 + (f11 - f01) * fx;
  return fx0 + (fx1 - fx0) * fz;
}

void FftOcean::AnimateMesh(terrain::TerrainMesh& mesh) const {
  if (mesh.positions.size() < 3 || n_ < 2) {
    return;
  }
  const std::size_t count = mesh.positions.size() / 3;
  mesh.normals.assign(count * 3, 0.f);
  if (mesh.uvs.size() < count * 2) {
    mesh.uvs.assign(count * 2, 0.f);
  }
  for (std::size_t i = 0; i < count; ++i) {
    float& x = mesh.positions[i * 3 + 0];
    float& y = mesh.positions[i * 3 + 1];
    float& z = mesh.positions[i * 3 + 2];
    y = SampleHeight(x, z);
    const Vec3 n = SampleNormal(x, z);
    mesh.normals[i * 3 + 0] = n.x;
    mesh.normals[i * 3 + 1] = n.y;
    mesh.normals[i * 3 + 2] = n.z;
    // Pack foam into UV.x for host tinting (UV.y kept).
    mesh.uvs[i * 2 + 0] = SampleFoam(x, z);
  }
}

terrain::TerrainMesh BuildOceanTileMesh(const FftOcean& ocean, int segments) {
  terrain::TerrainMesh mesh;
  const int seg = std::max(1, segments);
  const int verts = seg + 1;
  const float size = ocean.world_size();
  const Vec3 o = ocean.origin();
  mesh.positions.reserve(static_cast<std::size_t>(verts * verts * 3));
  mesh.uvs.reserve(static_cast<std::size_t>(verts * verts * 2));
  for (int z = 0; z < verts; ++z) {
    const float tz = static_cast<float>(z) / static_cast<float>(seg);
    const float wz = o.z + size * tz;
    for (int x = 0; x < verts; ++x) {
      const float tx = static_cast<float>(x) / static_cast<float>(seg);
      const float wx = o.x + size * tx;
      mesh.positions.push_back(wx);
      mesh.positions.push_back(0.f);
      mesh.positions.push_back(wz);
      mesh.uvs.push_back(tx);
      mesh.uvs.push_back(tz);
    }
  }
  mesh.indices.reserve(static_cast<std::size_t>(seg * seg * 6));
  for (int z = 0; z < seg; ++z) {
    for (int x = 0; x < seg; ++x) {
      const std::uint32_t i0 = static_cast<std::uint32_t>(z * verts + x);
      const std::uint32_t i1 = i0 + 1;
      const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(verts);
      const std::uint32_t i3 = i2 + 1;
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i1);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i0);
      mesh.indices.push_back(i3);
      mesh.indices.push_back(i2);
    }
  }
  ocean.AnimateMesh(mesh);
  return mesh;
}

}  // namespace engine::ocean
