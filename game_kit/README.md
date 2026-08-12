# game_kit

**品类无关** 玩法壳 + 脚本运行时（轻量「游戏引擎」壳）。依赖 [`render_engine`](../render_engine/) 公开 API，**不**实现 RHI/渲染后端。

品类可复用玩法请放 [`genre_kits`](../genre_kits/)；具体内容放 [`games`](../games/)。分层见 [../docs/LAYERS.md](../docs/LAYERS.md)。

| 文档 | 说明 |
|---|---|
| [docs/README.md](docs/README.md) | 文档索引 |
| [docs/POSITIONING.md](docs/POSITIONING.md) | 定位与边界 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构 |
| [docs/FEATURES.md](docs/FEATURES.md) | 功能清单 |
| [docs/SCRIPTING.md](docs/SCRIPTING.md) | 脚本系统专述 |
| [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md) | 约束 |
| [docs/PLAN.md](docs/PLAN.md) | 规划与里程碑 |
| [docs/GAPS.md](docs/GAPS.md) | 缺口 |

引擎侧接入契约：[../render_engine/docs/HOSTING.md](../render_engine/docs/HOSTING.md)。

**现状：** 仅文档；代码未开始。建议在 `render_engine` M4–M9 公开 API 可用后再实现。
