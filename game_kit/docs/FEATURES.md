# game_kit 功能清单

> 状态列：`规划` = 文档已定目标；实现随 [PLAN.md](PLAN.md)。

## 1. 玩法运行时

| ID | 功能 | 说明 | 状态 |
|---|---|---|---|
| GKRT01 | 关卡流 | Load/Unload、切换、加载中占位；对接引擎异步加载 | 规划 |
| GKRT02 | 暂停 / 时间缩放 | 逻辑时间与渲染帧解耦（渲染仍可跑） | 规划 |
| GKRT03 | Entity / ScriptComponent | 挂在引擎 Node 或平行 ID；脚本组件生命周期 | 规划 |
| GKRT04 | 定时器 | delay / interval；主线程触发 | 规划 |
| GKRT05 | 事件总线 | 玩法内 pub/sub；与引擎 Event 可桥接 | 规划 |
| GKRT06 | 玩法存档 | 读写槽位；不含完整 RPG 内容格式强制 | 规划 |
| GKRT07 | 触发器约定 | 物理 Trigger 进入/离开 → 脚本回调 | 规划 |
| GKRT08 | 玩家控制器骨架 | 读 Action → 移动/相机；具体手感由游戏调 | 规划 |
| GKRT09 | Prefab 实例化 | 解析共享 Prefab：生成 Node + 挂脚本 | 规划 |

## 2. 脚本系统

详见 [SCRIPTING.md](SCRIPTING.md)。摘要：

| ID | 功能 | 状态 |
|---|---|---|
| GKSC01 | 语言运行时（默认 Lua，可换） | 规划 |
| GKSC02 | 绑定白名单（Scene/Input/UI/Physics/Audio/Asset） | 规划 |
| GKSC03 | 脚本热重载（开发构建） | 规划 |
| GKSC04 | 错误隔离（脚本异常不崩 Device） | 规划 |
| GKSC05 | 调试钩子（日志、断点对接可选） | 规划 |
| GKSC06 | 协程 / yield 到下一帧（可选） | 规划 |

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
