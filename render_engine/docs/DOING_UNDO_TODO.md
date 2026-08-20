# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W17（引擎内加深）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0041](learn/adr/0041-w17-engine-deepen.md)；可进 engine 的加深 |
| **外置不变** | Nanite / 真 DDGI / 复制 / mac / C17 / FFX·NGX·MsQuic 真 API |

## Mega-W16（零尾巴收口）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **口径** | [ADR 0040](learn/adr/0040-w16-zero-tail-closeout.md) |

## Mega-W12–W15

| 项 | 值 |
|---|---|
| **状态** | **已收口**（经 W16） |

## 主线水位

```text
当前波：W17 已收口（ADR 0041）
外置：Nanite / 真 DDGI / XR / 节点图 / 蓝图 / mac / C17 / 引擎内复制 / 商业 SDK 真接线
禁止：复制进 engine/net；不改 game_kit/（除非他会话）
```

## Doing

| ID | 项 |
|---|---|
| — | （空） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | game_kit / editor | 玩法壳 / 复制 |
| 钉死 | C17 / Nanite / 真 DDGI | 外置 |
| 后续 | FFX/NGX / MsQuic API | 有 SDK 再接 |

## Undo

| 标签 | 值 |
|---|---|
| W17 | ADR 0041 |
| W16 | ADR 0040 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W17** | meshoptimizer Prefer 真接线；半分辨率软影 blur；glTF mesh0 全 prim；Sandbox 多 slot；VT 近场启发式；HotReload+dxc；kMaxMeshSlots=16；**VK 粒子 SSBO dispatch**；Path2D/WorldText 调试可见；Probe atlas 采样路径 |
| **W16** | 零尾巴收口 |
