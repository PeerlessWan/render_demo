# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## 主线水位（截至 W-auto-1 本地收波）

| 层 | 状态 |
|---|---|
| **M1–M25 无 vendor** | 已收口（`c9977b4`） |
| **ABC 并行波** | **已收口**（`c8b7527`） |
| **Sandbox 体验补丁** | Perf 1Hz / 默认关 TAA / IME（`eb05500`） |
| **W-auto-1** | **本地绿**（未 commit）：C3 矩阵比图；C5 语义；Cull compact；VK 局部影采样；RmlUi 最小文档；HTTPS 用例已加（本机 `ENGINE_WITH_OPENSSL=OFF` → 软 SKIP）；Bindless 热路径试开后回退 `pad=-1` |

```text
安全基线（远程）：eb05500
本波：实现 + ci_headless -Golden 绿；等用户下令再 commit/push
```

---

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空） | W-auto-1 已本地收波 | 见下方 Done |

---

## Todo（波后 / 明确后置）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P1 | T-https-openssl-on | 打开 `ENGINE_WITH_OPENSSL` 跑通自签 loopback 正向绿 | 本机 CMake 现为 OFF |
| P1 | T-bindless-stable | Bindless 堆槽采样默认热路径 | 本波试开导致 sandbox 黄金图漂，已回退 |
| P2 | T-c4-backend-parity | D3D12 vs VK 比图 | Auto-wave-2 |
| P3 | T-q4-warp | WARP 基线 | 需文档口径 |
| P3 | T-m14-bindless-full | 全 Bindless | 后置 |
| P3 | T-linux-m18 | Linux | 外置 |
| P3 | T-ground-slab | 悬浮层 | 搁置 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `eb05500` |
| 本波回退点 | 未推送；丢弃工作树即可回基线 |

验证：`ci_headless.ps1 -Golden`；unit 82；窗口 90 帧；Release 冒烟

---

## Done（近期）

| 项 | 说明 |
|---|---|
| **W-auto-1** | C3 三格比图基线；C5 语义断言；Cull compact UAV；VK local shadow sample；RmlUi LoadDocument；HTTPS loopback 用例；Bindless 回退 |
| W-sandbox-perf | Perf 1Hz；TAA 默认关；IME |
| W-abc-wave | Q3/C2；Cull UAV；VK sample；RmlUi；HTTPS |
| W-m100-nv / Track A–D | 见历史 |
