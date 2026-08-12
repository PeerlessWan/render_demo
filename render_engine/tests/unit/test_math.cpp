#include "mini_test.h"

#include "engine/core/math.h"

TEST_CASE("Vec3 length", "[math]") {
  const engine::Vec3 v{3.f, 4.f, 0.f};
  REQUIRE(v.length_squared() == 25.f);
  REQUIRE(v.length() == 5.f);
}

TEST_CASE("Vec3 zero", "[math]") {
  const engine::Vec3 z{};
  REQUIRE(z.length() == 0.f);
}
