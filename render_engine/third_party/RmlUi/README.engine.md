# RmlUi (engine vendor)

Pinned: **6.0** (https://github.com/mikke89/RmlUi)

- License: MIT (`LICENSE.txt`)
- Path: `third_party/RmlUi`
- CMake: `ENGINE_WITH_RMLUI` (auto ON when headers present)
- Integration: thin `RetainedUi` adapter via `CreateRetainedUiBackend` / `QueryRetainedUiBackend` (`is_rml=true`). Full RML document/skin path is a later wave — this checkout unblocks the factory/tests.
- **M15 100%口径**：真 RmlUi 外置/薄适配（或 stub）为已接受交付；完整文档引擎非本波阻塞项。

Do not `#include` RmlUi from samples; go through `engine/ui/rml_ui.h`.
