#pragma once

#include "engine/ui/retained_ui.h"

#include <memory>
#include <string>
#include <string_view>

namespace engine::ui {

// RmlUi adapter (M15). With ENGINE_WITH_RMLUI: thin adapter + minimal document load.
struct RetainedUiBackendInfo {
  const char* name = "retained";
  bool is_rml = false;
};

std::unique_ptr<RetainedUi> CreateRetainedUiBackend();
[[nodiscard]] RetainedUiBackendInfo QueryRetainedUiBackend();

// Minimal document path (Auto-wave). Returns false when RmlUi is not linked or load fails.
[[nodiscard]] bool LoadRmlDocumentFromMemory(RetainedUi& ui, std::string_view rml);
[[nodiscard]] bool RetainedUiHasRmlDocument(const RetainedUi& ui);

}  // namespace engine::ui
