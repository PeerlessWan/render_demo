#include "io/world_save.h"

#include "sim/blocks.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace mc {
namespace {

void WriteStack(std::ostream& o, const Stack& s) {
  o << "{\"id\":" << static_cast<int>(s.id) << ",\"n\":" << static_cast<int>(s.count)
    << ",\"w\":" << static_cast<int>(s.wear) << '}';
}

void SkipWs(const std::string& t, std::size_t* i) {
  while (*i < t.size() && (t[*i] == ' ' || t[*i] == '\n' || t[*i] == '\r' || t[*i] == '\t')) {
    ++*i;
  }
}

int ParseInt(const std::string& t, std::size_t* i) {
  SkipWs(t, i);
  int sign = 1;
  if (*i < t.size() && t[*i] == '-') {
    sign = -1;
    ++*i;
  }
  int v = 0;
  while (*i < t.size() && t[*i] >= '0' && t[*i] <= '9') {
    v = v * 10 + (t[*i] - '0');
    ++*i;
  }
  return v * sign;
}

void EatUntil(const std::string& t, std::size_t* i, char c) {
  while (*i < t.size() && t[*i] != c) {
    ++*i;
  }
  if (*i < t.size()) {
    ++*i;
  }
}

Stack ParseStack(const std::string& t, std::size_t* i) {
  Stack s;
  EatUntil(t, i, '{');
  while (*i < t.size() && t[*i] != '}') {
    SkipWs(t, i);
    if (t.compare(*i, 4, "\"id\"") == 0) {
      EatUntil(t, i, ':');
      s.id = static_cast<Id>(ParseInt(t, i));
    } else if (t.compare(*i, 3, "\"n\"") == 0) {
      EatUntil(t, i, ':');
      s.count = static_cast<std::uint8_t>(ParseInt(t, i));
    } else if (t.compare(*i, 3, "\"w\"") == 0) {
      EatUntil(t, i, ':');
      s.wear = static_cast<std::uint16_t>(ParseInt(t, i));
    } else {
      ++*i;
    }
  }
  if (*i < t.size() && t[*i] == '}') {
    ++*i;
  }
  return s;
}

std::string ChunkName(ChunkCoord c) {
  return "c_" + std::to_string(c.x) + "_" + std::to_string(c.z) + ".bin";
}

}  // namespace

engine::Status SaveWorld(const GameState& st, const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream lvl(dir / "level.json");
  if (!lvl) {
    return engine::Status::Fail("level.json");
  }
  lvl << "{\"seed\":" << st.world.seed() << ",\"time\":" << st.clock.time
      << ",\"creative\":" << (st.player.creative ? "true" : "false") << ",\"px\":" << st.player.pos.x
      << ",\"py\":" << st.player.pos.y << ",\"pz\":" << st.player.pos.z << ",\"hp\":" << st.player.hp
      << ",\"hunger\":" << st.player.hunger << ",\"sel\":" << st.player.inv.selected
      << ",\"view\":" << st.view_radius << ",\"spawn\":" << (st.player.has_spawn ? "true" : "false")
      << ",\"sx\":" << st.player.spawn.x << ",\"sy\":" << st.player.spawn.y
      << ",\"sz\":" << st.player.spawn.z << ",\"inv\":[";
  for (int i = 0; i < Inventory::kSize; ++i) {
    if (i) {
      lvl << ',';
    }
    WriteStack(lvl, st.player.inv.slots[i]);
  }
  lvl << "],\"chests\":[";
  bool first = true;
  for (const auto& [p, c] : st.boxes.chests) {
    if (!first) {
      lvl << ',';
    }
    first = false;
    lvl << "{\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z << ",\"s\":[";
    for (int i = 0; i < 27; ++i) {
      if (i) {
        lvl << ',';
      }
      WriteStack(lvl, c.slots[i]);
    }
    lvl << "]}";
  }
  lvl << "]}\n";

  for (const auto& [coord, ch] : st.world.chunks()) {
    if (!ch) {
      continue;
    }
    std::ofstream bin(dir / ChunkName(coord), std::ios::binary);
    const auto& d = ch->data();
    bin.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
  }
  return engine::Status::Ok();
}

