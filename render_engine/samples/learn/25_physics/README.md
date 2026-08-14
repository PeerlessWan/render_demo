# Learn 25 — 物理世界

## 目标

`CreateDefaultPhysicsWorld` 创建刚体、步进模拟、Raycast 查询（Jolt 或 builtin）。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_25_physics
build\samples\learn\25_physics\Debug\sample_25_physics.exe --headless --headless_frames=2
```

## 代码地图

| 符号 | 说明 |
|---|---|
| `CreateDefaultPhysicsWorld` | Jolt 优先，否则 builtin |
| `Step` / `Raycast` | 模拟与查询 |

## 必做练习

1. 对比 `ENGINE_WITH_JOLT=0/1` 后端名。
2. 用 `MoveCharacter` 推动 dynamic body。
3. 阅读 `physics_factory.cpp` 选择逻辑。

## 常见坑

- **无渲染**：纯物理 API，不创建 Application。
- **质量为 0**：floor 为 static。
