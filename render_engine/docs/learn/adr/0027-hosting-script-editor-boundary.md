# ADR 0027: 宿主分层 — 脚本与编辑器在引擎外（或可选插件）

- 状态: Accepted
- 日期: 2026-08-12
- 关联: docs/HOSTING.md, POSITIONING, KNOWN_GAPS C19–C21, ADR 0019/0025

## 背景

产品需要「将来能做游戏、能编内容」，但本仓库定位为渲染中台。若把脚本 VM 与完整编辑器做进 `engine/`，范围与维护成本会接近全能游戏引擎，与已锁死边界冲突。

## 决策

1. **脚本**：默认由 **外层玩法工程** 自带 VM；引擎不实现语言运行时。可选预留 `IScriptHost`（C19），默认空。  
2. **编辑器**：默认 **外部 DCC + tools CLI**（ADR 0025）。轻量校验/浏览工具为 C20；完整视口编辑器为 **独立 `editor/` 工程**（C21），不阻塞 M1–M25。  
3. 上层只依赖公开 API 面；禁止 backends/三方头；帧相位与生命周期见 [HOSTING.md](../../HOSTING.md)。  
4. 不因此修改「玩法/完整编辑器不进引擎核心」的 POSITIONING 结论；若改定位须新 ADR 废止本决策相关条款。

细则与目录文档：

- [HOSTING.md](../../HOSTING.md)  
- [game_kit/docs](../../../../game_kit/docs/README.md)  
- 视口编辑器：独立 `editor/` 工程；**规格不进本引擎文档树**（见 [LAYERS.md](../../../../docs/LAYERS.md)）  
- 多品类分层：[LAYERS.md](../../../../docs/LAYERS.md)、[ADR 0028](0028-genre-kits-layering.md)  

## 后果

- 优点：中台边界清晰；游戏与编辑器可独立演进。  
- 代价：开箱不能「写 Lua 就出 RPG」；需另建 game_kit/editor；品类系统见 genre_kits（ADR 0028）。

## 学习提示

1. 渲染 Sandbox ≠ 可玩 Demo。  
2. 脚本绑定白名单比「暴露一切 C++」更重要。  
3. 编辑器是宿主，不是又一个 backends。  
