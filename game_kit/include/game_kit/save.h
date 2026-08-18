#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

inline constexpr int kSaveFormatCurrent = 1;

struct SaveSlot {
  int index = 0;
  int version = kSaveFormatCurrent;
  std::string payload;
};

class SaveSlots {
 public:
  explicit SaveSlots(std::filesystem::path dir = "saves");

  engine::Status Write(int index, std::string_view payload);
  engine::Status Write(int index, std::string_view payload, int version);
  engine::Result<SaveSlot> Read(int index) const;
  engine::Status WriteBytes(std::string_view name, const std::vector<std::uint8_t>& bytes);
  engine::Result<std::vector<std::uint8_t>> ReadBytes(std::string_view name) const;
  [[nodiscard]] std::filesystem::path SlotPath(int index) const;
  [[nodiscard]] std::filesystem::path BytesPath(std::string_view name) const;

 private:
  std::filesystem::path dir_;
};

}  // namespace game_kit
