# ADR 0028: 多品类分层 — 薄 game_kit + genre_kits + games

- 状态: Accepted
- 日期: 2026-08-12
- 关联: docs/LAYERS.md, HOSTING.md, ADR 0027, game_kit POSITIONING

## 背景

工作区在 `render_engine`（渲染中台）之上已规划 `game_kit`（脚本 + 玩法骨架）。后续可能实现 **多种游戏类型**（如 2D 像素 RPG、3D 射击等）。若把对话/背包与 Hitscan/武器等全部塞进单一加厚的 `game_kit`，会导致依赖膨胀与版本耦合；若全部只放在各 `games/<title>`，同品类又会重复造轮子。

## 决策

1. **`game_kit` 保持薄且品类无关**：关卡流、Entity/脚本组件、定时器、事件总线、存档槽、触发器约定、玩家控制器骨架等；**不含**品类专有名词与系统（任务、弹匣、对话树等）。  
2. **品类可复用玩法放 `genre_kits/<name>_kit`**：按需创建（如 `rpg_kit`、`shooter_kit`）；依赖 `game_kit`，不依赖具体游戏，不碰 RHI/backends。  
3. **具体内容放 `games/<title>`**：可选用 0..N 个 genre kit；允许第一个标题先内聚实现，稳定后再抽 kit。  
4. **跨品类中间件**（状态同步、NavMesh、空间音频桥等）与品类 kit **平级另立**，默认仍不进 `render_engine` 核心（延续 ADR 0027）。  
5. 权威说明见工作区 [docs/LAYERS.md](../../../../docs/LAYERS.md)；HOSTING 仓库树与之对齐。

## 后果

- 优点：多类型可并行演进；中台与通用壳不被 RPG/射击细节污染。  
- 代价：目录与文档略多；需抵制「先全塞进 game_kit」的捷径。  
- 不阻塞引擎 M1–M25；genre_kits / games 仅为约定与占位时可零代码。

## 学习提示

1. 「游戏引擎 = game_kit + render_engine」仍成立；品类 kit 是可选加成。  
2. 抽 kit 的信号是「第二个同品类标题」或明确复用，而非空想完备框架。  
