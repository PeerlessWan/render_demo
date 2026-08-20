#include "mcp/protocol.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace editor {
namespace {

void SkipWs(std::string_view t, std::size_t* i) {
  while (i && *i < t.size() && std::isspace(static_cast<unsigned char>(t[*i]))) {
    ++*i;
  }
}

bool FindKey(std::string_view json, std::string_view key, std::size_t* value_pos) {
  const std::string pat = std::string("\"") + std::string(key) + "\"";
  std::size_t search = 0;
  while (search < json.size()) {
    const auto at = json.find(pat, search);
    if (at == std::string_view::npos) {
      return false;
    }
    if (at > 0 && json[at - 1] == '\\') {
      search = at + 1;
      continue;
    }
    std::size_t i = at + pat.size();
    SkipWs(json, &i);
    if (i < json.size() && json[i] == ':') {
      ++i;
      SkipWs(json, &i);
      if (value_pos) {
        *value_pos = i;
      }
      return true;
    }
    search = at + 1;
  }
  return false;
}

std::string ParseStringValue(std::string_view json, std::size_t i) {
  if (i >= json.size() || json[i] != '"') {
    return {};
  }
  ++i;
  std::string o;
  while (i < json.size()) {
    const char c = json[i];
    if (c == '\\' && i + 1 < json.size()) {
      o.push_back(json[i + 1]);
      i += 2;
      continue;
    }
    if (c == '"') {
      break;
    }
    o.push_back(c);
    ++i;
  }
  return o;
}

std::string RawValue(std::string_view json, std::size_t i) {
  SkipWs(json, &i);
  if (i >= json.size()) {
    return {};
  }
  if (json[i] == '"') {
    std::size_t j = i + 1;
    while (j < json.size()) {
      if (json[j] == '\\' && j + 1 < json.size()) {
        j += 2;
        continue;
      }
      if (json[j] == '"') {
        return std::string(json.substr(i, j - i + 1));
      }
      ++j;
    }
    return {};
  }
  if (json[i] == '{') {
    int depth = 0;
    for (std::size_t j = i; j < json.size(); ++j) {
      if (json[j] == '{') {
        ++depth;
      } else if (json[j] == '}') {
        --depth;
        if (depth == 0) {
          return std::string(json.substr(i, j - i + 1));
        }
      }
    }
  }
  if (json[i] == '[') {
    int depth = 0;
    for (std::size_t j = i; j < json.size(); ++j) {
      if (json[j] == '[') {
        ++depth;
      } else if (json[j] == ']') {
        --depth;
        if (depth == 0) {
          return std::string(json.substr(i, j - i + 1));
        }
      }
    }
  }
  std::size_t j = i;
  while (j < json.size() && json[j] != ',' && json[j] != '}' && json[j] != ']' &&
         !std::isspace(static_cast<unsigned char>(json[j]))) {
    ++j;
  }
  return std::string(json.substr(i, j - i));
}

std::string Escape(std::string_view s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '\\' || c == '"') {
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

std::string ReplyResult(const std::string& id, bool has_id, std::string_view result_obj) {
  std::ostringstream out;
  out << "{\"jsonrpc\":\"2.0\",";
  if (has_id) {
    out << "\"id\":" << id << ',';
  }
  out << "\"result\":" << result_obj << '}';
  return out.str();
}

std::string ToolText(const std::string& id, bool has_id, std::string_view text, bool is_error) {
  std::ostringstream result;
  result << "{\"content\":[{\"type\":\"text\",\"text\":\"" << Escape(text) << "\"}],\"isError\":"
         << (is_error ? "true" : "false") << '}';
  return ReplyResult(id, has_id, result.str());
}

const char* kToolsList =
    R"MCP({"tools":[{"name":"editor_dump","description":"Dump scene nodes and selection as JSON","inputSchema":{"type":"object","properties":{}}},{"name":"editor_open","description":"Open a scene JSON file","inputSchema":{"type":"object","properties":{"path":{"type":"string"}}}},{"name":"editor_save","description":"Save the current scene","inputSchema":{"type":"object","properties":{"path":{"type":"string"}}}},{"name":"editor_create","description":"Create cube, empty, ground, or player","inputSchema":{"type":"object","properties":{"kind":{"type":"string"}}}},{"name":"editor_select","description":"Select a node by name","inputSchema":{"type":"object","properties":{"name":{"type":"string"},"add":{"type":"boolean"}}}},{"name":"editor_set_transform","description":"Set transform on the selection","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"yaw":{"type":"number"},"pitch":{"type":"number"},"roll":{"type":"number"},"sx":{"type":"number"},"sy":{"type":"number"},"sz":{"type":"number"}}}},{"name":"editor_set_mesh","description":"Set mesh_id on the selection","inputSchema":{"type":"object","properties":{"mesh":{"type":"string"}}}},{"name":"editor_set_visible","description":"Set visibility on the selection","inputSchema":{"type":"object","properties":{"visible":{"type":"boolean"}}}},{"name":"editor_set_script","description":"Set script_path on the selection","inputSchema":{"type":"object","properties":{"script":{"type":"string"}}}},{"name":"editor_set_name","description":"Rename the selection","inputSchema":{"type":"object","properties":{"name":{"type":"string"}}}},{"name":"editor_duplicate","description":"Duplicate the selection","inputSchema":{"type":"object","properties":{}}},{"name":"editor_destroy","description":"Delete the selection","inputSchema":{"type":"object","properties":{}}},{"name":"editor_undo","description":"Undo","inputSchema":{"type":"object","properties":{}}},{"name":"editor_redo","description":"Redo","inputSchema":{"type":"object","properties":{}}},{"name":"editor_play","description":"Enter Play (logic only)","inputSchema":{"type":"object","properties":{}}},{"name":"editor_pause","description":"Toggle pause","inputSchema":{"type":"object","properties":{}}},{"name":"editor_step","description":"Step one play frame","inputSchema":{"type":"object","properties":{}}},{"name":"editor_stop","description":"Stop Play and restore snapshot","inputSchema":{"type":"object","properties":{}}},{"name":"editor_list_content","description":"List editor/content json","inputSchema":{"type":"object","properties":{}}},{"name":"editor_set_parent","description":"Reparent selection; empty parent is root","inputSchema":{"type":"object","properties":{"parent":{"type":"string"}}}},{"name":"editor_set_fields","description":"Set script persist fields (k=v lines)","inputSchema":{"type":"object","properties":{"fields":{"type":"string"}}}},{"name":"editor_hot_reload","description":"Reload Lua scripts preserving persist","inputSchema":{"type":"object","properties":{}}},{"name":"editor_bake","description":"Run lightmap_baker CLI","inputSchema":{"type":"object","properties":{}}},{"name":"editor_bake_nav","description":"Bake Recast navmesh from scene colliders/meshes","inputSchema":{"type":"object","properties":{}}},{"name":"editor_lint","description":"Run content_lint C20 CLI and return dependency graph JSON","inputSchema":{"type":"object","properties":{}}},{"name":"editor_place","description":"Instantiate a prefab JSON path","inputSchema":{"type":"object","properties":{"path":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"}}}},{"name":"editor_apply_prefab","description":"Write selection transform back to prefab source","inputSchema":{"type":"object","properties":{}}},{"name":"editor_revert_prefab","description":"Revert selection to prefab source transform","inputSchema":{"type":"object","properties":{}}},{"name":"editor_screenshot","description":"Write a screenshot stub (PPM)","inputSchema":{"type":"object","properties":{"path":{"type":"string"}}}},{"name":"editor_sculpt","description":"Raise terrain height at x,z","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"z":{"type":"number"},"y":{"type":"number"}}}},{"name":"editor_paint_tile","description":"Paint tile GID; x,z cell, sx=gid","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"z":{"type":"number"},"sx":{"type":"number"}}}}]})MCP";

}  // namespace

