# game_kit 规划

> **前置：** `render_engine` 至少 M4（Scene + Module + 输入）可用；脚本绑定建议等 M8–M9（序列化/UI/资产约定）后做可玩切片。  
> 本层里程碑前缀 **GK**；不阻塞引擎 M1–M25。  
> **品类玩法不进本层排期**；见 [LAYERS](../../docs/LAYERS.md) / `genre_kits`（ADR 0028）。首个游戏可先写在 `games/<title>`。

## 1. 里程碑

| 里程碑 | 目标 | 主要交付 | 验收 |
|---|---|---|---|
| **GK0** 文档与骨架 | 可编译空库 | CMake、空 `IGameModule`、链上引擎清屏 Sample | 能挂 Module 空 Update |
| **GK1** 运行时骨架 | 无脚本也可玩最小逻辑 | LevelFlow、Timer、EventBus、Entity 骨架、存档槽 v0 | C++ 可切关卡+暂停 |
| **GK2** 脚本 MVP | 脚本驱动 | Lua（或选定语言）、白名单绑定、ScriptComponent、错误隔离 | 脚本改位置/播音效；异常不毁 Device |
| **GK3** 可玩 Demo | 垂直切片 | 移动、触发器、UI HUD、热重载（Debug） | 第三人称或 2D 小关可走完 |
| **GK4** Prefab + 打磨 | 内容友好 | Prefab 实例化、与 Manifest 依赖、文档与 Sample | editor/C20 可消费同一 Prefab |
| **GK5** 可选加深 | 按需 | 协程、`IScriptHost` 对接（C19）、简单 AI 骨架 | 单独立项 |

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
| GK0–GK5 | 未开始（文档先行） |

## 4. 相关

- [FEATURES.md](FEATURES.md)  
- [GAPS.md](GAPS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/PLAN.md](../../render_engine/docs/PLAN.md)  
