#pragma once

#include "engine/ui/retained_ui.h"

#include <memory>
#include <string_view>

namespace engine::ui {

// RmlUi adapter placeholder (M15). Until RmlUi is vendored, returns the built-in RetainedUi.
struct RetainedUiBackendInfo {
  const char* name = "retained";
  bool is_rml = false;
};

std::unique_ptr<RetainedUi> CreateRetainedUiBackend();
[[nodiscard]] RetainedUiBackendInfo QueryRetainedUiBackend();

}  // namespace engine::ui
