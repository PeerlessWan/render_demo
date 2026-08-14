# Learn 16 — 后处理栈

## 目标

通过 `PostStack` + `EffectTuning` 开启 Bloom / Tonemap / AutoExposure，理解 M6–M7 后处理编排。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_16_post_stack
build\samples\learn\16_post_stack\Debug\sample_16_post_stack.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `RenderSystem::post_stack()` | 命名 pass 开关 |
| `set_post_enabled` | 运行时 toggle |
| `ResolvePostEffects` | FrameGraph Post pass |

## 必做练习

1. 关闭 Bloom，对比 pass 列表变化。
2. 调整 `exposure` 与 `auto_exposure_key`。
3. 阅读 `post_stack.cpp` 中 pass 名映射。

## 常见坑

- **Headless stub**：post resolve 计数成功；画面需 GPU 窗口。
- **Tonemap 默认开**：HDR lit 必须 resolve 到 LDR swapchain。
