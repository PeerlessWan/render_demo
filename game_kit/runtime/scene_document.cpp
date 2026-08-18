#include "game_kit/scene_document.h"

#include "game_kit/entity.h"
#include "game_kit/runtime.h"
#include "game_kit/script_fields.h"

#include "engine/core/host_api.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>

namespace game_kit {
namespace {

std::string Escape(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"') {
      o.push_back('\\');
    }
    o.push_back(c);
  }
  return o;
}

void WriteTransform(std::ostream& out, const engine::scene::Transform& t) {
  out << "{\"t\":[" << t.position.x << ',' << t.position.y << ',' << t.position.z << "],\"r\":["
      << t.rotation.x << ',' << t.rotation.y << ',' << t.rotation.z << ',' << t.rotation.w
      << "],\"s\":[" << t.scale.x << ',' << t.scale.y << ',' << t.scale.z << "]}";
}

}  // namespace

void ApplyNodeComponents(engine::scene::World& world, engine::scene::NodeId id, const SceneNode& n) {
  if (!world.valid(id)) {
    return;
  }
  for (const auto& c : n.components) {
    if (c.type == "MeshRenderer") {
      engine::scene::MeshRenderer mesh;
      mesh.mesh_id = c.mesh.empty() ? "cube" : c.mesh;
      mesh.material_id = c.material;
      if (mesh.mesh_id == "ground") {
        mesh.never_cull = true;
        mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
      }
      if (mesh.mesh_id == "terrain") {
        mesh.never_cull = true;
      }
      world.set_mesh(id, mesh);
    } else if (c.type == "Light") {
      engine::scene::LightComponent L;
      L.kind = c.kind;
      L.range = c.range;
      L.intensity = c.intensity;
      L.color = {c.color_r, c.color_g, c.color_b};
      world.set_light(id, L);
    } else if (c.type == "Camera") {
      engine::scene::CameraComponent cam;
      cam.active = c.active;
      cam.fovy_rad = c.fovy;
      world.set_camera(id, cam);
    } else if (c.type == "Collider") {
      engine::scene::ColliderComponent col;
      col.hx = c.hx;
      col.hy = c.hy;
      col.hz = c.hz;
      world.set_collider(id, col);
    } else if (c.type == "Sprite" || c.type == "Tilemap") {
      engine::scene::SpriteComponent spr;
      spr.atlas_id = c.atlas.empty() ? "tiles" : c.atlas;
      spr.gid = c.gid;
      world.set_sprite(id, spr);
    }
  }
}

void CaptureNodeComponents(const engine::scene::World& world, engine::scene::NodeId id,
                           SceneNode* n) {
  if (!n || !world.valid(id)) {
    return;
  }
  n->components.clear();
  if (const auto* mesh = world.mesh(id)) {
    SceneComponent c;
    c.type = "MeshRenderer";
    c.mesh = mesh->mesh_id;
    c.material = mesh->material_id;
    n->components.push_back(std::move(c));
  }
  if (const auto* L = world.light(id)) {
    SceneComponent c;
    c.type = "Light";
    c.kind = L->kind;
    c.range = L->range;
    c.intensity = L->intensity;
    c.color_r = L->color.x;
    c.color_g = L->color.y;
    c.color_b = L->color.z;
    n->components.push_back(std::move(c));
  }
  if (const auto* cam = world.camera(id)) {
    SceneComponent c;
    c.type = "Camera";
    c.active = cam->active;
    c.fovy = cam->fovy_rad;
    n->components.push_back(std::move(c));
  }
  if (const auto* col = world.collider(id)) {
    SceneComponent c;
    c.type = "Collider";
    c.hx = col->hx;
    c.hy = col->hy;
    c.hz = col->hz;
    n->components.push_back(std::move(c));
  }
  if (const auto* spr = world.sprite(id)) {
    SceneComponent c;
    c.type = "Sprite";
    c.atlas = spr->atlas_id;
    c.gid = spr->gid;
    n->components.push_back(std::move(c));
  }
}

