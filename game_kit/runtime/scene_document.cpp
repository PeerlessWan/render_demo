#include "game_kit/scene_document.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>
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
  if (const auto* mesh = world.mesh(id)) {
    SceneComponent c;
    c.type = "MeshRenderer";
    c.mesh = mesh->mesh_id;
    n.components.push_back(std::move(c));
  }
  doc.nodes.push_back(std::move(n));
  const std::string self = doc.nodes.back().id;
  for (engine::scene::NodeId c : world.children(id)) {
    WalkCapture(world, c, self, doc);
  }
}

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
    } else if (key == "script") {
      c.script = p.ParseString();
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
  out << "{\n  \"format_version\": " << doc.format_version << ",\n  \"nodes\": [\n";
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
    out << ",\"components\":[";
    for (std::size_t c = 0; c < n.components.size(); ++c) {
      const auto& comp = n.components[c];
      out << "{\"type\":\"" << Escape(comp.type) << "\"";
      if (!comp.mesh.empty()) {
        out << ",\"mesh\":\"" << Escape(comp.mesh) << "\",\"materials\":[]";
      }
      if (!comp.script.empty()) {
        out << ",\"script\":\"" << Escape(comp.script) << "\"";
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
  if (doc.format_version != 1) {
    return engine::Result<SceneDocument>::Fail("unsupported format_version");
  }
  return engine::Result<SceneDocument>::Ok(std::move(doc));
}

SceneDocument CaptureWorld(const engine::scene::World& world) {
  SceneDocument doc;
  for (engine::scene::NodeId r : world.roots()) {
    WalkCapture(world, r, {}, doc);
  }
  return doc;
}

engine::Status ApplyWorld(engine::scene::World& world, const SceneDocument& doc) {
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
      for (const auto& c : n.components) {
        if (c.type == "MeshRenderer") {
          engine::scene::MeshRenderer mesh;
          mesh.mesh_id = c.mesh.empty() ? "cube" : c.mesh;
          world.set_mesh(id, mesh);
        }
      }
      ids[n.id.empty() ? std::to_string(id) : n.id] = id;
      done[i] = 1;
      ++placed;
      progress = true;
    }
    if (!progress) {
      return engine::Status::Fail("scene parent cycle or missing parent");
    }
  }
  return engine::Status::Ok();
}

void ClearWorld(engine::scene::World& world) {
  const auto roots = world.roots();
  for (auto r : roots) {
    (void)world.DestroyNode(r);
  }
}

}  // namespace game_kit
