#include "game_kit/save.h"

#include <fstream>
#include <sstream>

namespace game_kit {

SaveSlots::SaveSlots(std::filesystem::path dir) : dir_(std::move(dir)) {}

std::filesystem::path SaveSlots::SlotPath(int index) const {
  return dir_ / ("slot_" + std::to_string(index) + ".json");
}

engine::Status SaveSlots::Write(int index, std::string_view payload) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  std::ofstream out(SlotPath(index), std::ios::binary);
  if (!out) {
    return engine::Status::Fail("cannot write save slot");
  }
  out << "{\"slot\":" << index << ",\"payload\":\"";
  for (char c : payload) {
    if (c == '\\' || c == '"') {
      out << '\\';
    }
    if (c == '\n') {
      out << "\\n";
    } else {
      out << c;
    }
  }
  out << "\"}";
  return engine::Status::Ok();
}

engine::Result<SaveSlot> SaveSlots::Read(int index) const {
  std::ifstream in(SlotPath(index), std::ios::binary);
  if (!in) {
    return engine::Result<SaveSlot>::Fail("save slot missing");
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  SaveSlot slot;
  slot.index = index;
  const auto key = text.find("\"payload\"");
  if (key == std::string::npos) {
    return engine::Result<SaveSlot>::Fail("save slot malformed");
  }
  const auto q1 = text.find('"', key + 9);
  if (q1 == std::string::npos) {
    return engine::Result<SaveSlot>::Fail("save slot malformed");
  }
  std::string payload;
  for (std::size_t i = q1 + 1; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      payload.push_back(text[i] == 'n' ? '\n' : text[i]);
      continue;
    }
    if (text[i] == '"') {
      break;
    }
    payload.push_back(text[i]);
  }
  slot.payload = std::move(payload);
  return engine::Result<SaveSlot>::Ok(std::move(slot));
}

std::filesystem::path SaveSlots::BytesPath(std::string_view name) const {
  return dir_ / std::filesystem::path(name);
}

engine::Status SaveSlots::WriteBytes(std::string_view name, const std::vector<std::uint8_t>& bytes) {
  std::error_code ec;
  std::filesystem::create_directories(dir_, ec);
  std::ofstream out(BytesPath(name), std::ios::binary);
  if (!out) {
    return engine::Status::Fail("cannot write save bytes");
  }
  if (!bytes.empty()) {
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  }
  return engine::Status::Ok();
}

engine::Result<std::vector<std::uint8_t>> SaveSlots::ReadBytes(std::string_view name) const {
  std::ifstream in(BytesPath(name), std::ios::binary);
  if (!in) {
    return engine::Result<std::vector<std::uint8_t>>::Fail("save bytes missing");
  }
  in.seekg(0, std::ios::end);
  const auto n = static_cast<std::size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> bytes(n);
  if (n > 0) {
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(n));
  }
  return engine::Result<std::vector<std::uint8_t>>::Ok(std::move(bytes));
}

}  // namespace game_kit
