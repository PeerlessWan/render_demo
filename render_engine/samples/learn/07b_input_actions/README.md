# Learn 07b — Input Actions

## 目标

演示 **`InputSystem` + `ActionMap`** 如何把物理按键映射为逻辑动作（Fire / Sprint）；headless 下跳过交互，仅验证主循环退出。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_07b_input_actions
build\samples\learn\07b_input_actions\Debug\sample_07b_input_actions.exe
build\samples\learn\07b_input_actions\Debug\sample_07b_input_actions.exe --headless --headless_frames=2
```

## 代码地图

| API | 说明 |
|---|---|
| `ActionMap::Bind` | `"Fire" → "Button:Space"` |
| `InputSystem::pressed` | 本帧刚按下 |
| `Application::Run` | 内部 `SyncInputFromWindow` + `EvaluateActions` |

## 必做练习

1. 窗口模式下按 Space，观察日志 `Action Fire pressed`。
2. 把 Fire 绑定到 `"Button:Mouse0"`，对比行为。
3. 用 `ActionMap::SaveToFile` / `LoadFromFile` 持久化绑定（参考 unit test）。

## 常见坑

- **Headless 无输入**：`is_headless()` 时本 sample 故意跳过交互逻辑。
- **Binding 语法**：须与 `input_system.cpp` 解析器一致（`Button:` / `Axis:` 前缀）。
- **Shift 键**：`Sprint` 绑定示例需引擎支持对应 token；W 键走 `key_down` 演示。
