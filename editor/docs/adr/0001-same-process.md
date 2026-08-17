# ADR 0001: 编辑器一期同进程

- 状态: Accepted
- 日期: 2026-08-14
- 关联: [ARCHITECTURE.md](../ARCHITECTURE.md) §3、[CONSTRAINTS.md](../CONSTRAINTS.md)

## 背景

视口编辑器需要共享 `render_engine` Device 与场景权威树。分进程视口更稳，但 IPC 与双份资产超出一期摆放器范围。

## 决策

1. 一期采用 **模式 A：同进程**。编辑器进程创建 `engine::Application`，工具 UI（Dear ImGui / `ImmediateUi`）与视口同一 Device。
2. Play-in-Editor（ED4）：进入 Play 前快照 `World`（或体素 `GameState`）；退出 **恢复快照**，丢弃运行时脏数据。
3. 发版若脚本/玩法崩溃影响编辑器，再评估模式 B（分进程视口）。

## 后果

- 优点：实现短、拣选与 Gizmo 直接读 `World` / `RenderScene`。
- 代价：脚本异常必须隔离（由 `game_kit` pcall 承担）；Device 丢失会带走整个编辑器。
