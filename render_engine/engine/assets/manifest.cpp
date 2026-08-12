#include "engine/assets/manifest.h"

#include "engine/core/log.h"

#include <fstream>
#include <sstream>

namespace engine::assets {
namespace {

// Extremely small JSON extractor for string values of a key in an object slice.
std::string ExtractStringField(std::string_view obj, std::string_view key) {
  const std::string pattern = std::string("\"") + std::string(key) + "\"";
  const auto pos = obj.find(pattern);
  if (pos == std::string_view::npos) {
    return {};
  }
  const auto colon = obj.find(':', pos + pattern.size());
  if (colon == std::string_view::npos) {
    return {};
  }
  const auto q1 = obj.find('"', colon + 1);
  if (q1 == std::string_view::npos) {
    return {};
  }
  const auto q2 = obj.find('"', q1 + 1);
  if (q2 == std::string_view::npos) {
    return {};
  }
  return std::string(obj.substr(q1 + 1, q2 - q1 - 1));
}

std::vector<AssetId> ExtractDeps(std::string_view obj) {
  std::vector<AssetId> deps;
  const auto key = obj.find("\"deps\"");
  if (key == std::string_view::npos) {
    return deps;
  }
  const auto lb = obj.find('[', key);
  const auto rb = obj.find(']', lb);
  if (lb == std::string_view::npos || rb == std::string_view::npos || rb <= lb) {
    return deps;
  }
  std::string_view arr = obj.substr(lb + 1, rb - lb - 1);
  std::size_t i = 0;
  while (i < arr.size()) {
    const auto q1 = arr.find('"', i);
    if (q1 == std::string_view::npos) {
      break;
    }
    const auto q2 = arr.find('"', q1 + 1);
    if (q2 == std::string_view::npos) {
      break;
    }
    deps.emplace_back(std::string(arr.substr(q1 + 1, q2 - q1 - 1)));
    i = q2 + 1;
  }
  return deps;
}

}  // namespace

Status Manifest::Add(ManifestEntry entry) {
  if (entry.id.empty()) {
    return Status::Fail(ErrorCode::InvalidArgument, "Manifest entry id empty");
  }
  entries_[entry.id] = std::move(entry);
  return Status::Ok();
}

const ManifestEntry* Manifest::Find(const AssetId& id) const {
  const auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : &it->second;
}

Result<Manifest> Manifest::LoadFromFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Result<Manifest>::Fail("Cannot open manifest: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();

  Manifest manifest;
  // Walk each {...} object inside "assets" array (nested braces not supported).
  const auto assets_key = text.find("\"assets\"");
  if (assets_key == std::string::npos) {
    return Result<Manifest>::Fail("Manifest missing assets array");
  }
  const auto arr_l = text.find('[', assets_key);
  const auto arr_r = text.rfind(']');
  if (arr_l == std::string::npos || arr_r == std::string::npos || arr_r <= arr_l) {
    return Result<Manifest>::Fail("Manifest assets array malformed");
  }

  std::size_t i = arr_l + 1;
  while (i < arr_r) {
    const auto obj_l = text.find('{', i);
    if (obj_l == std::string::npos || obj_l >= arr_r) {
      break;
    }
    const auto obj_r = text.find('}', obj_l);
    if (obj_r == std::string::npos || obj_r >= arr_r) {
      return Result<Manifest>::Fail("Manifest asset object not closed");
    }
    const std::string_view obj(text.data() + obj_l, obj_r - obj_l + 1);
    ManifestEntry entry;
    entry.id = AssetId(ExtractStringField(obj, "id"));
    entry.type = ExtractStringField(obj, "type");
    entry.path = ExtractStringField(obj, "path");
    entry.deps = ExtractDeps(obj);
    if (entry.id.empty() || entry.path.empty()) {
      return Result<Manifest>::Fail("Manifest asset missing id/path");
    }
    if (auto st = manifest.Add(std::move(entry)); !st) {
      return Result<Manifest>::Fail(st);
    }
    i = obj_r + 1;
  }

  if (manifest.entries_.empty()) {
    return Result<Manifest>::Fail("Manifest has no assets");
  }
  LogInfo("Manifest loaded: " + path.string());
  return Result<Manifest>::Ok(std::move(manifest));
}

}  // namespace engine::assets
