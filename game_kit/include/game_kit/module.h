#pragma once

#include "engine/app/module.h"

#include <string>
#include <string_view>

namespace game_kit {

// Play-layer module. Hosts register this with Application::modules().
class IGameModule : public engine::IModule {
 public:
  explicit IGameModule(std::string name = "game_kit") : name_(std::move(name)) {}
  std::string_view name() const override { return name_; }

 private:
  std::string name_;
};

}  // namespace game_kit
