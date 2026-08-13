#include "engine/ui/rml_ui.h"

namespace engine::ui {

std::unique_ptr<RetainedUi> CreateRetainedUiBackend() {
  // ENGINE_WITH_RMLUI would construct RmlUi adapter here.
  return std::make_unique<RetainedUi>();
}

RetainedUiBackendInfo QueryRetainedUiBackend() {
  RetainedUiBackendInfo info;
#if defined(ENGINE_WITH_RMLUI) && ENGINE_WITH_RMLUI
  info.name = "rmlui";
  info.is_rml = true;
#else
  info.name = "retained-fallback";
  info.is_rml = false;
#endif
  return info;
}

}  // namespace engine::ui
