#include "cmd/session.h"

#include "editing/light_bake.h"
#include "editing/ops.h"
#include "editing/snap.h"
#include "editing/terrain_edit.h"
#include "editing/tile_edit.h"
#include "io/dep_graph.h"
#include "io/scene_ext.h"

#include "game_kit/prefab.h"
#include "game_kit/scene_document.h"

#include "engine/assets/image_loader.h"
#include "engine/core/math.h"
#include "engine/rhi/i_device.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace editor {
namespace {

void EulerFromQuat(const engine::Quat& q, float* yaw, float* pitch, float* roll) {
  const float y = std::atan2(2.f * (q.w * q.y + q.x * q.z), 1.f - 2.f * (q.y * q.y + q.x * q.x));
  const float p = std::asin(std::clamp(2.f * (q.w * q.x - q.z * q.y), -1.f, 1.f));
  const float r = std::atan2(2.f * (q.w * q.z + q.x * q.y), 1.f - 2.f * (q.x * q.x + q.z * q.z));
  if (yaw) {
    *yaw = y;
  }
  if (pitch) {
    *pitch = p;
  }
  if (roll) {
    *roll = r;
  }
}

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

OpResult Fail(std::string msg) {
  OpResult r;
  r.ok = false;
  r.is_error = true;
  r.message = std::move(msg);
  return r;
}

OpResult Ok(std::string msg = {}) {
  OpResult r;
  r.message = std::move(msg);
  return r;
}

bool WritePpm(const std::filesystem::path& path, const std::vector<std::uint8_t>& rgba, int w, int h) {
  if (w <= 2 || h <= 2 || rgba.size() < static_cast<std::size_t>(w * h * 4)) {
    return false;
  }
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << "P3\n" << w << ' ' << h << "\n255\n";
  for (int i = 0; i < w * h; ++i) {
    const std::size_t o = static_cast<std::size_t>(i) * 4;
    out << static_cast<int>(rgba[o]) << ' ' << static_cast<int>(rgba[o + 1]) << ' '
        << static_cast<int>(rgba[o + 2]) << '\n';
  }
  return static_cast<bool>(out);
}

std::vector<game_kit::PrefabDocument> LoadCatalog(ContentBrowser* content) {
  std::vector<game_kit::PrefabDocument> cat;
  if (!content) {
    return cat;
  }
  for (const auto& it : content->items) {
    if (it.kind != ContentItem::Kind::Prefab) {
      continue;
    }
    auto p = game_kit::LoadPrefabDocument(it.path);
    if (p) {
      cat.push_back(std::move(p.value()));
    }
  }
  return cat;
}

bool Playing(const EditorSession& s) { return s.playing && *s.playing; }

bool Ready(const EditorSession& s) {
  return s.world && s.sel && s.undo && s.settings && s.meta && s.scene_path && s.playing && s.paused;
}

bool IsUnder(const engine::scene::World& world, engine::scene::NodeId id,
             engine::scene::NodeId ancestor) {
  auto p = FindParent(world, id);
  while (world.valid(p)) {
    if (p == ancestor) {
      return true;
    }
    p = FindParent(world, p);
  }
  return false;
}

void TickPlayFrame(EditorSession& s, float dt) {
  if (!s.rt || !s.scripts || !s.world) {
    return;
  }
  try {
    const bool was = s.rt->paused();
    s.rt->set_paused(false);
    s.rt->TickLogic(dt);
    s.scripts->Tick(dt);
    s.rt->set_paused(was);
    const auto player = FindNamed(*s.world, "player");
    if (s.world->valid(player)) {
      MovePlayerOnGround(*s.world, player, 0.f, {}, 5.5f, dt);
    }
  } catch (const std::exception& ex) {
    if (s.rt) {
      s.rt->set_paused(true);
    }
    (void)ex;
  } catch (...) {
    if (s.rt) {
      s.rt->set_paused(true);
    }
  }
}

}  // namespace

