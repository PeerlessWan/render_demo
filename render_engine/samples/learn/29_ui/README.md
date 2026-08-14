# Learn 29 — ImGui + Retained HUD

## 目标

同时使用 `ImmediateUi`（Dear ImGui 门面）与 `RetainedUi` HUD，理解输入捕获分工（M8/M15）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_29_ui
build\samples\learn\29_ui\Debug\sample_29_ui.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `ImmediateUi::Init` | ui_imgui CSO |
| `CreateRetainedUiBackend` | 保留模式 widget |
| `want_capture_*` | 与 ActionMap 分工 |

## 必做练习

1. 在 Retained UI 上 Pump 鼠标点击事件。
2. 对比 `ENGINE_WITH_IMGUI=0` 时 ImmediateUi 行为。
3. 阅读 Sandbox 中 UI 与 camera 捕获逻辑。

## 常见坑

- **ImGui 可选**：无 ImGui 时 Init 可能 warn 但仍 exit 0。
- **ui CSO**：依赖 Sandbox shader 编译。