void WalkCapture(const engine::scene::World& world, engine::scene::NodeId id,
                 const std::string& parent, SceneDocument& doc) {
  if (!world.valid(id)) {
    return;
  }
  SceneNode n;
  n.id = std::to_string(id);
  n.name = world.name(id);
  n.parent = parent;
  n.transform = world.local_transform(id);
  n.visible = world.visible(id);
  CaptureNodeComponents(world, id, &n);
  doc.nodes.push_back(std::move(n));
  const std::string self = doc.nodes.back().id;
  for (engine::scene::NodeId c : world.children(id)) {
    WalkCapture(world, c, self, doc);
  }
}

namespace {

struct Parser {
  std::string_view t;
  std::size_t i = 0;

  void Skip() {
    while (i < t.size() && std::isspace(static_cast<unsigned char>(t[i]))) {
      ++i;
    }
  }

  bool Consume(char c) {
    Skip();
    if (i < t.size() && t[i] == c) {
      ++i;
      return true;
    }
    return false;
  }

  bool Expect(char c) { return Consume(c); }

  std::string ParseString() {
    Skip();
    if (i >= t.size() || t[i] != '"') {
      return {};
    }
    ++i;
    std::string s;
    while (i < t.size() && t[i] != '"') {
      if (t[i] == '\\' && i + 1 < t.size()) {
        ++i;
      }
      s.push_back(t[i++]);
    }
    if (i < t.size() && t[i] == '"') {
      ++i;
    }
    return s;
  }

  double ParseNumber() {
    Skip();
    char* end = nullptr;
    const double v = std::strtod(t.data() + i, &end);
    if (end) {
      i = static_cast<std::size_t>(end - t.data());
    }
    return v;
  }

  bool ParseBool() {
    Skip();
    if (t.substr(i, 4) == "true") {
      i += 4;
      return true;
    }
    if (t.substr(i, 5) == "false") {
      i += 5;
      return false;
    }
    return false;
  }

  bool ParseNull() {
    Skip();
    if (t.substr(i, 4) == "null") {
      i += 4;
      return true;
    }
    return false;
  }

