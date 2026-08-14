# Learn 15 — 反射探针与简化 GI

## 目标

CPU 侧 `ProbeVolume` 采样 + `ReflectionProbe` 上传 cubemap，理解 M6/M8 GI 数据流。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_15_probes_gi
build\samples\learn\15_probes_gi\Debug\sample_15_probes_gi.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `ProbeVolume::Configure` / `UpdateFromLights` | 简化 diffuse GI |
| `ReflectionProbe::UpdateFromEnvironment` | 动态 cubemap 占位 |
| `IDevice::UploadReflectionCubemap` | GPU 上传 |

## 必做练习

1. 移动 probe light，观察 `Sample()` 颜色变化。
2. 对比 reflection probe 与 IBL 路径差异。
3. 阅读 `probe_volume.cpp` 插值逻辑。

## 常见坑

- **GI 为 CPU 占位**：非 Lumen/RTX GI；重在数据契约。
- **Vulkan**：IBL 上传 accepted stub，采样 parity TBD。
