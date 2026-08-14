#include "engine/ui/rml_ui.h"

namespace engine::ui {

namespace {

#if defined(ENGINE_WITH_RMLUI) && ENGINE_WITH_RMLUI
// Thin adapter: delegates to RetainedUi draw path; marks backend as RmlUi.
// Full RML document parse / skin editor is intentionally out of this wave.
class RmlUiThinAdapter final : public RetainedUi {
 public:
  using RetainedUi::RetainedUi;
};
#endif

}  // namespace

std::unique_ptr<RetainedUi> CreateRetainedUiBackend() {
#if defined(ENGINE_WITH_RMLUI) && ENGINE_WITH_RMLUI
  return std::make_unique<RmlUiThinAdapter>();
#else
  return std::make_unique<RetainedUi>();
#endif
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
