#include "mini_test.h"

#include "engine/assets/path_resolver.h"

#include <filesystem>
#include <fstream>

TEST_CASE("is_safe_relative rejects escape", "[assets]") {
  REQUIRE(engine::assets::is_safe_relative("shaders/a.cso"));
  REQUIRE_FALSE(engine::assets::is_safe_relative("../secret"));
  REQUIRE_FALSE(engine::assets::is_safe_relative("/abs/path"));
}

TEST_CASE("PathResolver finds file under root", "[assets]") {
  const auto root = std::filesystem::temp_directory_path() / "render_engine_path_test";
  std::filesystem::create_directories(root);
  const auto file = root / "triangle.vs.cso";
  {
    std::ofstream out(file, std::ios::binary);
    out << "x";
  }

  engine::assets::PathResolver resolver;
  resolver.AddRoot(root);
  const auto resolved = resolver.Resolve("triangle.vs.cso");
  REQUIRE(resolved.has_value());
  REQUIRE(std::filesystem::equivalent(*resolved, file));

  std::filesystem::remove_all(root);
}