  void SkipValue() {
    Skip();
    if (i >= t.size()) {
      return;
    }
    if (t[i] == '"') {
      ParseString();
      return;
    }
    if (t[i] == '{') {
      int depth = 0;
      do {
        if (t[i] == '{') {
          ++depth;
        } else if (t[i] == '}') {
          --depth;
        } else if (t[i] == '"') {
          ParseString();
          continue;
        }
        ++i;
      } while (i < t.size() && depth > 0);
      return;
    }
    if (t[i] == '[') {
      int depth = 0;
      do {
        if (t[i] == '[') {
          ++depth;
        } else if (t[i] == ']') {
          --depth;
        } else if (t[i] == '"') {
          ParseString();
          continue;
        }
        ++i;
      } while (i < t.size() && depth > 0);
      return;
    }
    ParseNumber();
  }
};

void ParseVec3(Parser& p, engine::Vec3& v) {
  if (!p.Expect('[')) {
    return;
  }
  v.x = static_cast<float>(p.ParseNumber());
  p.Consume(',');
  v.y = static_cast<float>(p.ParseNumber());
  p.Consume(',');
  v.z = static_cast<float>(p.ParseNumber());
  p.Expect(']');
}

void ParseQuat(Parser& p, engine::Quat& q) {
  if (!p.Expect('[')) {
    return;
  }
  q.x = static_cast<float>(p.ParseNumber());
  p.Consume(',');
  q.y = static_cast<float>(p.ParseNumber());
  p.Consume(',');
  q.z = static_cast<float>(p.ParseNumber());
  p.Consume(',');
  q.w = static_cast<float>(p.ParseNumber());
  p.Expect(']');
}

SceneComponent ParseComponent(Parser& p) {
  SceneComponent c;
  if (!p.Expect('{')) {
    return c;
  }
  while (!p.Consume('}')) {
    const std::string key = p.ParseString();
    p.Expect(':');
    if (key == "type") {
      c.type = p.ParseString();
    } else if (key == "mesh") {
      c.mesh = p.ParseString();
    } else if (key == "material") {
      c.material = p.ParseString();
    } else if (key == "materials") {
      if (p.Expect('[')) {
        bool first = true;
        while (!p.Consume(']')) {
          const std::string m = p.ParseString();
          if (first && c.material.empty()) {
            c.material = m;
            first = false;
          }
          p.Consume(',');
        }
      }
    } else if (key == "script") {
      c.script = p.ParseString();
    } else if (key == "kind") {
      if (p.t.size() > p.i && p.t[p.i] == '"') {
        const auto s = p.ParseString();
        c.kind = (s == "spot") ? 1 : (s == "directional" ? 2 : 0);
      } else {
        c.kind = static_cast<int>(p.ParseNumber());
      }
    } else if (key == "range") {
      c.range = static_cast<float>(p.ParseNumber());
    } else if (key == "intensity") {
      c.intensity = static_cast<float>(p.ParseNumber());
    } else if (key == "color") {
      if (p.Expect('[')) {
        c.color_r = static_cast<float>(p.ParseNumber());
        p.Consume(',');
        c.color_g = static_cast<float>(p.ParseNumber());
        p.Consume(',');
        c.color_b = static_cast<float>(p.ParseNumber());
        p.Consume(']');
      }
    } else if (key == "active") {
      c.active = p.ParseBool();
    } else if (key == "fovy" || key == "fovy_rad") {
      c.fovy = static_cast<float>(p.ParseNumber());
    } else if (key == "hx") {
      c.hx = static_cast<float>(p.ParseNumber());
    } else if (key == "hy") {
      c.hy = static_cast<float>(p.ParseNumber());
    } else if (key == "hz") {
      c.hz = static_cast<float>(p.ParseNumber());
    } else if (key == "gid") {
      c.gid = static_cast<int>(p.ParseNumber());
    } else if (key == "atlas") {
      c.atlas = p.ParseString();
    } else if (key == "fields") {
      const std::size_t start = p.i;
      p.SkipValue();
      c.fields_json = std::string(p.t.substr(start, p.i - start));
    } else if (key == "tag") {
      c.script = p.ParseString();
    } else if (key == "extra") {
      const std::size_t start = p.i;
      p.SkipValue();
      c.extra_json = std::string(p.t.substr(start, p.i - start));
    } else {
      p.SkipValue();
    }
    p.Consume(',');
  }
  return c;
}

SceneNode ParseNode(Parser& p) {
  SceneNode n;
  if (!p.Expect('{')) {
    return n;
  }
  while (!p.Consume('}')) {
    const std::string key = p.ParseString();
    p.Expect(':');
    if (key == "id") {
      n.id = p.ParseString();
    } else if (key == "name") {
      n.name = p.ParseString();
    } else if (key == "parent") {
      if (p.ParseNull()) {
        n.parent.clear();
      } else {
        n.parent = p.ParseString();
      }
    } else if (key == "visible") {
      n.visible = p.ParseBool();
    } else if (key == "transform") {
      if (p.Expect('{')) {
        while (!p.Consume('}')) {
          const std::string tk = p.ParseString();
          p.Expect(':');
          if (tk == "t") {
            ParseVec3(p, n.transform.position);
          } else if (tk == "r") {
            ParseQuat(p, n.transform.rotation);
          } else if (tk == "s") {
            ParseVec3(p, n.transform.scale);
          } else {
            p.SkipValue();
          }
          p.Consume(',');
        }
      }
    } else if (key == "components") {
      if (p.Expect('[')) {
        while (!p.Consume(']')) {
          n.components.push_back(ParseComponent(p));
          p.Consume(',');
        }
      }
    } else if (key == "prefab_id") {
      n.prefab_id = p.ParseString();
    } else if (key == "script_path") {
      n.script_path = p.ParseString();
    } else if (key == "extra") {
      const std::size_t start = p.i;
      p.SkipValue();
      n.extra_json = std::string(p.t.substr(start, p.i - start));
    } else if (key == "override") {
      const std::size_t start = p.i;
      p.SkipValue();
      n.override_json = std::string(p.t.substr(start, p.i - start));
    } else {
      p.SkipValue();
    }
    p.Consume(',');
  }
  return n;
}

}  // namespace