std::string JsonGetString(std::string_view json, std::string_view key) {
  std::size_t i = 0;
  if (!FindKey(json, key, &i)) {
    return {};
  }
  return ParseStringValue(json, i);
}

bool JsonGetBool(std::string_view json, std::string_view key, bool* out) {
  std::size_t i = 0;
  if (!FindKey(json, key, &i) || !out) {
    return false;
  }
  const auto raw = RawValue(json, i);
  if (raw == "true") {
    *out = true;
    return true;
  }
  if (raw == "false") {
    *out = false;
    return true;
  }
  return false;
}

bool JsonGetNumber(std::string_view json, std::string_view key, float* out) {
  std::size_t i = 0;
  if (!FindKey(json, key, &i) || !out) {
    return false;
  }
  const auto raw = RawValue(json, i);
  if (raw.empty() || raw[0] == '"' || raw[0] == '{' || raw[0] == '[') {
    return false;
  }
  char* end = nullptr;
  const float v = std::strtof(raw.c_str(), &end);
  if (!end || end == raw.c_str()) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseMcpRequest(std::string_view line, McpRequest* out) {
  if (!out) {
    return false;
  }
  *out = {};
  out->method = JsonGetString(line, "method");
  std::size_t id_pos = 0;
  if (FindKey(line, "id", &id_pos)) {
    out->has_id = true;
    if (id_pos < line.size() && line[id_pos] == '"') {
      out->id = std::string("\"") + ParseStringValue(line, id_pos) + "\"";
    } else {
      out->id = RawValue(line, id_pos);
    }
  }
  const auto params = [&] {
    std::size_t p = 0;
    if (!FindKey(line, "params", &p)) {
      return std::string{};
    }
    return RawValue(line, p);
  }();
  out->tool = JsonGetString(params, "name");
  std::size_t args_pos = 0;
  if (FindKey(params, "arguments", &args_pos)) {
    out->arguments = RawValue(params, args_pos);
  } else {
    out->arguments = "{}";
  }
  return !out->method.empty();
}

bool EditorOpFromTool(std::string_view tool, std::string_view arguments, EditorOp* out) {
  if (!out) {
    return false;
  }
  *out = {};
  auto num = [&](const char* k, bool* has, float* v) {
    if (JsonGetNumber(arguments, k, v)) {
      *has = true;
    }
  };
  if (tool == "editor_dump") {
    out->kind = EditorOp::Kind::Dump;
  } else if (tool == "editor_open") {
    out->kind = EditorOp::Kind::Open;
    out->path = JsonGetString(arguments, "path");
  } else if (tool == "editor_save") {
    out->kind = EditorOp::Kind::Save;
    out->path = JsonGetString(arguments, "path");
  } else if (tool == "editor_create") {
    out->kind = EditorOp::Kind::Create;
    out->create_kind = JsonGetString(arguments, "kind");
    if (out->create_kind.empty()) {
      out->create_kind = "cube";
    }
  } else if (tool == "editor_select") {
    out->kind = EditorOp::Kind::Select;
    out->name = JsonGetString(arguments, "name");
    (void)JsonGetBool(arguments, "add", &out->add);
  } else if (tool == "editor_set_transform") {
    out->kind = EditorOp::Kind::SetTransform;
    num("x", &out->has_x, &out->x);
    num("y", &out->has_y, &out->y);
    num("z", &out->has_z, &out->z);
    num("yaw", &out->has_yaw, &out->yaw);
    num("pitch", &out->has_pitch, &out->pitch);
    num("roll", &out->has_roll, &out->roll);
    num("sx", &out->has_sx, &out->sx);
    num("sy", &out->has_sy, &out->sy);
    num("sz", &out->has_sz, &out->sz);
  } else if (tool == "editor_set_mesh") {
    out->kind = EditorOp::Kind::SetMesh;
    out->mesh = JsonGetString(arguments, "mesh");
  } else if (tool == "editor_set_visible") {
    out->kind = EditorOp::Kind::SetVisible;
    out->has_visible = JsonGetBool(arguments, "visible", &out->visible);
  } else if (tool == "editor_set_script") {
    out->kind = EditorOp::Kind::SetScript;
    out->script = JsonGetString(arguments, "script");
  } else if (tool == "editor_set_name") {
    out->kind = EditorOp::Kind::SetName;
    out->name = JsonGetString(arguments, "name");
  } else if (tool == "editor_set_parent") {
    out->kind = EditorOp::Kind::SetParent;
    out->name = JsonGetString(arguments, "parent");
  } else if (tool == "editor_set_fields") {
    out->kind = EditorOp::Kind::SetFields;
    out->script = JsonGetString(arguments, "fields");
  } else if (tool == "editor_duplicate") {
    out->kind = EditorOp::Kind::Duplicate;
  } else if (tool == "editor_destroy") {
    out->kind = EditorOp::Kind::Destroy;
  } else if (tool == "editor_undo") {
    out->kind = EditorOp::Kind::Undo;
  } else if (tool == "editor_redo") {
    out->kind = EditorOp::Kind::Redo;
  } else if (tool == "editor_play") {
    out->kind = EditorOp::Kind::Play;
  } else if (tool == "editor_pause") {
    out->kind = EditorOp::Kind::Pause;
  } else if (tool == "editor_step") {
    out->kind = EditorOp::Kind::Step;
  } else if (tool == "editor_stop") {
    out->kind = EditorOp::Kind::Stop;
  } else if (tool == "editor_list_content") {
    out->kind = EditorOp::Kind::ListContent;
  } else if (tool == "editor_hot_reload") {
    out->kind = EditorOp::Kind::HotReload;
  } else if (tool == "editor_bake") {
    out->kind = EditorOp::Kind::Bake;
  } else if (tool == "editor_bake_nav") {
    out->kind = EditorOp::Kind::BakeNav;
  } else if (tool == "editor_lint") {
    out->kind = EditorOp::Kind::Lint;
  } else if (tool == "editor_place") {
    out->kind = EditorOp::Kind::Place;
    out->path = JsonGetString(arguments, "path");
    num("x", &out->has_x, &out->x);
    num("y", &out->has_y, &out->y);
    num("z", &out->has_z, &out->z);
  } else if (tool == "editor_apply_prefab") {
    out->kind = EditorOp::Kind::ApplyPrefab;
  } else if (tool == "editor_revert_prefab") {
    out->kind = EditorOp::Kind::RevertPrefab;
  } else if (tool == "editor_screenshot") {
    out->kind = EditorOp::Kind::Screenshot;
    out->path = JsonGetString(arguments, "path");
  } else if (tool == "editor_sculpt") {
    out->kind = EditorOp::Kind::Sculpt;
    num("x", &out->has_x, &out->x);
    num("y", &out->has_y, &out->y);
    num("z", &out->has_z, &out->z);
  } else if (tool == "editor_paint_tile") {
    out->kind = EditorOp::Kind::PaintTile;
    num("x", &out->has_x, &out->x);
    num("z", &out->has_z, &out->z);
    num("sx", &out->has_sx, &out->sx);
  } else {
    return false;
  }
  return true;
}

std::string HandleMcpSession(EditorSession sess, std::string_view line) {
  McpRequest req;
  if (!ParseMcpRequest(line, &req)) {
    return {};
  }
  if (req.method == "notifications/initialized" || req.method.rfind("notifications/", 0) == 0) {
    return {};
  }
  if (req.method == "initialize") {
    return ReplyResult(req.id, req.has_id,
                       "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
                       "\"serverInfo\":{\"name\":\"editor_mcp\",\"version\":\"1\"}}");
  }
  if (req.method == "tools/list") {
    return ReplyResult(req.id, req.has_id, kToolsList);
  }
  if (req.method == "tools/call") {
    EditorOp op;
    if (!EditorOpFromTool(req.tool, req.arguments, &op)) {
      return ToolText(req.id, req.has_id, "unknown tool", true);
    }
    const auto r = ApplyOp(sess, op);
    const auto body = r.json.empty() ? r.message : r.json;
    return ToolText(req.id, req.has_id, body, r.is_error);
  }
  if (req.has_id) {
    std::ostringstream err;
    err << "{\"jsonrpc\":\"2.0\",\"id\":" << req.id
        << ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
    return err.str();
  }
  return {};
}

std::string HandleMcpLine(EditorHost& host, std::string_view line) {
  return HandleMcpSession(host.Bind(), line);
}

}  // namespace editor
