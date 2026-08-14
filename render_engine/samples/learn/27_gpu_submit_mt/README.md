# Learn 27 — 多线程 GPU 提交

## 目标

调用 `SetSubmitConfig` 开启 multithread 录制偏好，配合 `QualitySettings::multithread_submit`（M14 P1）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_27_gpu_submit_mt
build\samples\learn\27_gpu_submit_mt\Debug\sample_27_gpu_submit_mt.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `SubmitConfig` | worker_count / multithread |
| `RenderSystem` DrawFrame | 内部也会 SetSubmitConfig |

## 必做练习

1. 设 `worker_count=0` 观察 ValidateSubmitConfig 失败。
2. 阅读 `submit_config.cpp` 校验规则。
3. 对比 D3D12 真并行与 headless stub。

## 常见坑

- **骨架 flag**：后端可仍单线程 fallback。
- **Bindless/HDR**：Feature 门控默认关。
