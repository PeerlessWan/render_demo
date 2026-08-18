# 脚本系统（game_kit）

## 1. 目标

在 **品类无关** 玩法壳中提供 **可热更、可绑定、主线程安全** 的脚本环境，驱动游戏逻辑，而不侵入 `render_engine` 后端。品类系统（对话/射击等）不进本层，见 [LAYERS](../../docs/LAYERS.md)。

## 2. 架构

```text
Script Source (.lua 等)
        │
        ▼
   VM Runtime（语言实现，经本层封装）
        │
        ▼
   Bindings（白名单） ──► Engine Host Facade ──► render_engine 公开 API
        │
        ▼
   ScriptComponent / 全局 Game 脚本
```

默认语言：**Lua 5.4**（可换 Luau）；更换 = 换 `script/vm` + 绑定，Facade 不变。实现前若改语言，补 game_kit ADR。  

绑定只经 [HOST_API.md](../../render_engine/docs/HOST_API.md)。  

默认绑定（Lua 全局函数）：`log`、`set_pos`/`get_pos`（可省略名字则用 self）、`set_visible`、`get_children`、`destroy_entity`、`instantiate`、`request_level`/`current_level`、`publish`/`subscribe`、`delay`/`interval`/`cancel_timer`、`set_paused`/`set_time_scale`/`get_time_scale`、`key_down`/`axis`/`pressed`、`raycast`（无物理则 miss）、`ui_set_text`/`ui_set_visible`、`play_wav`/`stop_audio`、`request_load`/`asset_ready`、`set_ai`/`get_ai`、`play_anim`（无骨骼宿主时 false）、`wait`/`start_coroutine`。  

每 VM 一份 Host（Lua extraspace），多 `ScriptComponent` 不会互相覆盖。  

- **不进引擎核心：** 与 ADR 0027 / HOSTING 方案 A 一致；game_kit 实现薄 `GameKitScriptHost`（C19）。  
- **Host API：** 绑定只经 [HOST_API.md](../../render_engine/docs/HOST_API.md)。  

## 3. 功能

| 能力 | 说明 |
|---|---|
| 加载/卸载脚本模块 | 按关卡或全局；失败可诊断 |
| 组件脚本 | `on_init` / `on_update` / `on_destroy`；可选 `on_trigger_*` |
| 全局服务脚本 | GameMode / 关卡导演 |
| 调用引擎 | 仅白名单：Node、Handle 加载、Action、UI、Raycast、Audio、Timer、事件 |
| 热重载 | Debug：改文件 → 重载模块；Release 可关 |
| 沙箱 | 禁文件系统乱写、禁加载任意原生库（产品策略可配） |
| 错误 | pcall 边界；日志 + 可选冻结该组件 |

## 4. 绑定白名单 / 黑名单

**允许（经 Facade）：**

- Node：位置/旋转/缩放、父子、显隐、查子节点  
- 生成/销毁实体（Prefab / 模板）  
- Animation：`play_anim` 无宿主返回 false（本层不接骨骼）  
- Audio：Play/Stop/增益（可选注入 `IAudioDevice`，否则 `PlayWavFile`）  
- UI：显隐、文本（可选注入 Retained UI）  
- Physics：Raycast（可选注入）；Trigger 为本层 AABB 约定  
- Assets：`request_load`、`asset_ready`  
- Input：读 Action / 轴  
- Timer / EventBus  
- LevelFlow：请求切关卡  

**禁止：**

- 创建/枚举 Device、Swapchain、改 PSO/描述符  
- 直接 `#include` 后端或 Jolt/ImGui 等三方头  
- 在 VM 线程外未入队情况下写 Scene  
- 绕过 Handle 持有 GPU 裸指针  

完整原则对齐：[HOSTING.md §5.4](../../render_engine/docs/HOSTING.md)。

## 5. 与帧、寿命

1. 所有绑定调用默认发生在 **Module.OnUpdate**（或显式主线程队列）。  
2. 异步加载完成：只在引擎 `Asset.PumpAsync` 之后投递脚本回调。  
3. Shutdown：先停 VM → 取消回调 → 再让引擎 Stopping（RUNTIME_FOUNDATIONS）。  
4. Handle：脚本侧持逻辑 ID/轻量 ref；销毁后访问 → 脚本错误而非进程崩溃。

## 6. 约束摘要

见 [CONSTRAINTS.md](CONSTRAINTS.md)。关键：单线程脚本默认；热重载不保证所有游戏状态可迁移。

## 7. 规划

见 [PLAN.md](PLAN.md) GK1–GK3。缺口见 [GAPS.md](GAPS.md)。

## 8. 相关

- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [FEATURES.md](FEATURES.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md](../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md)  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
