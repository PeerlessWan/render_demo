# game_kit 定位

## 是

- 挂在 `render_engine` 之上的 **品类无关** 玩法运行时 + 脚本层  
- 提供：关卡流、实体/脚本组件、定时器与事件、玩法存档槽、触发器约定、玩家控制器骨架  
- 可被多个游戏工程与多个 [`genre_kits`](../../genre_kits/README.md) 复用  
- 可称为「轻量游戏引擎」的壳，渲染仍来自中台  

## 不是

- 不是第二份 RHI / FrameGraph  
- **不是品类内容包**（对话/背包/任务/射击循环等 → `genre_kits/*` 或 `games/<title>`）  
- 不替代 `render_engine` 的物理求解、UI Retained 内核、网络传输  
- 不做状态同步独立服/匹配服（本层 `ReplicationSession` 为进程内占位）  
- 不内嵌完整可视化关卡编辑器（见 `editor/`）  

## 与 genre_kits / games

权威分层见 [../../docs/LAYERS.md](../../docs/LAYERS.md)、ADR 0028。

```text
games → genre_kit? → game_kit → render_engine
```

- `game_kit` **不得**依赖某个 genre kit 或某个游戏。  
- 第一个标题可将品类逻辑先写在 `games/<title>`，稳定后再抽 kit。  

## 一句话

> **game_kit = 脚本 + 品类无关玩法骨架；品类玩法在 genre_kits；内容在 games。**

## 相关

- [../README.md](../README.md)  
- [CONSTRAINTS.md](CONSTRAINTS.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [../../render_engine/docs/POSITIONING.md](../../render_engine/docs/POSITIONING.md)  
- [../../render_engine/docs/learn/adr/0028-genre-kits-layering.md](../../render_engine/docs/learn/adr/0028-genre-kits-layering.md)  
