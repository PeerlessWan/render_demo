#include "engine/render2d/sprite.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace engine::render2d {
namespace {

int FindIntAfter(const std::string& text, std::size_t from, std::string_view key, int fallback) {
  const auto p = text.find(std::string("\"") + std::string(key) + "\"", from);
  if (p == std::string::npos) {
    return fallback;
  }
  const auto colon = text.find(':', p);
  if (colon == std::string::npos) {
    return fallback;
  }
  return std::atoi(text.c_str() + colon + 1);
}

std::string FindStringAfter(const std::string& text, std::size_t from, std::string_view key) {
  const auto p = text.find(std::string("\"") + std::string(key) + "\"", from);
  if (p == std::string::npos) {
    return {};
  }
  const auto colon = text.find(':', p);
  if (colon == std::string::npos) {
    return {};
  }
  const auto q0 = text.find('"', colon + 1);
  if (q0 == std::string::npos) {
    return {};
  }
  const auto q1 = text.find('"', q0 + 1);
  if (q1 == std::string::npos) {
    return {};
  }
  return text.substr(q0 + 1, q1 - q0 - 1);
}

bool ContainsInsensitive(std::string_view hay, std::string_view needle) {
  if (needle.empty() || hay.size() < needle.size()) {
    return needle.empty();
  }
  auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < needle.size(); ++j) {
      if (lower(static_cast<unsigned char>(hay[i + j])) !=
          lower(static_cast<unsigned char>(needle[j]))) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

bool ParseIntArray(const std::string& text, std::size_t lb, std::size_t rb, std::vector<int>& out) {
  if (lb == std::string::npos || rb == std::string::npos || rb <= lb) {
    return false;
  }
  out.clear();
  std::string arr = text.substr(lb + 1, rb - lb - 1);
  std::stringstream nums(arr);
  std::string item;
  while (std::getline(nums, item, ',')) {
    // skip whitespace
    std::size_t i = 0;
    while (i < item.size() && std::isspace(static_cast<unsigned char>(item[i]))) {
      ++i;
    }
    if (i >= item.size()) {
      continue;
    }
    out.push_back(std::atoi(item.c_str() + static_cast<std::ptrdiff_t>(i)));
  }
  return true;
}

std::size_t MatchingBracket(const std::string& text, std::size_t open, char open_ch, char close_ch) {
  if (open == std::string::npos || open >= text.size() || text[open] != open_ch) {
    return std::string::npos;
  }
  int depth = 0;
  for (std::size_t i = open; i < text.size(); ++i) {
    if (text[i] == open_ch) {
      ++depth;
    } else if (text[i] == close_ch) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

bool IsCollisionLayer(const TilemapLayer& layer) {
  if (ContainsInsensitive(layer.name, "collision")) {
    return true;
  }
  if (ContainsInsensitive(layer.type, "collision")) {
    return true;
  }
  return false;
}

}  // namespace

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

  const int map_w = FindIntAfter(text, 0, "width", 0);
  const int map_h = FindIntAfter(text, 0, "height", 0);
  const int tile_w = FindIntAfter(text, 0, "tilewidth", 16);
  const int tile_h = FindIntAfter(text, 0, "tileheight", 16);

  std::string tileset_image;
  const auto tilesets_key = text.find("\"tilesets\"");
  if (tilesets_key != std::string::npos) {
    const auto arr = text.find('[', tilesets_key);
    const auto arr_end = MatchingBracket(text, arr, '[', ']');
    if (arr != std::string::npos && arr_end != std::string::npos) {
      const auto obj = text.find('{', arr);
      if (obj != std::string::npos && obj < arr_end) {
        tileset_image = FindStringAfter(text, obj, "image");
      }
    }
  }

  out_layers.clear();
  const auto layers_key = text.find("\"layers\"");
  if (layers_key == std::string::npos) {
    // Legacy single-data fallback (pre-multi-layer maps).
    TilemapLayer layer;
    layer.name = "layer0";
    layer.type = "tilelayer";
    layer.width = map_w;
    layer.height = map_h;
    layer.tile_w = tile_w;
    layer.tile_h = tile_h;
    layer.tileset_image = tileset_image;
    const auto data = text.find("\"data\"");
    if (data == std::string::npos) {
      return Status::Fail("tiled json missing layers/data");
    }
    const auto lb = text.find('[', data);
    const auto rb = text.find(']', lb);
    if (!ParseIntArray(text, lb, rb, layer.gids)) {
      return Status::Fail("tiled data array malformed");
    }
    layer.collision = IsCollisionLayer(layer);
    out_layers.push_back(std::move(layer));
    return Status::Ok();
  }

  const auto layers_arr = text.find('[', layers_key);
  const auto layers_end = MatchingBracket(text, layers_arr, '[', ']');
  if (layers_arr == std::string::npos || layers_end == std::string::npos) {
    return Status::Fail("tiled layers array malformed");
  }

  std::size_t pos = layers_arr + 1;
  while (pos < layers_end) {
    const auto obj = text.find('{', pos);
    if (obj == std::string::npos || obj >= layers_end) {
      break;
    }
    const auto obj_end = MatchingBracket(text, obj, '{', '}');
    if (obj_end == std::string::npos || obj_end > layers_end) {
      return Status::Fail("tiled layer object malformed");
    }

    TilemapLayer layer;
    layer.name = FindStringAfter(text, obj, "name");
    if (layer.name.empty()) {
      layer.name = "layer" + std::to_string(out_layers.size());
    }
    layer.type = FindStringAfter(text, obj, "type");
    layer.width = FindIntAfter(text, obj, "width", map_w);
    layer.height = FindIntAfter(text, obj, "height", map_h);
    layer.tile_w = tile_w;
    layer.tile_h = tile_h;
    layer.tileset_image = tileset_image;

    // Prefer this layer's own "data" array (within object bounds).
    const auto data = text.find("\"data\"", obj);
    if (data != std::string::npos && data < obj_end) {
      const auto lb = text.find('[', data);
      const auto rb = MatchingBracket(text, lb, '[', ']');
      if (lb != std::string::npos && rb != std::string::npos && rb <= obj_end) {
        if (!ParseIntArray(text, lb, rb, layer.gids)) {
          return Status::Fail("tiled layer data malformed: " + layer.name);
        }
      }
    }

    layer.collision = IsCollisionLayer(layer);
    // Keep tile layers (with data) and explicitly flagged collision layers.
    if (!layer.gids.empty() || layer.collision) {
      out_layers.push_back(std::move(layer));
    }
    pos = obj_end + 1;
  }

  if (out_layers.empty()) {
    return Status::Fail("tiled json has no tile layers");
  }
  return Status::Ok();
}

bool ExportCollisionGids(const std::vector<TilemapLayer>& layers, std::vector<int>& out_gids,
                         int& out_width, int& out_height) {
  for (const auto& layer : layers) {
    if (!layer.collision || layer.gids.empty()) {
      continue;
    }
    out_gids = layer.gids;
    out_width = layer.width;
    out_height = layer.height;
    return true;
  }
  return false;
}

}  // namespace engine::render2d
