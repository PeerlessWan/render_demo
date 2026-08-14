# Learn 07 — Scene + Camera

## 目标

用 **`World::CreateNode` + `MeshRenderer` + `RenderSceneExtractor`** 把场景图变成可绘制实例列表；观察相机与视锥剔除对 `instances` / `culled` 计数的影响。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_07_scene_camera
build\samples\learn\07_scene_camera\Debug\sample_07_scene_camera.exe --headless --headless_frames=2
```

## 代码地图

| 组件 | 说明 |
|---|---|
| `scene::World` | 节点、Transform、MeshRenderer |
| `Application::render_scene()` | 每帧自动 Extract（见 `application.cpp`） |
| `RenderSystem::DrawFrame` | 消费 `RenderScene` 实例 |

## 必做练习

1. 把立方体移出视锥外，确认日志里 `culled` 增加。
2. 给 ground 去掉 `never_cull`，对比实例数变化。
3. 用 WASD 移动相机（`Application` 默认 fly 控制），观察 instance 不变但画面变化。

## 常见坑

- **`UpdateTransforms`**：`Application` 已每帧调用；手动改 Transform 后需确保 world matrix 刷新。
- **空 instances**：节点未 `set_mesh` 或 `visible=false` 不会进入 RenderScene。
- **Headless**：仍打印 instance count，便于无窗口 CI。
