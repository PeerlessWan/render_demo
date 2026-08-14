# Learn 03 — Texture + Depth

## 目标

理解 lit 管线如何同时绑定 **颜色纹理** 与 **深度缓冲**：地面使用 UV 采样，多个立方体按 Z 前后遮挡。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_03_texture_depth
build\samples\learn\03_texture_depth\Debug\sample_03_texture_depth.exe
build\samples\learn\03_texture_depth\Debug\sample_03_texture_depth.exe --headless --headless_frames=2
```

## 代码地图

| 文件 | 说明 |
|---|---|
| `main.cpp` | `Application` + `RenderSystem::DrawFrame`，场景含 `ground` + 两个 `cube` |
| Sandbox CSO | `lit_cube` / `post` / `debug` 等着色器由 `sample_sandbox_shaders` 编译 |

## 必做练习

1. 把第二个立方体 `position.z` 改到相机前方，观察遮挡关系变化。
2. 在 `RenderSystemDesc` 里打开 `enable_shadows`，对比有无阴影时的深度 pass。
3. 用 `--headless --headless_frames=2` 确认 CI 可自动退出。

## 常见坑

- **Shader 路径**：依赖 `CMAKE_BINARY_DIR/samples/Sandbox/shaders`；需先构建 Sandbox 或开启 `ENGINE_BUILD_SAMPLES`。
- **Headless**：CPU stub 不真正采样纹理，但 `DrawFrame` 仍应成功；验收看退出码 0。
- **地面不显示**：检查 `never_cull` 与 `local_bounds` 是否覆盖大地平面。
