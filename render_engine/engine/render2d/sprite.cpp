#include "engine/render2d/sprite.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::render2d {

void SortSprites(std::vector<Sprite>& sprites) {
  std::stable_sort(sprites.begin(), sprites.end(), [](const Sprite& a, const Sprite& b) {
    if (a.sort_layer != b.sort_layer) {
      return a.sort_layer < b.sort_layer;
    }
    if (a.sort_y != b.sort_y) {
      return a.sort_y < b.sort_y;
    }
    return false;
  });
}

Status LoadTiledJson(const std::filesystem::path& path, std::vector<TilemapLayer>& out_layers) {
  std::ifstream in(path);
  if (!in) {
    return Status::Fail("cannot open tiled json: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();

  auto find_int = [&](std::string_view key, int fallback) {
    const auto p = text.find(std::string("\"") + std::string(key) + "\"");
    if (p == std::string::npos) {
      return fallback;
    }
    const auto colon = text.find(':', p);
    if (colon == std::string::npos) {
      return fallback;
    }
    return std::atoi(text.c_str() + colon + 1);
  };

  TilemapLayer layer;
  layer.name = "layer0";
  layer.width = find_int("width", 0);
  layer.height = find_int("height", 0);
  layer.tile_w = find_int("tilewidth", 16);
  layer.tile_h = find_int("tileheight", 16);
  const auto data = text.find("\"data\"");
  if (data == std::string::npos) {
    return Status::Fail("tiled json missing data");
  }
  const auto lb = text.find('[', data);
  const auto rb = text.find(']', lb);
  if (lb == std::string::npos || rb == std::string::npos) {
    return Status::Fail("tiled data array malformed");
  }
  std::string arr = text.substr(lb + 1, rb - lb - 1);
  std::stringstream nums(arr);
  std::string item;
  while (std::getline(nums, item, ',')) {
    layer.gids.push_back(std::atoi(item.c_str()));
  }
  out_layers.clear();
  out_layers.push_back(std::move(layer));
  return Status::Ok();
}

}  // namespace engine::render2d