engine::Status LoadWorld(GameState* st, const std::filesystem::path& dir) {
  if (!st) {
    return engine::Status::Fail("null");
  }
  std::ifstream lvl(dir / "level.json");
  if (!lvl) {
    return engine::Status::Fail("missing level.json");
  }
  std::ostringstream ss;
  ss << lvl.rdbuf();
  const std::string t = ss.str();
  auto find_num = [&](const char* key) {
    const auto p = t.find(key);
    if (p == std::string::npos) {
      return 0.0;
    }
    std::size_t i = t.find(':', p);
    if (i == std::string::npos) {
      return 0.0;
    }
    ++i;
    return static_cast<double>(std::strtod(t.c_str() + i, nullptr));
  };
  st->world = World(static_cast<std::uint32_t>(find_num("\"seed\"")));
  st->clock.time = static_cast<float>(find_num("\"time\""));
  st->player.creative = t.find("\"creative\":true") != std::string::npos;
  st->player.pos.x = static_cast<float>(find_num("\"px\""));
  st->player.pos.y = static_cast<float>(find_num("\"py\""));
  st->player.pos.z = static_cast<float>(find_num("\"pz\""));
  st->player.hp = static_cast<float>(find_num("\"hp\""));
  st->player.hunger = static_cast<float>(find_num("\"hunger\""));
  st->player.inv.selected = static_cast<int>(find_num("\"sel\""));
  st->player.dead = st->player.hp <= 0.f;
  st->player.flying = st->player.creative;
  if (t.find("\"view\"") != std::string::npos) {
    st->view_radius = static_cast<int>(find_num("\"view\""));
    if (st->view_radius < 2) {
      st->view_radius = 2;
    }
  }
  st->player.has_spawn = t.find("\"spawn\":true") != std::string::npos;
  if (st->player.has_spawn) {
    st->player.spawn.x = static_cast<float>(find_num("\"sx\""));
    st->player.spawn.y = static_cast<float>(find_num("\"sy\""));
    st->player.spawn.z = static_cast<float>(find_num("\"sz\""));
  }
  const auto invp = t.find("\"inv\"");
  if (invp != std::string::npos) {
    std::size_t i = t.find('[', invp);
    int slot = 0;
    while (i < t.size() && slot < Inventory::kSize) {
      const auto brace = t.find('{', i);
      if (brace == std::string::npos) {
        break;
      }
      i = brace;
      st->player.inv.slots[slot++] = ParseStack(t, &i);
    }
  }
  for (const auto& ent : std::filesystem::directory_iterator(dir)) {
    const auto name = ent.path().filename().string();
    if (name.size() < 8 || name[0] != 'c' || name[1] != '_') {
      continue;
    }
    std::size_t p = 2;
    int sign = 1;
    if (p < name.size() && name[p] == '-') {
      sign = -1;
      ++p;
    }
    int cx = 0;
    while (p < name.size() && name[p] >= '0' && name[p] <= '9') {
      cx = cx * 10 + (name[p] - '0');
      ++p;
    }
    cx *= sign;
    if (p >= name.size() || name[p] != '_') {
      continue;
    }
    ++p;
    sign = 1;
    if (p < name.size() && name[p] == '-') {
      sign = -1;
      ++p;
    }
    int cz = 0;
    while (p < name.size() && name[p] >= '0' && name[p] <= '9') {
      cz = cz * 10 + (name[p] - '0');
      ++p;
    }
    cz *= sign;
    std::ifstream bin(ent.path(), std::ios::binary);
    Chunk& ch = st->world.InsertBlank(ChunkCoord{cx, cz});
    bin.read(reinterpret_cast<char*>(ch.data().data()),
             static_cast<std::streamsize>(ch.data().size()));
    ch.set_dirty(false);
  }
  return engine::Status::Ok();
}

}  // namespace mc
