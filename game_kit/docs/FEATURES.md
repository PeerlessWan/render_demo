# game_kit 功能清单

> 状态列：`规划` = 文档已定目标；`已写` = 代码落地；`部分` = 骨架或可选注入。

## 1. 玩法运行时

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| GKRT01 | 关卡流 | Replace/Additive、`UnloadStacked`、`delay` 异步占位、`loading_progress`；`ClearPlayState` 在 Replace 切关时清理 | 已写 |
| GKRT02 | 暂停 / 时间缩放 | `paused` + `time_scale`；`logic_dt` 驱动逻辑 | 已写 |
| GKRT03 | Entity / ScriptComponent | tag 查询、Acquire/Release 池；`on_init` / `on_update` / `on_destroy` | 已写 |
| GKRT04 | 定时器 | delay / interval；主线程触发；Lua `delay`/`interval` | 已写 |
| GKRT05 | 事件总线 | 玩法内 pub/sub；Lua `publish`/`subscribe`/`unsubscribe`；`wait_event` 经 `on_publish` 唤醒 | 已写 |
| GKRT06 | 玩法存档 | 槽位 JSON v1；`WorldSnapshot` 位姿/旋转/缩放/tag/脚本 persist | 已写 |
| GKRT07 | 触发器约定 | AABB 体积；enter/leave → EventBus + `on_trigger_*` | 已写 |
| GKRT08 | 玩家控制器 | ActionMap + 跳跃 + 鼠标视角 + 相机相对移动 + E 冲刺；Follow / 弹簧臂 / 第一人称 | 已写 |
| GKRT09 | Prefab 实例化 | 生成 Node；可选挂 ScriptComponent | 已写 |
| GKRT10 | 物理接触 | `ConsumeContacts` / `OverlapAabb`；角色 `MoveCharacter` + 地面 Raycast | 已写 |
| GKRT11 | 动画播放 | `AnimPlayer` 接状态机；Notify；根运动默认写回实体 | 已写 |
| GKRT12 | 音频混合 | Mixer master/sfx/music 总线 + 距离衰减 | 已写 |
| GKRT13 | 导航 | Recast 烘焙 + FindPath；Steer 回退。无离线编辑器 | 已写 |
| GKRT14 | Timeline | cue → EventBus；Play/Pause/Seek | 已写 |
| GKRT15 | 联机占位 | `LoopbackReplicator` tick/Diff + `ReplicationSession` 进程内插值 | 已写 |

## 2. 脚本系统

详见 [SCRIPTING.md](SCRIPTING.md)。摘要：

| ID | 功能 | 状态 |
|---|---|---|
| GKSC01 | 语言运行时（默认 Lua，可换） | 已写 |
| GKSC02 | 绑定白名单（`api.json` → `luaL_Reg`） | 已写 |
| GKSC03 | 脚本热重载（开发构建） | 已写（可 `persist`） |
| GKSC04 | 错误隔离（脚本异常不崩 Device） | 已写（`luaL_traceback`） |
| GKSC05 | 调试钩子 | 已写（行断点 + `on_break` locals；无 IDE） |
| GKSC06 | 协程 / yield | 已写（`wait` / `wait_event` / `start_coroutine`） |
| GKSC07 | `import` 模块缓存 | 已写 |

## 3. 明确不做（本层）

| 项 | 去向 |
|---|---|
| RHI / 后处理 / GI | render_engine |
| 对话 / 背包 / 任务 / 战斗等 RPG 系统 | [`genre_kits/rpg_kit`](../../genre_kits/README.md) 或 `games/<title>` |
| 武器 / Hitscan / 射击循环 | [`genre_kits/shooter_kit`](../../genre_kits/README.md) 或 `games/<title>` |
| 其他品类专有玩法 | 对应 `genre_kits/<name>_kit` 或游戏工程 |
| NavMesh 离线编辑器 | 外置工具；运行时烘焙在本层 Recast |
| 状态同步独立服 / 反作弊 | 中间件或游戏；本层仅进程内 `ReplicationSession` |
| 材质节点图 | **范围外** / 外部 DCC（editor **不做**节点图） |
| 关卡视口编辑器 | 独立 [`editor/`](../../editor/docs/README.md) 或继续 DCC+CLI |
| 音频 DSP | 跨品类中间件或游戏 |
| 绑定生成器全家桶 / IDE 调试器 / Luau | 后置工具；本层仅有 `api.json` 生成器与行断点 |

## 4. 相关

- [PLAN.md](PLAN.md)  
- [GAPS.md](GAPS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../genre_kits/README.md](../../genre_kits/README.md)  
