# Learn 10 — Shadow Map

## 目标

开启 **单 cascade CSM 阴影**：`enable_shadows=true` 且 `shadow_cascades=1`，观察地面与立方体的阴影投影。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_10_shadow_map
build\samples\learn\10_shadow_map\Debug\sample_10_shadow_map.exe --headless --headless_frames=2
```

## 代码地图

| 设置 | 值 |
|---|---|
| `RenderSystemDesc::enable_shadows` | true |
| `EffectTuning::shadow_cascades` | 1 |
| Shadow pass | `FrameGraph` 内 `ShadowCSM` pass |

## 必做练习

1. 把 cascade 改为 2/4，对比阴影分辨率与稳定性。
2. 调整 `shadow_bias` 消除 shadow acne。
3. 旋转 `env.sun_direction`，观察阴影方向变化。

## 常见坑

- **需要 shadow CSO**：依赖 Sandbox 编译的 `shadow.vs/ps.cso`。
- **单 cascade 覆盖范围**：过大场景阴影变糊；过小则远处无阴影。
- **Headless**：stub 仍报告 `shadows_enabled()`；GPU 路径才可见阴影。