engine::Status SaveSceneDocument(const SceneDocument& doc, const std::filesystem::path& path) {
  std::ofstream out(path);
  if (!out) {
    return engine::Status::Fail("cannot write scene: " + path.string());
  }
  out << "{\n  \"format_version\": " << doc.format_version;
  if (!doc.host_api_hint.empty()) {
    out << ",\n  \"host_api_hint\": \"" << Escape(doc.host_api_hint) << "\"";
  }
  out << ",\n  \"nodes\": [\n";
  for (std::size_t i = 0; i < doc.nodes.size(); ++i) {
    const auto& n = doc.nodes[i];
    out << "    {\"id\":\"" << Escape(n.id) << "\",\"name\":\"" << Escape(n.name) << "\",\"parent\":";
    if (n.parent.empty()) {
      out << "null";
    } else {
      out << '"' << Escape(n.parent) << '"';
    }
    out << ",\"transform\":";
    WriteTransform(out, n.transform);
    out << ",\"visible\":" << (n.visible ? "true" : "false");
    if (!n.prefab_id.empty()) {
      out << ",\"prefab_id\":\"" << Escape(n.prefab_id) << "\"";
    }
    if (!n.script_path.empty()) {
      out << ",\"script_path\":\"" << Escape(n.script_path) << "\"";
    }
    if (!n.extra_json.empty()) {
      out << ",\"extra\":" << n.extra_json;
    }
    if (!n.override_json.empty()) {
      out << ",\"override\":" << n.override_json;
    }
    out << ",\"components\":[";
    for (std::size_t c = 0; c < n.components.size(); ++c) {
      const auto& comp = n.components[c];
      out << "{\"type\":\"" << Escape(comp.type) << "\"";
      if (!comp.mesh.empty()) {
        out << ",\"mesh\":\"" << Escape(comp.mesh) << "\"";
      }
      if (!comp.material.empty()) {
        out << ",\"material\":\"" << Escape(comp.material) << "\",\"materials\":[\""
            << Escape(comp.material) << "\"]";
      }
      if (!comp.script.empty()) {
        out << ",\"script\":\"" << Escape(comp.script) << "\"";
      }
      if (comp.type == "Light") {
        out << ",\"kind\":" << comp.kind << ",\"range\":" << comp.range
            << ",\"intensity\":" << comp.intensity << ",\"color\":[" << comp.color_r << ","
            << comp.color_g << "," << comp.color_b << "]";
      }
      if (comp.type == "Camera") {
        out << ",\"active\":" << (comp.active ? "true" : "false") << ",\"fovy\":" << comp.fovy;
      }
      if (comp.type == "Collider") {
        out << ",\"hx\":" << comp.hx << ",\"hy\":" << comp.hy << ",\"hz\":" << comp.hz;
      }
      if (comp.type == "Sprite" || comp.type == "Tilemap") {
        out << ",\"gid\":" << comp.gid;
        if (!comp.atlas.empty()) {
          out << ",\"atlas\":\"" << Escape(comp.atlas) << "\"";
        }
      }
      if (!comp.fields_json.empty()) {
        out << ",\"fields\":" << comp.fields_json;
      }
      if (comp.type == "GameTag" && !comp.script.empty()) {
        out << ",\"tag\":\"" << Escape(comp.script) << "\"";
      }
      if (!comp.extra_json.empty()) {
        out << ",\"extra\":" << comp.extra_json;
      }
      out << "}";
      if (c + 1 < n.components.size()) {
        out << ',';
      }
    }
    out << "]}";
    if (i + 1 < doc.nodes.size()) {
      out << ',';
    }
    out << '\n';
  }
  out << "  ]";
  if (!doc.extensions_json.empty()) {
    out << ",\n  \"extensions\":" << doc.extensions_json;
  }
  out << "\n}\n";
  return engine::Status::Ok();
}

