# editor



**独立内容编辑器工程**（视口 + 工具 UI）。依赖 [`render_engine`](../render_engine/) 作渲染视口；可选消费 [`game_kit`](../game_kit/) 的 Prefab/脚本元数据。



工作区分层：[../docs/LAYERS.md](../docs/LAYERS.md)。



| 文档 | 说明 |

|---|---|

| [docs/README.md](docs/README.md) | 文档索引 |

| [docs/POSITIONING.md](docs/POSITIONING.md) | 定位与边界 |

| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构 |

| [docs/FEATURES.md](docs/FEATURES.md) | 功能清单 |

| [docs/CONSTRAINTS.md](docs/CONSTRAINTS.md) | 约束 |

| [docs/PLAN.md](docs/PLAN.md) | 规划与里程碑 |

| [docs/GAPS.md](docs/GAPS.md) | 缺口 |



引擎侧：[../render_engine/docs/HOSTING.md](../render_engine/docs/HOSTING.md)（C21）、[TOOLING.md](../render_engine/docs/TOOLING.md)。  

品类 / 游戏：[../genre_kits/README.md](../genre_kits/README.md) · [../games/README.md](../games/README.md)。



**现状：** 摆放器 + 编辑器可用已收口（ED0–ED5）。场景可点选/Gizmo（含局部轴）/存盘/Play（`player` WASD + 脚本 Entity + Step）；体素 Play 为生存出生 + HUD。Agent 走 `editor_mcp`（见 [docs/AI.md](docs/AI.md)）。Smoke：`editor_smoke_tests`；headless：`editor_app --headless` / `--voxel`。



运行：`editor_app`（工作目录建议 `render_demo`，以便扫到 `editor/content` 与 `editor/scripts`）。

