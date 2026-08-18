# game_kit 规划

> **前置：** `render_engine` 至少 M4（Scene + Module + 输入）可用；脚本绑定建议等 M8–M9（序列化/UI/资产约定）后做可玩切片。  
> 本层里程碑前缀 **GK**；**不阻塞**引擎 M1–M25，但是工作区 **「游戏可用」的主缺口**（相对 Sandbox 已能出的「渲染可用」）。  
> 口径与落地顺序：[render_engine PLAN §1.9](../../render_engine/docs/PLAN.md)。  
> **品类玩法不进本层排期**；见 [LAYERS](../../docs/LAYERS.md) / `genre_kits`（ADR 0028）。首个游戏可先写在 `games/<title>`。

## 0. 在工作区里排第几

| 口径 | 本层角色 |
|---|---|
| **渲染可用** | 不负责；Sandbox / 引擎看板（如 **W-vk-parity**） |
| **游戏可用** | **GK0–GK3 就是主缺口**：切关、暂停、存档槽、脚本不毁 Device、一条小关走完 |
| **对标主流** | GK4 Prefab + `editor` ED；动画树走引擎 C10 或上层自建——不宣称对齐 UE5 |

**优先于**大气/云/Bindless 全量（那些是引擎加深，不替代本层）。  
**不插队**当前引擎 Doing（现为 Win Vulkan 对标）：下令开 GK 实现波后再写代码。

## 1. 里程碑

| 里程碑 | 目标 | 主要交付 | 验收 |
|---|---|---|---|
| **GK0** 文档与骨架 | 可编译空库 | CMake、空 `IGameModule`、链上引擎清屏 Sample | 能挂 Module 空 Update |
| **GK1** 运行时骨架 | 无脚本也可玩最小逻辑 | LevelFlow、Timer、EventBus、Entity 骨架、存档槽 v0 | C++ 可切关卡+暂停 |
| **GK2** 脚本 MVP | 脚本驱动 | Lua（或选定语言）、白名单绑定、ScriptComponent、错误隔离 | 脚本改位置/播音效；异常不毁 Device |
| **GK3** 可玩 Demo | 垂直切片 | 移动、触发器、UI HUD、热重载（Debug） | 第三人称或 2D 小关可走完 |
| **GK4** Prefab + 打磨 | 内容友好 | Prefab 实例化、与 Manifest 依赖、文档与 Sample | editor/C20 可消费同一 Prefab |
| **GK5** 可选加深 | 按需 | 协程、`IScriptHost` 对接（C19）、简单 AI 骨架 | 单独立项（已接线） |

## 2. 与引擎里程碑建议对齐

| 引擎 | game_kit |
|---|---|
| M1–M3 | 仅文档 / GK0 准备 |
| M4–M6 | GK0–GK1 |
| M8–M9 | GK2 |
| M15–M16 | GK3（UI/2D 更完整时） |
| 之后 | GK4–GK5 |

## 3. 进度

| 里程碑 | 状态 |
|---|---|
| GK0–GK3 | 已写；`game_kit_tests` / samples 可 headless |
| GK4 Prefab | `PrefabDocument` + `Instantiate`（可挂脚本）+ `samples/prefab_place` |
| GK5 | 已写接线：Lua 协程 `wait`/`start_coroutine`、`AiState` 挂实体、`GameKitScriptHost` |

## 4. 相关

- [FEATURES.md](FEATURES.md)  
- [GAPS.md](GAPS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/PLAN.md](../../render_engine/docs/PLAN.md) **§1.9**  
- [../../editor/docs/PLAN.md](../../editor/docs/PLAN.md)（P1，可后于 GK3）  
