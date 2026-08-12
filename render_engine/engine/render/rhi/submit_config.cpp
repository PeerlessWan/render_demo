#include "engine/rhi/submit_config.h"

namespace engine::rhi {

Status ValidateSubmitConfig(const SubmitConfig& cfg) {
  if (cfg.multithread && cfg.worker_count < 1) {
    return Status::Fail(ErrorCode::InvalidArgument, "multithread submit requires worker_count>=1");
  }
  return Status::Ok();
}

}  // namespace engine::rhi