engine::Result<SceneDocument> LoadSceneDocument(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return engine::Result<SceneDocument>::Fail("cannot read scene: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  Parser p;
  p.t = text;
  SceneDocument doc;
  if (!p.Expect('{')) {
    return engine::Result<SceneDocument>::Fail("scene json root");
  }
  while (!p.Consume('}')) {
    const std::string key = p.ParseString();
    p.Expect(':');
    if (key == "format_version") {
      doc.format_version = static_cast<int>(p.ParseNumber());
    } else if (key == "host_api_hint") {
      doc.host_api_hint = p.ParseString();
    } else if (key == "nodes") {
      if (p.Expect('[')) {
        while (!p.Consume(']')) {
          doc.nodes.push_back(ParseNode(p));
          p.Consume(',');
        }
      }
    } else if (key == "extensions") {
      const std::size_t start = p.i;
      p.SkipValue();
      doc.extensions_json = std::string(p.t.substr(start, p.i - start));
    } else {
      p.SkipValue();
    }
    p.Consume(',');
  }
  if (doc.format_version != 1 && doc.format_version != 2 && doc.format_version != 3) {
    return engine::Result<SceneDocument>::Fail("unsupported format_version");
  }
  return engine::Result<SceneDocument>::Ok(std::move(doc));
}

SceneDocument CaptureWorld(const engine::scene::World& world) {
  SceneDocument doc;
  doc.format_version = kSceneFormatCurrent;
  doc.host_api_hint = engine::kHostApiVersion;
  for (engine::scene::NodeId r : world.roots()) {
    WalkCapture(world, r, {}, doc);
  }
  return doc;
}

SceneDocument CaptureWorld(const engine::scene::World& world, const GameRuntime& rt) {
  SceneDocument doc = CaptureWorld(world);
  for (auto& n : doc.nodes) {
    const auto nid = static_cast<engine::scene::NodeId>(std::strtoul(n.id.c_str(), nullptr, 10));
    const Entity* e = rt.entities().FindByNode(nid);
    if (!e) {
      continue;
    }
    if (n.script_path.empty() && !e->script_path.empty()) {
      n.script_path = e->script_path;
    }
    bool has_script = false;
    for (const auto& c : n.components) {
      if (c.type == "Script") {
        has_script = true;
        break;
      }
    }
    if (!has_script && !n.script_path.empty()) {
      SceneComponent sc;
      sc.type = "Script";
      sc.script = n.script_path;
      n.components.push_back(std::move(sc));
    }
    for (const auto& tag : e->tags) {
      SceneComponent g;
      g.type = "GameTag";
      g.script = tag;
      n.components.push_back(std::move(g));
    }
  }
  return doc;
}

void BindSceneScripts(GameRuntime& rt, engine::scene::World& world, const SceneDocument& doc,
                      const std::unordered_map<std::string, engine::scene::NodeId>& ids) {
  rt.set_world(&world);
  rt.scripts().AttachHost(&world, &rt);
  for (const auto& n : doc.nodes) {
    auto it = ids.find(n.id.empty() ? n.name : n.id);
    if (it == ids.end()) {
      continue;
    }
    const auto nid = it->second;
    std::string name = n.name.empty() ? world.name(nid) : n.name;
    if (name.empty()) {
      name = "node_" + std::to_string(nid);
    }
    Entity* e = rt.entities().FindByNode(nid);
    if (!e) {
      const auto eid = rt.entities().Create(name, nid);
      e = rt.entities().Get(eid);
    }
    if (!e) {
      continue;
    }
    std::string path = n.script_path;
    std::string fields;
    for (const auto& c : n.components) {
      if (c.type == "Script" && !c.script.empty()) {
        path = c.script;
        fields = c.fields_json;
      }
      if (c.type == "GameTag" && !c.script.empty()) {
        e->AddTag(c.script);
      }
    }
    if (path.empty()) {
      continue;
    }
    e->script_path = path;
    const auto resolved = rt.ResolveScriptPath(path);
    const auto sid = rt.scripts().Attach(nid, resolved.string());
    if (auto* sc = rt.scripts().Get(sid)) {
      sc->entity = e->id;
      (void)rt.scripts().LoadFromDisk(*sc);
      std::ifstream in(resolved, std::ios::binary);
      std::string src;
      if (in) {
        src.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      const auto blob = MergeExportsAndPersist(src, fields);
      if (!blob.empty()) {
        (void)sc->vm.RestorePersist(blob);
      }
    }
  }
}

engine::Status ApplyWorld(engine::scene::World& world, const SceneDocument& doc,
                          std::unordered_map<std::string, engine::scene::NodeId>* out_ids) {
  std::unordered_map<std::string, engine::scene::NodeId> ids;
  // Parents before children: multiple passes.
  std::vector<char> done(doc.nodes.size(), 0);
  std::size_t placed = 0;
  while (placed < doc.nodes.size()) {
    bool progress = false;
    for (std::size_t i = 0; i < doc.nodes.size(); ++i) {
      if (done[i]) {
        continue;
      }
      const auto& n = doc.nodes[i];
      engine::scene::NodeId parent = engine::scene::kInvalidNode;
      if (!n.parent.empty()) {
        auto it = ids.find(n.parent);
        if (it == ids.end()) {
          continue;
        }
        parent = it->second;
      }
      const auto id = world.CreateNode(n.name, parent);
      world.set_local_transform(id, n.transform);
      world.set_visible(id, n.visible);
      ApplyNodeComponents(world, id, n);
      ids[n.id.empty() ? std::to_string(id) : n.id] = id;
      done[i] = 1;
      ++placed;
      progress = true;
    }
    if (!progress) {
      return engine::Status::Fail("scene parent cycle or missing parent");
    }
  }
  if (out_ids) {
    *out_ids = std::move(ids);
  }
  return engine::Status::Ok();
}

void ClearWorld(engine::scene::World& world) {
  const auto roots = world.roots();
  for (auto r : roots) {
    (void)world.DestroyNode(r);
  }
}

engine::Status ValidateSceneDocument(const SceneDocument& doc) {
  if (doc.format_version < 1 || doc.format_version > kSceneFormatCurrent) {
    return engine::Status::Fail("unsupported scene format_version");
  }
  std::unordered_map<std::string, int> ids;
  for (const auto& n : doc.nodes) {
    const std::string key = n.id.empty() ? n.name : n.id;
    if (key.empty()) {
      return engine::Status::Fail("scene node missing id and name");
    }
    if (ids[key]++ > 0) {
      return engine::Status::Fail("duplicate scene node id");
    }
    for (const auto& c : n.components) {
      if (c.type.empty()) {
        return engine::Status::Fail("component missing type");
      }
    }
  }
  for (const auto& n : doc.nodes) {
    if (n.parent.empty()) {
      continue;
    }
    if (!ids.count(n.parent)) {
      return engine::Status::Fail("missing parent " + n.parent);
    }
    if ((!n.id.empty() && n.parent == n.id) || (n.id.empty() && n.parent == n.name)) {
      return engine::Status::Fail("node parent is self");
    }
  }
  std::vector<char> done(doc.nodes.size(), 0);
  std::size_t placed = 0;
  while (placed < doc.nodes.size()) {
    bool progress = false;
    for (std::size_t i = 0; i < doc.nodes.size(); ++i) {
      if (done[i]) {
        continue;
      }
      const auto& n = doc.nodes[i];
      if (!n.parent.empty()) {
        bool parent_done = false;
        for (std::size_t j = 0; j < doc.nodes.size(); ++j) {
          if (!done[j]) {
            continue;
          }
          const auto& p = doc.nodes[j];
          if (p.id == n.parent || (p.id.empty() && p.name == n.parent)) {
            parent_done = true;
            break;
          }
        }
        if (!parent_done) {
          continue;
        }
      }
      done[i] = 1;
      ++placed;
      progress = true;
    }
    if (!progress) {
      return engine::Status::Fail("scene parent cycle");
    }
  }
  return engine::Status::Ok();
}

}  // namespace game_kit
