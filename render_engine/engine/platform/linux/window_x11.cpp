// Mega-W9: optional TU so CMake can list platform/linux/ without requiring X11 on Windows.
// Real symbols live as inline stubs in engine/platform/linux/window_x11.h (#ifdef __linux__).

#include "engine/platform/linux/window_x11.h"

namespace engine::platform::linux_x11 {

// Anchor for the translation unit (keeps MSVC/lld happy when the lib is listed).
volatile int g_window_x11_stub_anchor = 0;

}  // namespace engine::platform::linux_x11
