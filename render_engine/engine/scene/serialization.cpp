#include "engine/scene/serialization.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace engine::scene {
namespace {

void WriteNode(std::ostream& out, const World& world, NodeId id, bool& first) {
  if (!world.valid(id)) {
    return;
  }
  if (!first) {
    out << ',';
  }
  first = false;
  const auto& t = world.local_transform(id);
  out << "{\"name\":\"" << world.name(id) << "\",\"pos\":[" << t.position.x << ',' << t.position.y
      << ',' << t.position.z << "]";
  if (const auto* mesh = world.mesh(id)) {
    out << ",\"mesh\":\"" << mesh->mesh_id << "\"";
  }
  out << ",\"children\":[";
  bool cf = true;
  for (NodeId c : world.children(id)) {
    WriteNode(out, world, c, cf);
  }
  out << "]}";
}

}  // namespace

Status SaveWorldJson(const World& world, const std::filesystem::path& path) {
  std::ofstream out(path);
  if (!out) {
    return Status::Fail("cannot write scene: " + path.string());
  }
  out << "{\"nodes\":[";
  bool first = true;
  for (NodeId r : world.roots()) {
    WriteNode(out, world, r, first);
  }
  out << "]}";
  return Status::Ok();
}

Result<World> LoadWorldJson(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return Result<World>::Fail("cannot read scene: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();

  World world;
  // Extremely small parser: find "name":"..." objects sequentially (flat roots only for MVP).
  std::size_t pos = 0;
  while (true) {
    const auto name_key = text.find("\"name\"", pos);
    if (name_key == std::string::npos) {
      break;
    }
    const auto q1 = text.find('"', name_key + 6);
    const auto q2 = text.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) {
      break;
    }
    const std::string name = text.substr(q1 + 1, q2 - q1 - 1);
    const NodeId id = world.CreateNode(name);
    const auto pos_key = text.find("\"pos\"", q2);
    if (pos_key != std::string::npos && pos_key < q2 + 80) {
      const auto lb = text.find('[', pos_key);
      const auto rb = text.find(']', lb);
      if (lb != std::string::npos && rb != std::string::npos) {
        Transform t = world.local_transform(id);
        std::sscanf(text.c_str() + lb + 1, "%f,%f,%f", &t.position.x, &t.position.y, &t.position.z);
        world.set_local_transform(id, t);
      }
    }
    const auto mesh_key = text.find("\"mesh\"", q2);
    if (mesh_key != std::string::npos && mesh_key < q2 + 120) {
      const auto mq1 = text.find('"', mesh_key + 6);
      const auto mq2 = text.find('"', mq1 + 1);
      if (mq1 != std::string::npos && mq2 != std::string::npos) {
        MeshRenderer mesh;
        mesh.mesh_id = text.substr(mq1 + 1, mq2 - mq1 - 1);
        world.set_mesh(id, std::move(mesh));
      }
    }
    pos = q2 + 1;
  }
  world.UpdateTransforms();
  return Result<World>::Ok(std::move(world));
}

}  // namespace engine::scene
