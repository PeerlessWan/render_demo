#include "engine/render/ies_profile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace engine::render {
namespace {

float Saturate(float x) { return std::clamp(x, 0.f, 1.f); }

std::vector<IesLut>& Registry() {
  static std::vector<IesLut> g;
  return g;
}

std::vector<std::string> Tokenize(std::string_view text) {
  std::vector<std::string> tokens;
  std::string cur;
  auto flush = [&]() {
    if (!cur.empty()) {
      tokens.push_back(cur);
      cur.clear();
    }
  };
  for (char c : text) {
    if (c == ',' || c == '\t' || c == '\r' || c == '\n' || c == ' ') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  return tokens;
}

bool IsNumberToken(const std::string& t) {
  if (t.empty()) {
    return false;
  }
  char* end = nullptr;
  std::strtof(t.c_str(), &end);
  return end != t.c_str();
}

}  // namespace

float SampleIesLut(float u, const IesLut& lut) {
  u = Saturate(u);
  if (lut.samples.empty()) {
    return 1.f;
  }
  if (lut.samples.size() == 1) {
    return lut.samples[0] / std::max(lut.max_candela, 1e-6f);
  }
  const float pos = u * static_cast<float>(lut.samples.size() - 1);
  const std::size_t i0 = static_cast<std::size_t>(pos);
  const std::size_t i1 = std::min(i0 + 1, lut.samples.size() - 1);
  const float t = pos - static_cast<float>(i0);
  const float a = lut.samples[i0];
  const float b = lut.samples[i1];
  const float v = a + (b - a) * t;
  return v / std::max(lut.max_candela, 1e-6f);
}

float EvalIesFactor(float cos_theta, const IesLut& lut) {
  const float c = Saturate(cos_theta);
  const float u = 1.f - c;
  return SampleIesLut(u, lut);
}

float SampleIesLut(float u, int profile) {
  u = Saturate(u);
  if (profile >= 100) {
    const std::size_t idx = static_cast<std::size_t>(profile - 100);
    const auto& reg = Registry();
    if (idx < reg.size()) {
      return SampleIesLut(u, reg[idx]);
    }
    return 1.f;
  }
  switch (profile) {
    case 1:  // narrow
      return std::pow(1.f - u, 4.5f);
    case 2:  // wide wash
      return std::pow(1.f - u, 1.6f);
    case 3: {  // batwing: dip on-axis, peak mid
      const float mid = 1.f - std::fabs(u - 0.45f) * 2.2f;
      return Saturate(mid) * (0.35f + 0.65f * (1.f - u));
    }
    default:
      return 1.f;
  }
}

float EvalIesFactor(float cos_theta, int profile) {
  if (profile <= 0) {
    return 1.f;
  }
  const float c = Saturate(cos_theta);
  const float u = 1.f - c;  // 0 along axis
  return SampleIesLut(u, profile);
}

Result<IesLut> LoadIesText(std::string_view text) {
  const auto tokens = Tokenize(text);
  // Find TILT=NONE (or TILT=INCLUDE) then numeric photometric block.
  std::size_t i = 0;
  while (i < tokens.size()) {
    const auto& t = tokens[i];
    if (t.rfind("TILT=", 0) == 0 || t == "TILT") {
      ++i;
      // Skip INCLUDE tilt block if present: next number is lamp count starts after tilt data.
      if (i < tokens.size() && (tokens[i] == "INCLUDE" || tokens[i - 1].find("INCLUDE") != std::string::npos)) {
        // Unsupported full tilt include — fail soft by continuing scan for lamp count pattern.
      }
      break;
    }
    ++i;
  }
  // Advance to first numeric run (lamp count).
  while (i < tokens.size() && !IsNumberToken(tokens[i])) {
    ++i;
  }
  if (i + 10 >= tokens.size()) {
    return Result<IesLut>::Fail(Status::Fail(ErrorCode::InvalidArgument, "IES: truncated header"));
  }

  auto read_f = [&](std::size_t& idx) -> float {
    return std::strtof(tokens[idx++].c_str(), nullptr);
  };
  auto read_i = [&](std::size_t& idx) -> int {
    return static_cast<int>(std::strtol(tokens[idx++].c_str(), nullptr, 10));
  };

  (void)read_i(i);  // num lamps
  (void)read_f(i);  // lumens
  const float multiplier = std::max(read_f(i), 0.f);
  const int nvert = read_i(i);
  const int nhoriz = read_i(i);
  (void)read_i(i);  // photometric type
  (void)read_i(i);  // units
  (void)read_f(i);  // width
  (void)read_f(i);  // length
  (void)read_f(i);  // height
  (void)read_f(i);  // ballast
  if (i < tokens.size() && IsNumberToken(tokens[i])) {
    (void)read_f(i);  // ballast-lamp factor
  }
  if (i < tokens.size() && IsNumberToken(tokens[i])) {
    (void)read_f(i);  // input watts
  }

  if (nvert <= 0 || nhoriz <= 0 || nvert > 4096 || nhoriz > 4096) {
    return Result<IesLut>::Fail(Status::Fail(ErrorCode::InvalidArgument, "IES: bad angle counts"));
  }
  if (i + static_cast<std::size_t>(nvert + nhoriz) > tokens.size()) {
    return Result<IesLut>::Fail(Status::Fail(ErrorCode::InvalidArgument, "IES: truncated angles"));
  }

  std::vector<float> vert(static_cast<std::size_t>(nvert));
  for (int v = 0; v < nvert; ++v) {
    vert[static_cast<std::size_t>(v)] = read_f(i);
  }
  for (int h = 0; h < nhoriz; ++h) {
    (void)read_f(i);  // horizontal angles (ignored; we average)
  }

  const std::size_t need = static_cast<std::size_t>(nvert) * static_cast<std::size_t>(nhoriz);
  if (i + need > tokens.size()) {
    return Result<IesLut>::Fail(Status::Fail(ErrorCode::InvalidArgument, "IES: truncated candela"));
  }

  std::vector<float> avg(static_cast<std::size_t>(nvert), 0.f);
  for (int h = 0; h < nhoriz; ++h) {
    for (int v = 0; v < nvert; ++v) {
      avg[static_cast<std::size_t>(v)] += read_f(i) * (multiplier > 0.f ? multiplier : 1.f);
    }
  }
  const float inv_h = 1.f / static_cast<float>(nhoriz);
  float max_c = 1e-6f;
  for (float& c : avg) {
    c *= inv_h;
    max_c = std::max(max_c, c);
  }

  // Remap vertical angles → u in [0,1] where 0° (on-axis) → u=0, 90° → u=1.
  // Build a fixed 32-sample LUT by interpolating in angle space.
  IesLut lut;
  lut.max_candela = max_c;
  constexpr int kSamples = 32;
  lut.samples.resize(kSamples, 0.f);
  for (int s = 0; s < kSamples; ++s) {
    const float u = static_cast<float>(s) / static_cast<float>(kSamples - 1);
    const float angle_deg = u * 90.f;
    // Find surrounding vertical samples.
    std::size_t i0 = 0;
    while (i0 + 1 < vert.size() && vert[i0 + 1] < angle_deg) {
      ++i0;
    }
    const std::size_t i1 = std::min(i0 + 1, vert.size() - 1);
    float t = 0.f;
    if (i1 != i0 && vert[i1] > vert[i0]) {
      t = (angle_deg - vert[i0]) / (vert[i1] - vert[i0]);
    }
    lut.samples[static_cast<std::size_t>(s)] = avg[i0] + (avg[i1] - avg[i0]) * t;
  }
  return Result<IesLut>::Ok(std::move(lut));
}

Result<IesLut> LoadIesFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result<IesLut>::Fail(Status::Fail(ErrorCode::NotFound, "IES file not found"));
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return LoadIesText(ss.str());
}

int RegisterIesLut(IesLut lut) {
  auto& reg = Registry();
  const int id = 100 + static_cast<int>(reg.size());
  reg.push_back(std::move(lut));
  return id;
}

void ClearRegisteredIesLuts() { Registry().clear(); }

}  // namespace engine::render
