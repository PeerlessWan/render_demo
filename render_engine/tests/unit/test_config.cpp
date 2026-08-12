#include "mini_test.h"

#include "engine/core/config.h"

TEST_CASE("Config set/get", "[config]") {
  engine::Config cfg;
  REQUIRE_FALSE(cfg.has("backend"));
  cfg.set("backend", "d3d12");
  REQUIRE(cfg.has("backend"));
  const auto value = cfg.get("backend");
  REQUIRE(value.has_value());
  REQUIRE(*value == "d3d12");
}

TEST_CASE("Config clear", "[config]") {
  engine::Config cfg;
  cfg.set("a", "1");
  cfg.clear();
  REQUIRE_FALSE(cfg.has("a"));
}
