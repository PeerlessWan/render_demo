#include "engine/render/frame_graph.h"

#include <queue>
#include <unordered_map>

namespace engine::render {

PassId FrameGraph::AddPass(std::string name,
                           std::vector<std::string> reads,
                           std::vector<std::string> writes,
                           ExecuteFn execute) {
  compiled_ = false;
  Pass pass;
  pass.name = std::move(name);
  pass.reads = std::move(reads);
  pass.writes = std::move(writes);
  pass.execute = std::move(execute);
  passes_.push_back(std::move(pass));
  return static_cast<PassId>(passes_.size() - 1);
}

Status FrameGraph::AddDependency(PassId before, PassId after) {
  if (before >= passes_.size() || after >= passes_.size()) {
    return Status::Fail(ErrorCode::InvalidArgument, "FrameGraph dependency out of range");
  }
  compiled_ = false;
  explicit_deps_.emplace_back(before, after);
  return Status::Ok();
}

Status FrameGraph::Compile() {
  order_.clear();
  const auto n = passes_.size();
  if (n == 0) {
    compiled_ = true;
    return Status::Ok();
  }

  // Edge: last writer of a resource -> later reader/writer. Plus explicit deps.
  std::vector<std::vector<PassId>> adj(n);
  std::vector<int> indeg(n, 0);

  auto add_edge = [&](PassId from, PassId to) {
    if (from == to) {
      return;
    }
    adj[from].push_back(to);
    ++indeg[to];
  };

  std::unordered_map<std::string, PassId> last_writer;
  for (PassId i = 0; i < n; ++i) {
    for (const auto& r : passes_[i].reads) {
      if (auto it = last_writer.find(r); it != last_writer.end()) {
        add_edge(it->second, i);
      }
    }
    for (const auto& w : passes_[i].writes) {
      if (auto it = last_writer.find(w); it != last_writer.end()) {
        add_edge(it->second, i);
      }
      last_writer[w] = i;
    }
  }
  for (const auto& [before, after] : explicit_deps_) {
    add_edge(before, after);
  }

  std::priority_queue<PassId, std::vector<PassId>, std::greater<PassId>> ready;
  for (PassId i = 0; i < n; ++i) {
    if (indeg[i] == 0) {
      ready.push(i);
    }
  }

  while (!ready.empty()) {
    const PassId u = ready.top();
    ready.pop();
    order_.push_back(u);
    for (PassId v : adj[u]) {
      if (--indeg[v] == 0) {
        ready.push(v);
      }
    }
  }

  if (order_.size() != n) {
    return Status::Fail("FrameGraph has a cycle");
  }
  compiled_ = true;
  return Status::Ok();
}

Status FrameGraph::Execute() {
  if (!compiled_) {
    if (auto st = Compile(); !st) {
      return st;
    }
  }
  for (PassId id : order_) {
    if (passes_[id].execute) {
      passes_[id].execute();
    }
  }
  return Status::Ok();
}

void FrameGraph::Reset() {
  passes_.clear();
  explicit_deps_.clear();
  order_.clear();
  compiled_ = false;
}

}  // namespace engine::render
