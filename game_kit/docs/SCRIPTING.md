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

默认绑定源：[`script/bindings/api.json`](../script/bindings/api.json) 生成 `lua_api_reg.inc`。含 `ui_label`/`ui_button`/`ui_panel`/`ui_toggle`/`ui_slider`/`ui_layout`、`bake_nav`/`bake_nav_world`/`find_path`/`nav_agent`。  

每 VM 一份 Host（Lua extraspace）。pcall 走 `luaL_traceback`。`set_instruction_budget` 用 count hook 打断死循环。`set_debug_hooks(true)` 记录 `last_line` / `last_chunk`，`ScriptDebugger` 行断点在 hook 内调 `on_break` 可读 locals；`DapSession` 可 `Handle` DAP JSON（命中后 hook 内阻塞直到 `continue`）。热重载可保留 `persist`。`wait_event(topic)` 挂起协程直到 EventBus 发布该主题。打开 base 后剥掉 `load`/`loadfile`/`dofile`。  

- **不进引擎核心：** 与 ADR 0027 / HOSTING 方案 A 一致；game_kit 实现薄 `GameKitScriptHost`（C19）。  
- **Host API：** 绑定只经 [HOST_API.md](../../render_engine/docs/HOST_API.md)。  

## 3. 功能

| 能力 | 说明 |
|---|---|
| 加载/卸载脚本模块 | `import('a.b')` → `script_root/a/b.lua`；`_GK_LOADED` 缓存 |
| 组件脚本 | `on_init` / `on_update` / `on_destroy`；`on_trigger_*` / `on_collision_*` / `on_anim_notify` |
| 全局服务脚本 | GameMode / 关卡导演；`on_asset_ready` |
| 调用引擎 | 仅白名单：Node、Handle 加载、Action、UI、Raycast、Audio、Timer、事件、Anim/Nav/Mixer/Snapshot |
| 热重载 | Debug：改文件 → 重载模块；可选保留 `persist` |
| 沙箱 | 禁文件系统乱写、禁加载任意原生库（产品策略可配） |
| 错误 | pcall + traceback；日志 + 冻结该组件 |

## 4. 绑定白名单 / 黑名单

**允许（经 Facade）：**

- Node：位置/旋转/缩放、父子、显隐、查子节点  
- 生成/销毁实体（Prefab / 模板）  
- Animation：`play_anim` 经 `AnimPlayer`（无骨骼 clip 也可播；Notify / 根运动）  
- Audio：Play/Stop/增益；`mixer_*` 距离衰减  
- UI：显隐、文本、panel/button/toggle/slider、`ui_layout` 列布局；点击/开关进 EventBus  
- Physics：Raycast（可选注入）；Trigger 为本层 AABB；body AABB 接触 → `on_collision_*`  
- Assets：`request_load`、`asset_ready`、`on_asset_ready`  
- Input：读 Action / 轴  
- Timer / EventBus  
- LevelFlow：请求切关卡（Replace/Additive + delay）  
- Nav / Timeline / Snapshot / Loopback 薄层  

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

见 [CONSTRAINTS.md](CONSTRAINTS.md)。关键：单线程脚本默认；热重载 `persist` 可迁移，不保证任意游戏状态可迁移。

## 7. 规划

见 [PLAN.md](PLAN.md) GK1–GK3。缺口见 [GAPS.md](GAPS.md)。

## 8. 相关

- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [FEATURES.md](FEATURES.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md](../../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md)  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
