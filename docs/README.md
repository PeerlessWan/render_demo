# 工作区文档索引

> 全工作区入口。引擎实现仍从 [render_engine/docs/GETTING_STARTED_M1.md](../render_engine/docs/GETTING_STARTED_M1.md) 开始。

## 先读

| 你想… | 文档 |
|---|---|
| 各层放什么、依赖怎么走 | **[LAYERS.md](LAYERS.md)**（权威） |
| 引擎怎么被外挂 | [HOSTING.md](../render_engine/docs/HOSTING.md) |
| 引擎边界与里程碑 | [POSITIONING](../render_engine/docs/POSITIONING.md) · [PLAN](../render_engine/docs/PLAN.md) |

## 目录 ↔ 文档

| 目录 | 角色 | 文档入口 |
|---|---|---|
| `render_engine/` | 渲染中台 | [../render_engine/docs/README.md](../render_engine/docs/README.md) |
| `game_kit/` | 品类无关玩法壳 + 脚本 | [../game_kit/docs/README.md](../game_kit/docs/README.md) |
| `genre_kits/` | 可选品类层 | [../genre_kits/README.md](../genre_kits/README.md) |
| `games/` | 具体游戏工程 | [../games/README.md](../games/README.md) |
| `editor/` | 独立视口编辑器（**文档只在本层**，不进引擎 docs） | [../editor/docs/README.md](../editor/docs/README.md) |
| `tools/` | 离线工具占位 | [../tools/README.md](../tools/README.md) |

## 关键 ADR

- [0027 宿主：脚本/编辑器在引擎外](../render_engine/docs/learn/adr/0027-hosting-script-editor-boundary.md)  
- [0028 多品类：薄 game_kit + genre_kits](../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
- 全表：[ADR_INDEX](../render_engine/docs/learn/ADR_INDEX.md)  

## 相关

- 工作区根 [../README.md](../README.md)  
