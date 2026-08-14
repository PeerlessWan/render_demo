# Learn 11 — Frame Graph

## 目标

通过 **`RenderSystem::DrawFrame`** 驱动内置 `FrameGraph`：每帧声明 Shadow / Lit / Post 等 pass，拓扑排序后执行；默认跑 2 帧并打印 pass 数量。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_11_frame_graph
build\samples\learn\11_frame_graph\Debug\sample_11_frame_graph.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `RenderSystem::frame_graph()` | 访问内部 FrameGraph |
| `FrameGraph::AddPass` / `Compile` / `Execute` | 见 `render_system.cpp` |
| `order()` | 编译后的 pass 执行顺序 |

## 必做练习

1. 阅读 `render_system.cpp` 中 `graph_.AddPass` 列表，画出依赖图。
2. 打开 shadows 后对比 pass 数量变化。
3. 尝试在独立单元里构造最小 FrameGraph（见 `frame_graph.h`）。

## 常见坑

- **每帧 Reset**：`DrawFrame` 内会 `graph_.Reset()`，不要缓存上一帧的 `order()` 指针。
- **Pass 名仅调试**：资源读写边才是排序依据。
- **默认 2 帧**：未传 `--headless` 时窗口模式需手动关闭；传 `--headless` 自动退出。
