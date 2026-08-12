#pragma once

#include "engine/assets/asset_handle.h"
#include "engine/assets/manifest.h"
#include "engine/core/result.h"

#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine::assets {

using LoadCallback = std::function<void(Status status, AssetHandle handle)>;

// Async load + main-thread Pump (RUNTIME_FOUNDATIONS §3). M3 skeleton.
class AssetSystem {
 public:
  AssetSystem();
  ~AssetSystem();

  AssetSystem(const AssetSystem&) = delete;
  AssetSystem& operator=(const AssetSystem&) = delete;

  void AddRoot(std::filesystem::path root);
  Status SetManifest(Manifest manifest);

  // Request load (and deps). Completion/failure callbacks run only on PumpAsync (main thread).
  AssetHandle RequestLoad(const AssetId& id, LoadCallback callback = {});
  void Cancel(const AssetHandle& handle);

  // Drain completed jobs; invoke callbacks. Call from Application main loop after Asset phase.
  void PumpAsync();

  [[nodiscard]] const Manifest& manifest() const { return manifest_; }

 private:
  struct Job {
    AssetId id;
    std::shared_ptr<AssetRecord> record;
    LoadCallback callback;
    bool cancel = false;
  };

  struct Completion {
    std::shared_ptr<AssetRecord> record;
    LoadCallback callback;
    Status status = Status::Ok();
  };

  void WorkerMain();
  Status LoadRecord(const AssetId& id, AssetRecord& out);
  std::filesystem::path ResolvePath(const std::string& relative) const;

  Manifest manifest_;
  std::vector<std::filesystem::path> roots_;

  std::mutex mutex_;
  std::vector<Job> pending_;
  std::vector<Completion> completed_;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace engine::assets
