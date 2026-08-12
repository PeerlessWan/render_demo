#include "engine/assets/asset_system.h"

#include "engine/assets/path_resolver.h"
#include "engine/core/log.h"

#include <chrono>
#include <fstream>

namespace engine::assets {

AssetSystem::AssetSystem() : worker_([this] { WorkerMain(); }) {}

AssetSystem::~AssetSystem() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  if (worker_.joinable()) {
    worker_.join();
  }
}

void AssetSystem::AddRoot(std::filesystem::path root) { roots_.push_back(std::move(root)); }

Status AssetSystem::SetManifest(Manifest manifest) {
  manifest_ = std::move(manifest);
  return Status::Ok();
}

std::filesystem::path AssetSystem::ResolvePath(const std::string& relative) const {
  if (!is_safe_relative(relative)) {
    return {};
  }
  for (const auto& root : roots_) {
    auto candidate = root / relative;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) && !ec) {
      return candidate;
    }
  }
  return {};
}

Status AssetSystem::LoadRecord(const AssetId& id, AssetRecord& out) {
  const ManifestEntry* entry = manifest_.Find(id);
  if (!entry) {
    return Status::Fail(ErrorCode::NotFound, "Asset not in manifest: " + id.value());
  }
  for (const auto& dep : entry->deps) {
    if (!manifest_.Find(dep)) {
      return Status::Fail(ErrorCode::Failed,
                          "Missing dependency '" + dep.value() + "' for " + id.value());
    }
    // Ensure dependency file exists (diagnostic; full graph load can be expanded later).
    const ManifestEntry* dep_entry = manifest_.Find(dep);
    if (ResolvePath(dep_entry->path).empty()) {
      return Status::Fail(ErrorCode::Failed,
                          "Dependency file missing: " + dep.value() + " path=" + dep_entry->path);
    }
  }

  const auto path = ResolvePath(entry->path);
  if (path.empty()) {
    return Status::Fail(ErrorCode::NotFound, "Asset file not found: " + entry->path);
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return Status::Fail("Cannot open asset file: " + path.string());
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<std::streamoff>(in.tellg());
  in.seekg(0, std::ios::beg);
  out.bytes.resize(size > 0 ? static_cast<std::size_t>(size) : 0);
  if (!out.bytes.empty()) {
    in.read(reinterpret_cast<char*>(out.bytes.data()), static_cast<std::streamsize>(out.bytes.size()));
  }
  out.id = id;
  out.state = AssetState::Ready;
  out.error.clear();
  return Status::Ok();
}

AssetHandle AssetSystem::RequestLoad(const AssetId& id, LoadCallback callback) {
  auto record = std::make_shared<AssetRecord>();
  record->id = id;
  record->state = AssetState::Pending;
  AssetHandle handle(record);

  {
    std::lock_guard lock(mutex_);
    pending_.push_back(Job{id, record, std::move(callback), false});
  }
  return handle;
}

void AssetSystem::Cancel(const AssetHandle& handle) {
  if (!handle.valid()) {
    return;
  }
  std::lock_guard lock(mutex_);
  handle.record()->state = AssetState::Cancelled;
  for (auto& job : pending_) {
    if (job.record == handle.record()) {
      job.cancel = true;
    }
  }
}

void AssetSystem::PumpAsync() {
  std::vector<Completion> local;
  {
    std::lock_guard lock(mutex_);
    local.swap(completed_);
  }
  for (auto& c : local) {
    if (c.callback) {
      c.callback(c.status, AssetHandle(c.record));
    }
  }
}

void AssetSystem::WorkerMain() {
  using namespace std::chrono_literals;
  for (;;) {
    Job job;
    {
      std::unique_lock lock(mutex_);
      if (stop_ && pending_.empty()) {
        return;
      }
      if (pending_.empty()) {
        lock.unlock();
        std::this_thread::sleep_for(2ms);
        continue;
      }
      job = std::move(pending_.front());
      pending_.erase(pending_.begin());
    }

    Completion completion;
    completion.record = job.record;
    completion.callback = std::move(job.callback);

    if (job.cancel || job.record->state == AssetState::Cancelled) {
      job.record->state = AssetState::Cancelled;
      completion.status = Status::Fail(ErrorCode::Failed, "cancelled");
    } else {
      AssetRecord loaded;
      auto st = LoadRecord(job.id, loaded);
      if (job.record->state == AssetState::Cancelled) {
        completion.status = Status::Fail(ErrorCode::Failed, "cancelled");
      } else if (!st) {
        job.record->state = AssetState::Failed;
        job.record->error = st.message();
        completion.status = st;
      } else {
        job.record->bytes = std::move(loaded.bytes);
        job.record->state = AssetState::Ready;
        job.record->error.clear();
        completion.status = Status::Ok();
      }
    }

    {
      std::lock_guard lock(mutex_);
      completed_.push_back(std::move(completion));
      if (stop_ && pending_.empty()) {
        return;
      }
    }
  }
}

}  // namespace engine::assets
