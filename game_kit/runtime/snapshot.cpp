#include "game_kit/snapshot.h"

#include "game_kit/entity.h"
#include "game_kit/runtime.h"
#include "game_kit/save.h"
#include "game_kit/script_component.h"

#include "engine/scene/world.h"

#include <cstdlib>
#include <sstream>

namespace game_kit {
namespace {

std::string Escape(std::string_view s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
    }
    if (c == '\n') {
      o += "\\n";
      continue;
    }
    o.push_back(c);
  }
  return o;
}

std::string JoinTags(const std::vector<std::string>& tags) {
  std::string o;
  for (std::size_t i = 0; i < tags.size(); ++i) {
    if (i) {
      o += ',';
    }
    o += tags[i];
  }
  return o;
}

std::vector<std::string> SplitTags(std::string_view s) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < s.size()) {
    const auto c = s.find(',', i);
    const auto part = s.substr(i, (c == std::string_view::npos ? s.size() : c) - i);
    if (!part.empty()) {
      out.emplace_back(part);
    }
    if (c == std::string_view::npos) {
      break;
    }
    i = c + 1;
  }
  return out;
}

bool ParseQuoted(std::string_view json, std::string_view key, std::string* out) {
  const std::string pat = std::string("\"") + std::string(key) + "\":\"";
  const auto i = json.find(pat);
  if (i == std::string_view::npos || !out) {
    return false;
  }
  auto start = i + pat.size();
  std::string v;
  for (std::size_t k = start; k < json.size(); ++k) {
    if (json[k] == '\\' && k + 1 < json.size()) {
      ++k;
      v.push_back(json[k] == 'n' ? '\n' : json[k]);
      continue;
    }
    if (json[k] == '"') {
      break;
    }
    v.push_back(json[k]);
  }
  *out = std::move(v);
  return true;
}

}  // namespace

WorldSnapshot CaptureSnapshot(const EntityWorld& entities, const engine::scene::World* world,
                              std::string_view level) {
  WorldSnapshot snap;
  snap.format_version = kSnapshotFormatCurrent;
  snap.level = std::string(level);
  for (const auto& e : entities.all()) {
    EntitySnap s;
    s.name = e.name;
    s.ai = std::string(ToString(e.ai.state));
    s.active = e.active;
    s.tags = e.tags;
    s.script_path = e.script_path;
    if (world && e.node != engine::scene::kInvalidNode && world->valid(e.node)) {
      const auto t = world->local_transform(e.node);
      s.position = t.position;
      s.rotation = t.rotation;
      s.scale = t.scale;
    }
    snap.entities.push_back(std::move(s));
  }
  return snap;
}

WorldSnapshot CaptureSnapshot(const GameRuntime& rt, const engine::scene::World* world) {
  auto snap = CaptureSnapshot(rt.entities(), world, rt.levels().current());
  for (auto& s : snap.entities) {
    const Entity* e = nullptr;
    for (const auto& ent : rt.entities().all()) {
      if (ent.name == s.name) {
        e = &ent;
        break;
      }
    }
    if (!e) {
      continue;
    }
    for (const auto& c : rt.scripts().all()) {
      if (c.node == e->node || c.entity == e->id) {
        s.persist = c.vm.DumpPersist();
        if (s.script_path.empty()) {
          s.script_path = c.path;
        }
        break;
      }
    }
  }
  return snap;
}

engine::Status ApplySnapshot(GameRuntime& rt, engine::scene::World* world, const WorldSnapshot& snap) {
  for (const auto& s : snap.entities) {
    Entity* e = rt.entities().FindByName(s.name);
    if (!e) {
      const auto id = rt.entities().Create(s.name);
      e = rt.entities().Get(id);
    }
    if (!e) {
      continue;
    }
    e->active = s.active;
    e->ai.Set(ParseAiState(s.ai));
    e->script_path = s.script_path;
    e->tags = s.tags;
    if (world && e->node != engine::scene::kInvalidNode && world->valid(e->node)) {
      auto t = world->local_transform(e->node);
      t.position = s.position;
      t.rotation = s.rotation;
      t.scale = s.scale;
      world->set_local_transform(e->node, t);
    }
    if (!s.persist.empty()) {
      if (auto* sc = rt.scripts().FindByNode(e->node)) {
        (void)sc->vm.RestorePersist(s.persist);
      }
    }
  }
  return engine::Status::Ok();
}

std::string SnapshotToJson(const WorldSnapshot& snap) {
  std::ostringstream out;
  out << "{\"v\":" << snap.format_version << ",\"tick\":" << snap.tick << ",\"level\":\""
      << Escape(snap.level) << "\",\"entities\":[";
  for (std::size_t i = 0; i < snap.entities.size(); ++i) {
    const auto& e = snap.entities[i];
    out << "{\"name\":\"" << Escape(e.name) << "\",\"ai\":\"" << Escape(e.ai)
        << "\",\"active\":" << (e.active ? "true" : "false") << ",\"p\":[" << e.position.x << ','
        << e.position.y << ',' << e.position.z << "],\"r\":[" << e.rotation.x << ',' << e.rotation.y
        << ',' << e.rotation.z << ',' << e.rotation.w << "],\"s\":[" << e.scale.x << ',' << e.scale.y
        << ',' << e.scale.z << "],\"tags\":\"" << Escape(JoinTags(e.tags)) << "\",\"script\":\""
        << Escape(e.script_path) << "\",\"persist\":\"" << Escape(e.persist) << "\"}";
    if (i + 1 < snap.entities.size()) {
      out << ',';
    }
  }
  out << "]}";
  return out.str();
}

