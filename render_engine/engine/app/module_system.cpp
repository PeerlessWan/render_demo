#include "engine/app/module.h"

#include "engine/core/log.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace engine {

Status ModuleSystem::Register(std::unique_ptr<IModule> module) {
  if (!module) {
    return Status::Fail(ErrorCode::InvalidArgument, "null module");
  }
  if (Find(module->name())) {
    return Status::Fail("duplicate module: " + std::string(module->name()));
  }
  modules_.push_back(std::move(module));
  return Status::Ok();
}

IModule* ModuleSystem::Find(std::string_view name) const {
  for (const auto& m : modules_) {
    if (m->name() == name) {
      return m.get();
    }
  }
  return nullptr;
}

Status ModuleSystem::InitAll(Application& app) {
  // Kahn topo by Requires().
  std::unordered_map<std::string, int> indeg;
  std::unordered_map<std::string, std::vector<std::string>> adj;
  for (const auto& m : modules_) {
    indeg[std::string(m->name())] = 0;
  }
  for (const auto& m : modules_) {
    for (const auto& req : m->Requires()) {
      if (!Find(req)) {
        return Status::Fail("missing required module: " + req);
      }
      adj[req].push_back(std::string(m->name()));
      ++indeg[std::string(m->name())];
    }
  }
  std::vector<std::string> order;
  std::vector<std::string> ready;
  for (const auto& [n, d] : indeg) {
    if (d == 0) {
      ready.push_back(n);
    }
  }
  while (!ready.empty()) {
    const std::string n = ready.back();
    ready.pop_back();
    order.push_back(n);
    for (const auto& v : adj[n]) {
      if (--indeg[v] == 0) {
        ready.push_back(v);
      }
    }
  }
  if (order.size() != modules_.size()) {
    return Status::Fail("module dependency cycle");
  }

  std::vector<std::unique_ptr<IModule>> sorted;
  for (const auto& n : order) {
    auto it = std::find_if(modules_.begin(), modules_.end(),
                           [&](const auto& m) { return m->name() == n; });
    sorted.push_back(std::move(*it));
    modules_.erase(it);
  }
  modules_ = std::move(sorted);

  for (auto& m : modules_) {
    if (auto st = m->OnInit(app); !st) {
      return st;
    }
    LogInfo(std::string("Module init: ") + std::string(m->name()));
  }
  inited_ = true;
  return Status::Ok();
}

void ModuleSystem::UpdateAll(Application& app, float dt) {
  if (!inited_) {
    return;
  }
  for (auto& m : modules_) {
    m->OnUpdate(app, dt);
  }
}

void ModuleSystem::ShutdownAll(Application& app) {
  for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
    (*it)->OnShutdown(app);
  }
  inited_ = false;
}

}  // namespace engine
