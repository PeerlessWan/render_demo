# Learn 26 — P1 后处理与动态反射

## 目标

开启 SSR / DoF / MotionBlur / VolumetricFog + `ReflectionProbe` 上传（M13 P1）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_26_p1_post_reflect
build\samples\learn\26_p1_post_reflect\Debug\sample_26_p1_post_reflect.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `EffectTuning::enable_ssr` 等 | P1 运行时开关 |
| `ReflectionProbe` | 动态 cubemap CPU→GPU |

## 必做练习

1. 逐项关闭 post pass，观察 FrameGraph 变化。
2. 调整 `reflection_intensity`。
3. 对比 Sandbox 中完整 P1 栈。

## 常见坑

- **Headless stub**：post/反射 resolve 可能 no-op；配置仍应成功。
- **金属 mesh**：使用 `metal` mesh_id 便于观察 specular。