engine::Result<WorldSnapshot> SnapshotFromJson(std::string_view json) {
  WorldSnapshot snap;
  snap.format_version = 0;
  const auto vpos = json.find("\"v\":");
  if (vpos != std::string_view::npos) {
    snap.format_version = std::atoi(json.data() + vpos + 4);
  }
  const auto tpos = json.find("\"tick\":");
  if (tpos != std::string_view::npos) {
    snap.tick = static_cast<std::uint32_t>(std::atoi(json.data() + tpos + 7));
  }
  ParseQuoted(json, "level", &snap.level);
  std::size_t pos = 0;
  while (true) {
    const auto npos = json.find("\"name\":\"", pos);
    if (npos == std::string_view::npos) {
      break;
    }
    EntitySnap e;
    auto start = npos + 8;
    auto end = json.find('"', start);
    e.name = std::string(json.substr(start, end - start));
    const auto chunk_end = json.find("\"name\":\"", npos + 8);
    const auto chunk = json.substr(npos, chunk_end == std::string_view::npos ? json.size() - npos
                                                                            : chunk_end - npos);
    ParseQuoted(chunk, "ai", &e.ai);
    ParseQuoted(chunk, "script", &e.script_path);
    ParseQuoted(chunk, "persist", &e.persist);
    std::string tags;
    if (ParseQuoted(chunk, "tags", &tags)) {
      e.tags = SplitTags(tags);
    }
    const auto p = chunk.find("\"p\":[");
    if (p != std::string_view::npos) {
      start = p + 5;
      char* next = nullptr;
      const std::string nums(chunk.substr(start, 128));
      e.position.x = std::strtof(nums.c_str(), &next);
      e.position.y = next ? std::strtof(next + 1, &next) : 0.f;
      e.position.z = next ? std::strtof(next + 1, nullptr) : 0.f;
    }
    const auto r = chunk.find("\"r\":[");
    if (r != std::string_view::npos) {
      start = r + 5;
      char* next = nullptr;
      const std::string nums(chunk.substr(start, 128));
      e.rotation.x = std::strtof(nums.c_str(), &next);
      e.rotation.y = next ? std::strtof(next + 1, &next) : 0.f;
      e.rotation.z = next ? std::strtof(next + 1, &next) : 0.f;
      e.rotation.w = next ? std::strtof(next + 1, nullptr) : 1.f;
    }
    const auto sc = chunk.find("\"s\":[");
    if (sc != std::string_view::npos) {
      start = sc + 5;
      char* next = nullptr;
      const std::string nums(chunk.substr(start, 128));
      e.scale.x = std::strtof(nums.c_str(), &next);
      e.scale.y = next ? std::strtof(next + 1, &next) : 1.f;
      e.scale.z = next ? std::strtof(next + 1, nullptr) : 1.f;
    }
    const auto act = chunk.find("\"active\":");
    if (act != std::string_view::npos) {
      e.active = chunk.substr(act + 9).starts_with("true");
    }
    snap.entities.push_back(std::move(e));
    pos = end + 1;
  }
  return engine::Result<WorldSnapshot>::Ok(std::move(snap));
}

engine::Status SaveSnapshot(SaveSlots& saves, int slot, const WorldSnapshot& snap) {
  return saves.Write(slot, SnapshotToJson(snap), kSaveFormatCurrent);
}

engine::Result<WorldSnapshot> LoadSnapshot(const SaveSlots& saves, int slot) {
  auto r = saves.Read(slot);
  if (!r) {
    return engine::Result<WorldSnapshot>::Fail(r.status().message());
  }
  return SnapshotFromJson(r.value().payload);
}

void LoopbackReplicator::PushDiff(const WorldSnapshot& now) {
  WorldSnapshot d;
  d.level = now.level;
  d.format_version = now.format_version;
  d.tick = ++tick_;
  for (const auto& e : now.entities) {
    bool changed = !has_last_;
    if (!changed) {
      changed = true;
      for (const auto& old : last_.entities) {
        if (old.name != e.name) {
          continue;
        }
        changed = old.ai != e.ai || old.active != e.active || old.script_path != e.script_path ||
                  (old.position - e.position).length_squared() > 0.0001f ||
                  (old.scale - e.scale).length_squared() > 0.0001f;
        break;
      }
    }
    if (changed) {
      d.entities.push_back(e);
    }
  }
  pending_ = std::move(d);
  has_ = !pending_.entities.empty() || pending_.level != last_.level;
  last_ = now;
  last_.tick = tick_;
  has_last_ = true;
}

void ReplicationSession::ServerCapture(const GameRuntime& rt, const engine::scene::World* world) {
  auto snap = CaptureSnapshot(rt, world);
  transport_.Push(std::move(snap));
  target_ = transport_.Pull();
  tick_ = target_.tick;
  has_ = true;
}

void ReplicationSession::ClientApply(GameRuntime& rt, engine::scene::World* world, float dt) {
  if (!has_ || !world) {
    return;
  }
  const float a = dt <= 0.f ? 1.f : (dt * 12.f > 1.f ? 1.f : dt * 12.f);
  for (const auto& s : target_.entities) {
    Entity* e = rt.entities().FindByName(s.name);
    if (!e) {
      const auto id = rt.entities().Create(s.name);
      e = rt.entities().Get(id);
    }
    if (!e) {
      continue;
    }
    e->active = s.active;
    e->ai.Set(ParseAiState(s.ai));
    if (e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
      continue;
    }
    auto t = world->local_transform(e->node);
    t.position = t.position * (1.f - a) + s.position * a;
    t.scale = s.scale;
    t.rotation = s.rotation;
    world->set_local_transform(e->node, t);
  }
}

}  // namespace game_kit
