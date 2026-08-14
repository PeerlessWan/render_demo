# Learn 05 — Upload Ring

## 目标

理解 **每帧上传动态几何** 与 GPU in-flight 帧的关系：本 sample 每帧调用 `UploadLitGeometry` 写入 slot 5，模拟 upload ring 的「写入新数据、旧帧仍可能引用旧 buffer」场景。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_05_upload_ring
build\samples\learn\05_upload_ring\Debug\sample_05_upload_ring.exe --headless --headless_frames=2
```

## 代码地图

| 函数 | 说明 |
|---|---|
| `BuildRotatedCube` | CPU 侧生成旋转立方体顶点 |
| `UploadLitGeometry(5, …)` | 上传到自定义 mesh slot |
| `RenderSystem::DrawFrame` | 通过场景 `mesh_id` 解析材质并 draw |

## 必做练习

1. 把上传频率改为「每 3 帧一次」，思考 in-flight 资源何时可释放。
2. 阅读 D3D12 后端 upload heap 实现，画出 triple-buffer 示意。
3. 增大旋转速度，观察是否出现 tearing（GPU 路径）。

## 常见坑

- **重复 Upload 销毁旧 buffer**：引擎可能在 in-flight 完成前保留旧资源；不要在外部 `delete` GPU 资源。
- **Slot 冲突**：slot 0 为默认立方体；自定义几何请用 ≥1 的空 slot。
- **Headless**：上传 API 仍会被调用并记日志，便于 CI 冒烟。
