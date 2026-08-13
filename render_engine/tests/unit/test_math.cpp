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
