# Learn 24 — 点光阴影 / TAA / SSAO

## 目标

组合 **局部光 shadow atlas** 与 **SSAO + TAA** 后处理开关（M11 P0）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_24_local_shadows_taa_ao
build\samples\learn\24_local_shadows_taa_ao\Debug\sample_24_local_shadows_taa_ao.exe --headless --headless_frames=2
```

## 代码地图

| 设置 | 说明 |
|---|---|
| `LocalLight::cast_shadow` | 点光立方体 shadow |
| `enable_ssao` / `enable_taa` | Post resolve 路径 |

## 必做练习

1. 关闭 TAA，观察 jitter 行为差异。
2. 增加第二个 cast_shadow 点光。
3. 阅读 `local_lights.h` 与 shadow atlas 调度。

## 常见坑

- **Headless**：local shadow pass 可能 stub；日志仍可验证配置。
- **透明排序**：本 sample 未启用透明 pass。
