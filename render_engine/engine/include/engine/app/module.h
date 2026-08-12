#pragma once

#include "engine/core/result.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class Application;

class IModule {
 public:
  virtual ~IModule() = default;
  virtual std::string_view name() const = 0;
  virtual Status OnInit(Application&) { return Status::Ok(); }
  virtual void OnUpdate(Application&, float /*dt*/) {}
  virtual void OnShutdown(Application&) {}
  virtual std::vector<std::string> Requires() const { return {}; }
};

class ModuleSystem {
 public:
  Status Register(std::unique_ptr<IModule> module);
  Status InitAll(Application& app);
  void UpdateAll(Application& app, float dt);
  void ShutdownAll(Application& app);

  [[nodiscard]] IModule* Find(std::string_view name) const;

 private:
  std::vector<std::unique_ptr<IModule>> modules_;
  bool inited_ = false;
};

}  // namespace engine
