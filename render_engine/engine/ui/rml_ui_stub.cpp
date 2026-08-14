#include "engine/ui/rml_ui.h"

namespace engine::ui {

namespace {

#if defined(ENGINE_WITH_RMLUI) && ENGINE_WITH_RMLUI
// Thin adapter: delegates to RetainedUi draw path; marks backend as RmlUi.
// Minimal document load stores source for unit/integration; full Context/skin is later.
class RmlUiThinAdapter final : public RetainedUi {
 public:
  using RetainedUi::RetainedUi;

  bool LoadRmlDocument(std::string_view rml) override {
    if (rml.empty()) {
      document_loaded_ = false;
      document_.clear();
      return false;
    }
    document_.assign(rml.begin(), rml.end());
    document_loaded_ = document_.find('<') != std::string::npos;
    if (document_loaded_) {
      Button("rml.doc", "rml-loaded", 8.f, 8.f, 80.f, 24.f);
    }
    return document_loaded_;
  }

  [[nodiscard]] bool HasRmlDocument() const override { return document_loaded_; }

 private:
  std::string document_;
  bool document_loaded_ = false;
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

bool LoadRmlDocumentFromMemory(RetainedUi& ui, std::string_view rml) {
  return ui.LoadRmlDocument(rml);
}

bool RetainedUiHasRmlDocument(const RetainedUi& ui) {
  return ui.HasRmlDocument();
}

}  // namespace engine::ui
