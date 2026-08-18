#include "play/scene_play.h"

#include "engine/core/math.h"

#include "game_kit/runtime.h"
#include "game_kit/script_fields.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {
namespace {

void Collect(const engine::scene::World& world, engine::scene::NodeId id,
             std::vector<engine::scene::NodeId>* out) {
  if (!out || !world.valid(id)) {
    return;
  }
  out->push_back(id);
  for (auto c : world.children(id)) {
    Collect(world, c, out);
  }
}

std::string MetaKey(const std::string& name, int ordinal) {
  if (name.empty()) {
    return std::string("_#") + std::to_string(ordinal);
  }
  if (ordinal > 0) {
    return name + "#" + std::to_string(ordinal);
  }
  return name;
}

bool IsGroundNode(const engine::scene::World& world, engine::scene::NodeId id) {
  if (world.name(id) == "ground") {
    return true;
  }
  const auto* mesh = world.mesh(id);
  return mesh && mesh->mesh_id == "ground";
}

std::string EscapeMeta(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"') {
      o.push_back('\\');
      o.push_back(c);
    } else if (c == '\n' || c == '\r') {
      o += "\\n";
    } else {
      o.push_back(c);
    }
  }
  return o;
}

std::string UnescapeMeta(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      o.push_back(n == 'n' ? '\n' : n);
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

std::string ReadAllText(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

std::string EncodeNodeMeta(const NodeMeta& m) {
  std::ostringstream o;
  o << "{\"has_light\":" << (m.has_light ? "true" : "false") << ",\"light_range\":" << m.light_range
    << ",\"light_intensity\":" << m.light_intensity << ",\"light_kind\":" << m.light_kind
    << ",\"light_r\":" << m.light_r << ",\"light_g\":" << m.light_g << ",\"light_b\":" << m.light_b
    << ",\"has_camera\":" << (m.has_camera ? "true" : "false")
    << ",\"active_camera\":" << (m.active_camera ? "true" : "false") << ",\"camera_fovy\":" << m.camera_fovy
    << ",\"has_collider\":" << (m.has_collider ? "true" : "false") << ",\"collider_hx\":" << m.collider_hx
    << ",\"collider_hy\":" << m.collider_hy << ",\"collider_hz\":" << m.collider_hz << ",\"material\":\""
    << m.material_id << "\",\"fields\":\"" << EscapeMeta(m.script_fields) << "\",\"anim\":\""
    << EscapeMeta(m.anim_state) << "\",\"source\":\"" << EscapeMeta(m.source_prefab) << "\"}";
  return o.str();
}

void DecodeNodeMetaExtra(std::string_view extra, NodeMeta* m) {
  if (!m || extra.empty()) {
    return;
  }
  auto has = [&](const char* key) {
    const std::string pat = std::string("\"") + key + "\":true";
    return extra.find(pat) != std::string_view::npos;
  };
  auto num = [&](const char* key, float* out) {
    const std::string pat = std::string("\"") + key + "\":";
    const auto i = extra.find(pat);
    if (i == std::string_view::npos || !out) {
      return;
    }
    *out = std::strtof(extra.data() + i + pat.size(), nullptr);
  };
  auto str = [&](const char* key, std::string* out) {
    const std::string pat = std::string("\"") + key + "\":\"";
    const auto i = extra.find(pat);
    if (i == std::string_view::npos || !out) {
      return;
    }
    auto s = extra.substr(i + pat.size());
    const auto e = s.find('"');
    if (e != std::string_view::npos) {
      *out = UnescapeMeta(s.substr(0, e));
    }
  };
  m->has_light = has("has_light");
  m->has_camera = has("has_camera");
  m->active_camera = has("active_camera");
  m->has_collider = has("has_collider");
  num("light_range", &m->light_range);
  num("light_intensity", &m->light_intensity);
  float kind = static_cast<float>(m->light_kind);
  num("light_kind", &kind);
  m->light_kind = static_cast<int>(kind);
  num("light_r", &m->light_r);
  num("light_g", &m->light_g);
  num("light_b", &m->light_b);
  num("camera_fovy", &m->camera_fovy);
  num("collider_hx", &m->collider_hx);
  num("collider_hy", &m->collider_hy);
  num("collider_hz", &m->collider_hz);
  str("material", &m->material_id);
  str("fields", &m->script_fields);
  str("anim", &m->anim_state);
  str("source", &m->source_prefab);
}

void CollectAllNodes(const engine::scene::World& world, std::vector<engine::scene::NodeId>* out) {
  if (!out) {
    return;
  }
  out->clear();
  for (auto r : world.roots()) {
    Collect(world, r, out);
  }
}

engine::scene::NodeId FindNamed(const engine::scene::World& world, std::string_view name) {
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(world, &nodes);
  for (auto id : nodes) {
    if (world.name(id) == name) {
      return id;
    }
  }
  return engine::scene::kInvalidNode;
}

void StampMeta(game_kit::SceneDocument* doc,
               const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  if (!doc) {
    return;
  }
  doc->format_version = game_kit::kSceneFormatCurrent;
  for (auto& n : doc->nodes) {
    const auto id = static_cast<engine::scene::NodeId>(std::strtoul(n.id.c_str(), nullptr, 10));
    auto it = meta.find(id);
    if (it == meta.end()) {
      continue;
    }
    n.prefab_id = it->second.prefab_id;
    n.script_path = it->second.script_path;
    n.extra_json = EncodeNodeMeta(it->second);
    n.override_json = it->second.override_json;
    if (!it->second.script_path.empty()) {
      bool has = false;
      for (auto& c : n.components) {
        if (c.type == "Script") {
          c.script = it->second.script_path;
          if (!it->second.script_fields.empty()) {
            c.fields_json = it->second.script_fields;
          }
          has = true;
          break;
        }
      }
      if (!has) {
        game_kit::SceneComponent sc;
        sc.type = "Script";
        sc.script = it->second.script_path;
        if (!it->second.script_fields.empty()) {
          sc.fields_json = it->second.script_fields;
        }
        n.components.push_back(std::move(sc));
      }
    }
  }
}

void RestoreMeta(const game_kit::SceneDocument& stored, const game_kit::SceneDocument& captured,
                 std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!meta) {
    return;
  }
  meta->clear();
  std::unordered_map<std::string, int> stored_seen;
  std::unordered_map<std::string, const game_kit::SceneNode*> by_key;
  for (const auto& n : stored.nodes) {
    const std::string kn = n.name.empty() ? "_" : n.name;
    const int ord = stored_seen[kn]++;
    by_key[MetaKey(n.name, ord)] = &n;
  }
  std::unordered_map<std::string, int> captured_seen;
  for (const auto& n : captured.nodes) {
    const std::string kn = n.name.empty() ? "_" : n.name;
    const int ord = captured_seen[kn]++;
    auto it = by_key.find(MetaKey(n.name, ord));
    if (it == by_key.end()) {
      continue;
    }
    const auto id = static_cast<engine::scene::NodeId>(std::strtoul(n.id.c_str(), nullptr, 10));
    (*meta)[id] = NodeMeta{};
    auto& mm = (*meta)[id];
    mm.prefab_id = it->second->prefab_id;
    mm.script_path = it->second->script_path;
    mm.override_json = it->second->override_json;
    DecodeNodeMetaExtra(it->second->extra_json, &mm);
    if (mm.script_path.empty()) {
      for (const auto& c : it->second->components) {
        if (c.type == "Script" && !c.script.empty()) {
          mm.script_path = c.script;
          if (!c.fields_json.empty()) {
            mm.script_fields = c.fields_json;
          }
        }
      }
    }
    if (mm.source_prefab.empty()) {
      mm.source_prefab = mm.prefab_id;
    }
  }
}

void SyncMetaToWorld(engine::scene::World& world,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  for (const auto& kv : meta) {
    if (!world.valid(kv.first)) {
      continue;
    }
    const auto& m = kv.second;
    if (m.has_light) {
      engine::scene::LightComponent L;
      L.range = m.light_range;
      L.intensity = m.light_intensity;
      L.kind = m.light_kind;
      L.color = {m.light_r, m.light_g, m.light_b};
      world.set_light(kv.first, L);
    } else {
      world.clear_light(kv.first);
    }
    if (m.has_camera) {
      engine::scene::CameraComponent cam;
      cam.active = m.active_camera;
      cam.fovy_rad = m.camera_fovy;
      world.set_camera(kv.first, cam);
    } else {
      world.clear_camera(kv.first);
    }
    if (m.has_collider) {
      engine::scene::ColliderComponent col;
      col.hx = m.collider_hx;
      col.hy = m.collider_hy;
      col.hz = m.collider_hz;
      world.set_collider(kv.first, col);
    } else {
      world.clear_collider(kv.first);
    }
    if (const auto* mesh = world.mesh(kv.first); mesh && !m.material_id.empty()) {
      auto copy = *mesh;
      copy.material_id = m.material_id;
      world.set_mesh(kv.first, copy);
    }
  }
}

void SyncWorldToMeta(const engine::scene::World& world,
                     std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!meta) {
    return;
  }
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(world, &nodes);
  for (auto id : nodes) {
    auto& m = (*meta)[id];
    if (const auto* L = world.light(id)) {
      m.has_light = true;
      m.light_range = L->range;
      m.light_intensity = L->intensity;
      m.light_kind = L->kind;
      m.light_r = L->color.x;
      m.light_g = L->color.y;
      m.light_b = L->color.z;
    }
    if (const auto* cam = world.camera(id)) {
      m.has_camera = true;
      m.active_camera = cam->active;
      m.camera_fovy = cam->fovy_rad;
    }
    if (const auto* col = world.collider(id)) {
      m.has_collider = true;
      m.collider_hx = col->hx;
      m.collider_hy = col->hy;
      m.collider_hz = col->hz;
    }
    if (const auto* mesh = world.mesh(id)) {
      if (!mesh->material_id.empty()) {
        m.material_id = mesh->material_id;
      }
    }
  }
}