EditorSession EditorHost::Bind() {
  EditorSession s;
  s.world = &world;
  s.sel = &sel;
  s.undo = &undo;
  s.settings = &settings;
  s.meta = &meta;
  s.scene_path = &scene_path;
  s.playing = &playing;
  s.paused = &paused;
  s.rt = &rt;
  s.scripts = &scripts;
  s.scene_snap = &scene_snap;
  s.content = &content;
  s.edit_world = &world;
  s.play_world = &play_world;
  s.lint_json = &lint_json;
  s.screenshot_path = &screenshot_path;
  s.world = playing ? &play_world : &world;
  return s;
}

engine::scene::NodeId CreatePrimitive(engine::scene::World& world, std::string_view kind,
                                      const EditorSettings& settings) {
  if (kind == "empty") {
    return world.CreateNode("empty");
  }
  if (kind == "light") {
    const auto id = world.CreateNode("light");
    engine::scene::Transform t;
    t.position = {0.f, 2.f, 0.f};
    world.set_local_transform(id, t);
    engine::scene::LightComponent L;
    world.set_light(id, L);
    return id;
  }
  if (kind == "camera") {
    const auto id = world.CreateNode("camera");
    engine::scene::Transform t;
    t.position = {0.f, 1.6f, 4.f};
    world.set_local_transform(id, t);
    engine::scene::CameraComponent cam;
    cam.active = true;
    world.set_camera(id, cam);
    return id;
  }
  if (kind == "collider") {
    const auto id = world.CreateNode("collider");
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    world.set_mesh(id, mesh);
    engine::scene::ColliderComponent col;
    world.set_collider(id, col);
    return id;
  }
  if (kind == "sprite") {
    const auto id = world.CreateNode("sprite");
    engine::scene::SpriteComponent spr;
    world.set_sprite(id, spr);
    return id;
  }
  if (kind == "ground") {
    const auto id = world.CreateNode("ground");
    engine::scene::Transform t;
    t.scale = {8.f, 1.f, 8.f};
    if (settings.snap) {
      SnapTransform(&t, settings.grid);
    }
    world.set_local_transform(id, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "ground";
    mesh.never_cull = true;
    mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
    world.set_mesh(id, mesh);
    return id;
  }
  const bool player = kind == "player";
  const auto id = world.CreateNode(player ? "player" : "cube");
  engine::scene::Transform t;
  t.position = {0.f, 0.5f, 0.f};
  if (settings.snap) {
    SnapTransform(&t, settings.grid);
  }
  world.set_local_transform(id, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(id, mesh);
  return id;
}

std::string DumpSessionJson(const EditorSession& s) {
  if (!Ready(s)) {
    return "{}";
  }
  std::ostringstream out;
  out << "{\"playing\":" << (*s.playing ? "true" : "false") << ",\"paused\":"
      << (*s.paused ? "true" : "false") << ",\"scene_path\":\"" << Escape(s.scene_path->string())
      << "\",\"selected\":[";
  bool first_sel = true;
  for (auto id : s.sel->All()) {
    if (!s.world->valid(id)) {
      continue;
    }
    if (!first_sel) {
      out << ',';
    }
    first_sel = false;
    const auto n = s.world->name(id);
    out << '"' << Escape(n.empty() ? std::to_string(id) : n) << '"';
  }
  out << "],\"nodes\":[";
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(*s.world, &nodes);
  bool first = true;
  for (auto id : nodes) {
    if (!first) {
      out << ',';
    }
    first = false;
    const auto& t = s.world->local_transform(id);
    float yaw = 0.f;
    float pitch = 0.f;
    float roll = 0.f;
    EulerFromQuat(t.rotation, &yaw, &pitch, &roll);
    const auto p = FindParent(*s.world, id);
    std::string parent;
    if (s.world->valid(p)) {
      parent = s.world->name(p);
    }
    std::string mesh;
    if (const auto* m = s.world->mesh(id)) {
      mesh = m->mesh_id;
    }
    std::string script;
    auto it = s.meta->find(id);
    if (it != s.meta->end()) {
      script = it->second.script_path;
    }
    const auto n = s.world->name(id);
    out << "{\"id\":" << id << ",\"name\":\"" << Escape(n) << "\",\"parent\":\"" << Escape(parent)
        << "\",\"x\":" << t.position.x << ",\"y\":" << t.position.y << ",\"z\":" << t.position.z
        << ",\"yaw\":" << yaw << ",\"pitch\":" << pitch << ",\"roll\":" << roll << ",\"sx\":"
        << t.scale.x << ",\"sy\":" << t.scale.y << ",\"sz\":" << t.scale.z << ",\"mesh\":\""
        << Escape(mesh) << "\",\"visible\":" << (s.world->visible(id) ? "true" : "false")
        << ",\"script\":\"" << Escape(script) << "\"}";
  }
  out << "]}";
  return out.str();
}

OpResult ApplyOp(EditorSession s, const EditorOp& op) {
  if (!Ready(s)) {
    return Fail("session not bound");
  }
  auto& world = *s.world;
  const bool play = Playing(s);
  const bool struct_op = op.kind == EditorOp::Kind::Create || op.kind == EditorOp::Kind::Destroy ||
                         op.kind == EditorOp::Kind::Duplicate || op.kind == EditorOp::Kind::Open ||
                         op.kind == EditorOp::Kind::Save || op.kind == EditorOp::Kind::SetName ||
                         op.kind == EditorOp::Kind::SetMesh || op.kind == EditorOp::Kind::SetScript ||
                         op.kind == EditorOp::Kind::SetParent || op.kind == EditorOp::Kind::Place ||
                         op.kind == EditorOp::Kind::ApplyPrefab || op.kind == EditorOp::Kind::RevertPrefab;
  if (play && struct_op) {
    return Fail("blocked during play");
  }

  switch (op.kind) {
    case EditorOp::Kind::Dump: {
      auto r = Ok();
      r.json = DumpSessionJson(s);
      r.message = r.json;
      return r;
    }
    case EditorOp::Kind::Open: {
      const auto path = op.path.empty() ? *s.scene_path : std::filesystem::path(op.path);
      auto doc = game_kit::LoadSceneDocument(path);
      if (!doc) {
        return Fail(doc.status().message());
      }
      game_kit::ClearWorld(world);
      if (auto st = game_kit::ApplyWorld(world, doc.value()); !st) {
        return Fail(st.message());
      }
      s.sel->Clear();
      s.undo->Clear();
      s.settings->dirty = false;
      *s.scene_path = path;
      auto cap = game_kit::CaptureWorld(world);
      RestoreMeta(doc.value(), cap, s.meta);
      SyncWorldToMeta(world, s.meta);
      UnpackEditorExtensions(doc.value(), s.settings);
      return Ok("loaded " + path.string());
    }
    case EditorOp::Kind::Save: {
      auto doc = game_kit::CaptureWorld(world);
      StampMeta(&doc, *s.meta);
      PackEditorExtensions(*s.settings, &doc);
      const auto path = op.path.empty() ? *s.scene_path : std::filesystem::path(op.path);
      if (auto st = game_kit::SaveSceneDocument(doc, path); !st) {
        return Fail(st.message());
      }
      *s.scene_path = path;
      s.settings->dirty = false;
      return Ok("saved " + path.string());
    }
    case EditorOp::Kind::Create: {
      std::string kind = op.create_kind.empty() ? "cube" : op.create_kind;
      const auto id = CreatePrimitive(world, kind, *s.settings);
      std::vector<NodeSnap> snaps;
      CaptureSubtree(world, id, *s.meta, &snaps);
      s.undo->PushSpawn(std::move(snaps));
      s.sel->Set(id);
      SyncWorldToMeta(world, s.meta);
      s.settings->dirty = true;
      return Ok("created " + kind);
    }
    case EditorOp::Kind::Select: {
      const auto id = FindNamed(world, op.name);
      if (!world.valid(id)) {
        return Fail("not found: " + op.name);
      }
      if (op.add) {
        s.sel->Toggle(id);
      } else {
        s.sel->Set(id);
      }
      return Ok("selected " + op.name);
    }
    case EditorOp::Kind::SetTransform: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<engine::scene::NodeId> valid;
      std::vector<engine::scene::Transform> before;
      std::vector<engine::scene::Transform> after;
      for (auto id : ids) {
        if (!world.valid(id)) {
          continue;
        }
        valid.push_back(id);
        auto t = world.local_transform(id);
        before.push_back(t);
        if (op.has_x) {
          t.position.x = op.x;
        }
        if (op.has_y) {
          t.position.y = op.y;
        }
        if (op.has_z) {
          t.position.z = op.z;
        }
        float yaw = 0.f;
        float pitch = 0.f;
        float roll = 0.f;
        EulerFromQuat(t.rotation, &yaw, &pitch, &roll);
        if (op.has_yaw) {
          yaw = op.yaw;
        }
        if (op.has_pitch) {
          pitch = op.pitch;
        }
        if (op.has_roll) {
          roll = op.roll;
        }
        if (op.has_yaw || op.has_pitch || op.has_roll) {
          t.rotation = engine::Quat::FromEulerYxz(yaw, pitch, roll);
        }
        if (op.has_sx) {
          t.scale.x = op.sx;
        }
        if (op.has_sy) {
          t.scale.y = op.sy;
        }
        if (op.has_sz) {
          t.scale.z = op.sz;
        }
        if (s.settings->snap) {
          SnapTransform(&t, s.settings->grid);
        }
        world.set_local_transform(id, t);
        after.push_back(t);
      }
      if (before.empty()) {
        return Fail("no selection");
      }
      s.undo->PushBatch(valid, before, after);
      s.settings->dirty = true;
      return Ok("transform");
    }
    case EditorOp::Kind::SetMesh: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        before.push_back(CaptureProp(world, id, *s.meta));
        engine::scene::MeshRenderer mesh;
        mesh.mesh_id = op.mesh.empty() ? "cube" : op.mesh;
        if (mesh.mesh_id == "ground") {
          mesh.never_cull = true;
          mesh.local_bounds = {{-4.f, -0.05f, -4.f}, {4.f, 0.05f, 4.f}};
        }
        world.set_mesh(id, mesh);
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("mesh");
    }
    case EditorOp::Kind::SetVisible: {
      if (!op.has_visible) {
        return Fail("visible required");
      }
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        before.push_back(CaptureProp(world, id, *s.meta));
        world.set_visible(id, op.visible);
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("visible");
    }
    case EditorOp::Kind::SetScript: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        before.push_back(CaptureProp(world, id, *s.meta));
        (*s.meta)[id].script_path = op.script;
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("script");
    }
    case EditorOp::Kind::SetName: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        before.push_back(CaptureProp(world, id, *s.meta));
        world.set_name(id, op.name);
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("name");
    }
    case EditorOp::Kind::SetParent: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      engine::scene::NodeId parent = engine::scene::kInvalidNode;
      if (!op.name.empty()) {
        parent = FindNamed(world, op.name);
        if (!world.valid(parent)) {
          return Fail("parent not found: " + op.name);
        }
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        if (id == parent) {
          return Fail("cannot parent to self");
        }
        before.push_back(CaptureProp(world, id, *s.meta));
        auto st = world.set_parent(id, parent);
        if (!st) {
          return Fail(st.message());
        }
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("parent");
    }
    case EditorOp::Kind::SetFields: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<PropSnap> before;
      std::vector<PropSnap> after;
      for (auto id : ids) {
        before.push_back(CaptureProp(world, id, *s.meta));
        (*s.meta)[id].script_fields = op.script;
        after.push_back(CaptureProp(world, id, *s.meta));
      }
      s.undo->PushProps(std::move(before), std::move(after));
      s.settings->dirty = true;
      return Ok("fields");
    }
    case EditorOp::Kind::Duplicate: {
      const float off = s.settings->snap ? s.settings->grid : 1.f;
      const auto created = DuplicateSelection(world, *s.sel, off);
      if (created.empty()) {
        return Fail("nothing to duplicate");
      }
      std::vector<NodeSnap> snaps;
      for (auto id : created) {
        CaptureSubtree(world, id, *s.meta, &snaps);
      }
      s.undo->PushSpawn(std::move(snaps));
      s.sel->Set(created.front());
      for (std::size_t i = 1; i < created.size(); ++i) {
        s.sel->Toggle(created[i]);
      }
      s.settings->dirty = true;
      return Ok("duplicated");
    }
    case EditorOp::Kind::Destroy: {
      const auto ids = s.sel->All();
      if (ids.empty()) {
        return Fail("no selection");
      }
      std::vector<NodeSnap> snaps;
      for (auto id : ids) {
        bool skip = false;
        for (auto o : ids) {
          if (o != id && IsUnder(world, id, o)) {
            skip = true;
            break;
          }
        }
        if (!skip) {
          CaptureSubtree(world, id, *s.meta, &snaps);
        }
      }
      s.undo->PushKill(std::move(snaps));
      DestroySelection(world, s.sel);
      s.settings->dirty = true;
      return Ok("destroyed");
    }
    case EditorOp::Kind::Undo:
      if (!s.undo->Undo(world, s.meta, s.settings)) {
        return Fail("nothing to undo");
      }
      s.settings->dirty = true;
      return Ok("undo");
    case EditorOp::Kind::Redo:
      if (!s.undo->Redo(world, s.meta, s.settings)) {
        return Fail("nothing to redo");
      }
      s.settings->dirty = true;
      return Ok("redo");
    case EditorOp::Kind::Play: {
      if (play) {
        return Fail("already playing");
      }
      if (!s.rt || !s.scripts || !s.scene_snap) {
        return Fail("runtime not bound");
      }
      engine::scene::World* edit = s.edit_world ? s.edit_world : s.world;
      *s.scene_snap = game_kit::CaptureWorld(*edit);
      StampMeta(s.scene_snap, *s.meta);
      engine::scene::World* live = s.world;
      if (s.play_world && s.edit_world) {
        game_kit::ClearWorld(*s.play_world);
        if (auto st = game_kit::ApplyWorld(*s.play_world, *s.scene_snap); !st) {
          return Fail(st.message());
        }
        auto cap = game_kit::CaptureWorld(*s.play_world);
        RestoreMeta(*s.scene_snap, cap, s.meta);
        live = s.play_world;
      }
      BindPlayScripts(*s.scripts, *live, *s.rt, *s.meta);
      *s.playing = true;
      *s.paused = false;
      return Ok("play");
    }
    case EditorOp::Kind::Pause: {
      if (!play) {
        return Fail("not playing");
      }
      *s.paused = !*s.paused;
      if (s.rt) {
        s.rt->set_paused(*s.paused);
      }
      return Ok(*s.paused ? "paused" : "resumed");
    }
    case EditorOp::Kind::Step: {
      if (!play) {
        return Fail("not playing");
      }
      TickPlayFrame(s, 1.f / 60.f);
      return Ok("step");
    }
    case EditorOp::Kind::Stop: {
      if (!play) {
        return Fail("not playing");
      }
      if (s.scripts) {
        s.scripts->Clear();
      }
      if (s.rt) {
        s.rt->entities().Clear();
        s.rt->set_world(nullptr);
      }
      if (s.play_world && s.edit_world) {
        game_kit::ClearWorld(*s.play_world);
        if (s.scene_snap) {
          auto cap = game_kit::CaptureWorld(*s.edit_world);
          RestoreMeta(*s.scene_snap, cap, s.meta);
        }
      } else if (s.scene_snap) {
        game_kit::ClearWorld(world);
        (void)game_kit::ApplyWorld(world, *s.scene_snap);
        auto cap = game_kit::CaptureWorld(world);
        RestoreMeta(*s.scene_snap, cap, s.meta);
      }
      *s.playing = false;
      *s.paused = false;
      return Ok("stopped");
    }
    case EditorOp::Kind::ListContent: {
      if (!s.content) {
        return Fail("no content");
      }
      s.content->Scan(
          {std::filesystem::path("editor/content"), std::filesystem::path("game_kit/samples")});
      std::ostringstream out;
      out << "{\"items\":[";
      bool first = true;
      for (const auto& it : s.content->items) {
        if (!first) {
          out << ',';
        }
        first = false;
        out << "{\"kind\":\"" << (it.kind == ContentItem::Kind::Scene ? "scene" : "prefab")
            << "\",\"label\":\"" << Escape(it.label) << "\",\"path\":\"" << Escape(it.path.string())
            << "\",\"asset_id\":\"" << Escape(it.asset_id) << "\"}";
      }
      out << "]}";
      auto r = Ok();
      r.json = out.str();
      r.message = r.json;
      return r;
    }
    case EditorOp::Kind::HotReload: {
      if (!s.scripts) {
        return Fail("no scripts");
      }
      int n = 0;
      std::vector<std::string> paths;
      for (const auto& c : s.scripts->all()) {
        if (!c.path.empty()) {
          paths.push_back(c.path);
        }
      }
      for (const auto& p : paths) {
        (void)s.scripts->ReloadPath(p, true);
        ++n;
      }
      if (s.device) {
        if (!s.settings->heights.empty()) {
          if (!UploadTerrainMesh(*s.device, world, s.settings->heights, 2)) {
            return Fail("mesh hot reload failed");
          }
        }
        auto loader = engine::assets::CreateDefaultImageLoader();
        std::error_code ec;
        const auto root = std::filesystem::path("editor/content");
        if (loader && std::filesystem::exists(root, ec)) {
          for (const auto& ent : std::filesystem::directory_iterator(root, ec)) {
            if (!ent.is_regular_file(ec)) {
              continue;
            }
            const auto ext = ent.path().extension().string();
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
              continue;
            }
            auto img = loader->LoadFile(ent.path());
            if (!img) {
              return Fail(img.status().message());
            }
            if (auto st = s.device->UploadLitAlbedoRgba(img.value().rgba.data(), img.value().width,
                                                        img.value().height, 0);
                !st) {
              return Fail(st.message());
            }
            ++n;
          }
        }
      }
      return Ok("reloaded " + std::to_string(n));
    }
    case EditorOp::Kind::Bake: {
      const auto out = std::filesystem::path("editor/content/lightmap.rgba");
      if (auto st = BakeSceneLights(world, s.settings->heights, out, s.device); !st) {
        return Fail(st.message());
      }
      return Ok("bake");
    }
    case EditorOp::Kind::Lint: {
      auto doc = game_kit::CaptureWorld(world);
      StampMeta(&doc, *s.meta);
      const auto scene_st = game_kit::ValidateSceneDocument(doc);
      auto graph = BuildDepGraph(std::filesystem::path("editor/content"));
      if (!graph.ok) {
        graph = BuildDepGraph(std::filesystem::path("editor/content/manifest.json"));
      }
      const auto json = DepGraphJson(graph);
      if (s.lint_json) {
        *s.lint_json = json;
      }
      (void)TryRunTool("content_lint");
      if (!scene_st) {
        auto r = Fail(scene_st.message());
        r.json = json;
        r.message = scene_st.message();
        return r;
      }
      auto r = graph.ok ? Ok("lint") : Fail(graph.message);
      r.json = json;
      r.message = json;
      return r;
    }
    case EditorOp::Kind::Place: {
      if (op.path.empty()) {
        return Fail("path required");
      }
      auto prefab = game_kit::LoadPrefabDocument(op.path);
      if (!prefab) {
        return Fail(prefab.status().message());
      }
      engine::scene::Transform trs;
      trs.position = {op.has_x ? op.x : 0.f, op.has_y ? op.y : 0.5f, op.has_z ? op.z : 0.f};
      auto catalog = LoadCatalog(s.content);
      const auto id = game_kit::InstantiateNested(world, prefab.value(), trs, catalog, s.rt);
      if (id == engine::scene::kInvalidNode) {
        return Fail("instantiate failed");
      }
      (*s.meta)[id].prefab_id = prefab.value().prefab_id;
      (*s.meta)[id].source_prefab = op.path;
      std::vector<NodeSnap> snaps;
      CaptureSubtree(world, id, *s.meta, &snaps);
      s.undo->PushSpawn(std::move(snaps));
      s.sel->Set(id);
      s.settings->dirty = true;
      return Ok("placed");
    }
    case EditorOp::Kind::ApplyPrefab: {
      const auto id = s.sel->node;
      if (!world.valid(id)) {
        return Fail("no selection");
      }
      auto it = s.meta->find(id);
      if (it == s.meta->end() || it->second.source_prefab.empty()) {
        return Fail("no prefab source");
      }
      auto loaded = game_kit::LoadPrefabDocument(it->second.source_prefab);
      if (!loaded) {
        return Fail(loaded.status().message());
      }
      auto doc = loaded.value();
      game_kit::ApplyInstanceToSource(world, id, &doc);
      if (auto st = game_kit::SavePrefabDocument(doc, it->second.source_prefab); !st) {
        return Fail(st.message());
      }
      return Ok("apply prefab");
    }
    case EditorOp::Kind::RevertPrefab: {
      const auto id = s.sel->node;
      if (!world.valid(id)) {
        return Fail("no selection");
      }
      auto it = s.meta->find(id);
      if (it == s.meta->end() || it->second.source_prefab.empty()) {
        return Fail("no prefab source");
      }
      auto loaded = game_kit::LoadPrefabDocument(it->second.source_prefab);
      if (!loaded || loaded.value().scene.nodes.empty()) {
        return Fail("prefab missing");
      }
      const auto& src = loaded.value().scene.nodes[0];
      world.set_local_transform(id, src.transform);
      world.set_visible(id, src.visible);
      game_kit::ApplyNodeComponents(world, id, src);
      it->second.override_json.clear();
      s.settings->dirty = true;
      return Ok("revert prefab");
    }
    case EditorOp::Kind::Screenshot: {
      const auto path = op.path.empty() ? std::filesystem::temp_directory_path() / "editor_shot.ppm"
                                        : std::filesystem::path(op.path);
      std::vector<std::uint8_t> rgba;
      int rw = 0;
      int rh = 0;
      if (s.device) {
        if (auto st = s.device->ReadbackTextureStub(rgba, rw, rh); !st) {
          return Fail(st.message());
        }
      } else {
        rw = 4;
        rh = 4;
        rgba.assign(static_cast<std::size_t>(rw * rh * 4), 180);
        for (int i = 0; i < rw * rh; ++i) {
          rgba[static_cast<std::size_t>(i) * 4 + 3] = 255;
        }
      }
      if (!WritePpm(path, rgba, rw, rh)) {
        return Fail("screenshot readback too small or write failed");
      }
      if (s.screenshot_path) {
        *s.screenshot_path = path.generic_string();
      }
      auto r = Ok("screenshot " + path.generic_string());
      r.json = std::string("{\"path\":\"") + Escape(path.generic_string()) + "\"}";
      r.message = r.json;
      return r;
    }
    case EditorOp::Kind::Sculpt: {
      auto h0 = s.settings->heights;
      auto t0 = s.settings->tiles;
      auto a0 = s.settings->anim;
      const float amt = op.has_y ? op.y : s.settings->sculpt;
      if (s.settings->sculpt_mode == 1) {
        LowerHeight(&s.settings->heights, static_cast<int>(op.x), static_cast<int>(op.z), amt, 2.f);
      } else if (s.settings->sculpt_mode == 2) {
        SmoothHeight(&s.settings->heights, static_cast<int>(op.x), static_cast<int>(op.z), 2.f);
      } else {
        RaiseHeight(&s.settings->heights, static_cast<int>(op.x), static_cast<int>(op.z), amt, 2.f);
      }
      s.undo->PushGrid(std::move(h0), std::move(t0), std::move(a0), s.settings->heights,
                       s.settings->tiles, s.settings->anim);
      if (s.device) {
        if (!UploadTerrainMesh(*s.device, world, s.settings->heights, 2)) {
          return Fail("sculpt upload failed");
        }
      }
      s.settings->dirty = true;
      return s.device ? Ok("sculpt") : Ok("sculpt (cpu only)");
    }
    case EditorOp::Kind::PaintTile: {
      auto h0 = s.settings->heights;
      auto t0 = s.settings->tiles;
      auto a0 = s.settings->anim;
      PaintTile(&s.settings->tiles, nullptr, static_cast<int>(op.x), static_cast<int>(op.z),
                static_cast<int>(op.sx));
      s.undo->PushGrid(std::move(h0), std::move(t0), std::move(a0), s.settings->heights,
                       s.settings->tiles, s.settings->anim);
      s.settings->dirty = true;
      return Ok("tile");
    }
  }
  return Fail("unknown op");
}

}  // namespace editor
