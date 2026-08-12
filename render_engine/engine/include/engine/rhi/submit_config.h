#pragma once

#include "engine/core/result.h"

namespace engine::rhi {

// M14: toggle for parallel command recording (skeleton flag; single-thread fallback always safe).
struct SubmitConfig {
  bool multithread = false;
  int worker_count = 1;
};

Status ValidateSubmitConfig(const SubmitConfig& cfg);

}  // namespace engine::rhi
