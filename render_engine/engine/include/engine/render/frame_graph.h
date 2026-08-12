#pragma once

#include "engine/core/result.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace engine::render {

using PassId = std::uint32_t;

// Minimal FrameGraph: declare passes with resource read/write names, topo-sort, execute.
class FrameGraph {
 public:
  using ExecuteFn = std::function<void()>;

  PassId AddPass(std::string name,
                 std::vector<std::string> reads,
                 std::vector<std::string> writes,
                 ExecuteFn execute);

  // Explicit ordering edge: `before` must run before `after` (also enables cycle tests).
  Status AddDependency(PassId before, PassId after);

  Status Compile();
  Status Execute();
  void Reset();

  [[nodiscard]] const std::vector<PassId>& order() const { return order_; }

 private:
  struct Pass {
    std::string name;
    std::vector<std::string> reads;
    std::vector<std::string> writes;
    ExecuteFn execute;
  };

  std::vector<Pass> passes_;
  std::vector<std::pair<PassId, PassId>> explicit_deps_;
  std::vector<PassId> order_;
  bool compiled_ = false;
};

}  // namespace engine::render