void BindPlayScripts(game_kit::ScriptComponentWorld& scripts, engine::scene::World& world,
                     game_kit::GameRuntime& rt,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  rt.set_world(&world);
  rt.entities().Clear();
  scripts.Clear();
  scripts.AttachHost(&world, &rt);
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(world, &nodes);
  for (auto id : nodes) {
    std::string name = world.name(id);
    if (name.empty()) {
      name = "node_" + std::to_string(id);
    }
    (void)rt.entities().Create(name, id);
    auto it = meta.find(id);
    if (it != meta.end() && !it->second.anim_state.empty()) {
      rt.anims().GetOrCreate(name).Play(it->second.anim_state, true);
    }
    if (it == meta.end() || it->second.script_path.empty()) {
      continue;
    }
    const auto cid = scripts.Attach(id, it->second.script_path);
    if (auto* c = scripts.Get(cid)) {
      (void)scripts.LoadFromDisk(*c);
      const auto src = ReadAllText(c->path.empty() ? it->second.script_path : c->path);
      const auto blob = game_kit::MergeExportsAndPersist(src, it->second.script_fields);
      if (!blob.empty()) {
        (void)c->vm.RestorePersist(blob);
      }
    }
  }
}

float GroundTopY(const engine::scene::World& world, float x, float z) {
  float best = 0.f;
  bool any = false;
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(world, &nodes);
  for (auto id : nodes) {
    if (!IsGroundNode(world, id)) {
      continue;
    }
    const auto& t = world.local_transform(id);
    const float hx = std::fabs(t.scale.x) * 0.5f;
    const float hz = std::fabs(t.scale.z) * 0.5f;
    if (x < t.position.x - hx || x > t.position.x + hx || z < t.position.z - hz ||
        z > t.position.z + hz) {
      continue;
    }
    const float top = t.position.y + std::fabs(t.scale.y) * 0.5f;
    if (!any || top > best) {
      best = top;
      any = true;
    }
  }
  return any ? best : 0.f;
}

