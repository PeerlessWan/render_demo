# game_kit 功能清单

> 状态列：`规划` = 文档已定目标；`已写` = 代码落地；`部分` = 骨架或可选注入。

## 1. 玩法运行时

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| GKRT01 | 关卡流 | Load/Unload、切换；`ClearPlayState` 在切关时清理 | 已写 |
| GKRT02 | 暂停 / 时间缩放 | `paused` + `time_scale`；`logic_dt` 驱动逻辑 | 已写 |
| GKRT03 | Entity / ScriptComponent | 挂在引擎 Node；`on_init` / `on_update` / `on_destroy` | 已写 |
| GKRT04 | 定时器 | delay / interval；主线程触发；Lua `delay`/`interval` | 已写 |
| GKRT05 | 事件总线 | 玩法内 pub/sub；Lua `publish`/`subscribe` | 已写 |
| GKRT06 | 玩法存档 | 读写槽位；不含完整 RPG 内容格式强制 | 已写 |
| GKRT07 | 触发器约定 | AABB 体积；enter/leave → EventBus + `on_trigger_*` | 已写 |
| GKRT08 | 玩家控制器骨架 | WASD + 跟随相机；可选 `MoveCharacter` | 已写 |
| GKRT09 | Prefab 实例化 | 生成 Node；可选挂 ScriptComponent | 已写 |

## 2. 脚本系统

详见 [SCRIPTING.md](SCRIPTING.md)。摘要：

| ID | 功能 | 状态 |
|---|---|---|
| GKSC01 | 语言运行时（默认 Lua，可换） | 已写 |
| GKSC02 | 绑定白名单（Scene/Input/UI/Physics/Audio/Asset） | 已写（Animation 为 no-op） |
| GKSC03 | 脚本热重载（开发构建） | 已写 |
| GKSC04 | 错误隔离（脚本异常不崩 Device） | 已写 |
| GKSC05 | 调试钩子（日志、断点对接可选） | 部分（`lua_sethook` call 日志） |
| GKSC06 | 协程 / yield 到下一帧（可选） | 已写（`wait` / `start_coroutine`） |

## 3. 明确不做（本层）

| 项 | 去向 |
|---|---|
| RHI / 后处理 / GI | render_engine |
| 对话 / 背包 / 任务 / 战斗等 RPG 系统 | [`genre_kits/rpg_kit`](../../genre_kits/README.md) 或 `games/<title>` |
| 武器 / Hitscan / 射击循环 | [`genre_kits/shooter_kit`](../../genre_kits/README.md) 或 `games/<title>` |
| 其他品类专有玩法 | 对应 `genre_kits/<name>_kit` 或游戏工程 |
| NavMesh 产品化 | 跨品类中间件或游戏自建（见 [LAYERS](../../docs/LAYERS.md)） |
| 状态同步 / 反作弊 | 中间件或游戏；引擎只给传输 |
| 材质节点图 | **范围外** / 外部 DCC（editor **不做**节点图） |
| 关卡视口编辑器 | 独立 [`editor/`](../../editor/docs/README.md) 或继续 DCC+CLI |
| 音频 DSP | 跨品类中间件或游戏 |

## 4. 相关

- [PLAN.md](PLAN.md)  
- [GAPS.md](GAPS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../genre_kits/README.md](../../genre_kits/README.md)  
