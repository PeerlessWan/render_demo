#include "mini_test.h"

#include "engine/core/math.h"

#include <cmath>

TEST_CASE("Vec3 length", "[math]") {
  const engine::Vec3 v{3.f, 4.f, 0.f};
  REQUIRE(v.length_squared() == 25.f);
  REQUIRE(v.length() == 5.f);
}

TEST_CASE("Vec3 zero", "[math]") {
  const engine::Vec3 z{};
  REQUIRE(z.length() == 0.f);
}

TEST_CASE("Mat4 Inverse roundtrip", "[math]") {
  const auto m = engine::Mat4::Perspective(1.0f, 1.6f, 0.1f, 100.f) *
                 engine::Mat4::LookAt({0, 2, 5}, {0, 0, 0}, {0, 1, 0});
  const auto inv = m.Inverse();
  const auto id = inv * m;
  REQUIRE(std::fabs(id.m[0] - 1.f) < 1e-3f);
  REQUIRE(std::fabs(id.m[5] - 1.f) < 1e-3f);
  REQUIRE(std::fabs(id.m[10] - 1.f) < 1e-3f);
  REQUIRE(std::fabs(id.m[15] - 1.f) < 1e-3f);
}

TEST_CASE("Mat4 Perspective maps near/far to D3D Z [0,1]", "[math]") {
  constexpr float zn = 0.1f;
  constexpr float zf = 100.f;
  const auto proj = engine::Mat4::Perspective(1.0f, 1.f, zn, zf);
  // View looks down -Z; a point at distance d in front is (0,0,-d).
  const auto near_ndc = proj.TransformPoint({0.f, 0.f, -zn});
  const auto far_ndc = proj.TransformPoint({0.f, 0.f, -zf});
  REQUIRE(std::fabs(near_ndc.z - 0.f) < 1e-4f);
  REQUIRE(std::fabs(far_ndc.z - 1.f) < 1e-4f);
  const auto mid_ndc = proj.TransformPoint({0.f, 0.f, -1.f});
  REQUIRE(mid_ndc.z > 0.f);
  REQUIRE(mid_ndc.z < 1.f);
}