void ResolveGroundY(engine::scene::World& world, engine::scene::NodeId player) {
  if (!world.valid(player)) {
    return;
  }
  auto t = world.local_transform(player);
  const float ground = GroundTopY(world, t.position.x, t.position.z);
  if (t.position.y < ground) {
    t.position.y = ground;
    world.set_local_transform(player, t);
  }
}

void MovePlayerOnGround(engine::scene::World& world, engine::scene::NodeId player, float yaw,
                        const engine::Vec3& wish, float speed, float dt) {
  if (!world.valid(player) || dt <= 0.f) {
    return;
  }
  const engine::Vec3 fwd =
      engine::Quat::FromEulerYxz(yaw, 0.f, 0.f).Rotate(engine::Vec3{0.f, 0.f, -1.f});
  const engine::Vec3 right =
      engine::Quat::FromEulerYxz(yaw, 0.f, 0.f).Rotate(engine::Vec3{1.f, 0.f, 0.f});
  engine::Vec3 delta = fwd * wish.z + right * wish.x;
  if (delta.length_squared() > 1e-8f) {
    delta = engine::Normalize(delta) * (speed * dt);
  }
  auto t = world.local_transform(player);
  t.position += delta;
  world.set_local_transform(player, t);
  ResolveGroundY(world, player);
}

void FollowPlayerCamera(engine::render::Camera* cam, const engine::Vec3& player_pos) {
  if (!cam) {
    return;
  }
  const engine::Vec3 eye = player_pos + engine::Vec3{0.f, 1.6f, 0.f};
  const engine::Vec3 fwd =
      engine::Quat::FromEulerYxz(cam->yaw, cam->pitch, 0.f).Rotate(engine::Vec3{0.f, 0.f, -1.f});
  cam->position = eye - fwd * 5.f;
}

void WriteInstanceOverride(const engine::scene::World& world, engine::scene::NodeId id, NodeMeta* m) {
  if (!m || !world.valid(id)) {
    return;
  }
  if (m->source_prefab.empty() && m->prefab_id.empty()) {
    return;
  }
  const auto t = world.local_transform(id);
  std::ostringstream o;
  o << "{\"x\":" << t.position.x << ",\"y\":" << t.position.y << ",\"z\":" << t.position.z
    << ",\"sx\":" << t.scale.x << ",\"sy\":" << t.scale.y << ",\"sz\":" << t.scale.z
    << ",\"visible\":" << (world.visible(id) ? "true" : "false");
  if (!m->material_id.empty()) {
    o << ",\"material\":\"" << EscapeMeta(m->material_id) << '"';
  }
  if (m->has_light) {
    o << ",\"kind\":" << m->light_kind << ",\"range\":" << m->light_range
      << ",\"intensity\":" << m->light_intensity << ",\"color\":[" << m->light_r << ',' << m->light_g
      << ',' << m->light_b << ']';
  }
  if (!m->script_fields.empty()) {
    o << ",\"fields\":" << m->script_fields;
  }
  o << '}';
  m->override_json = o.str();
}

}  // namespace editor
