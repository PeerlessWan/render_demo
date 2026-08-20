# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W20（中台产品级加深）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0043](learn/adr/0043-w20-product-deepen.md) |
| **水位** | 产品 Pass + 真 GPU + 可维护/可度量；不宣称 Lumen/RTXGI/Nanite |
| **冻结** | DLSS / FSR2 / MsQuic 真 SDK |
| **单测** | 194 passed / 0 failed |

## Mega-W18（半落地加深）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0042](learn/adr/0042-w18-partial-deepen.md) |

## Mega-W17 / W16

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | ADR 0041 / 0040 |

## 主线水位

```text
当前波：W20 已收口（ADR 0043）
外置：Nanite / 真 DDGI / XR / 节点图 / 蓝图 / mac / C17 / 引擎内复制
冻结：DLSS / FSR2 / MsQuic 真 SDK
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
| 冻结 | FFX/NGX / MsQuic API | 产品明确重启前不开发 |

## Undo

| 标签 | 值 |
|---|---|
| W20 | ADR 0043 |
| W18 | ADR 0042 |
| W17 | ADR 0041 |
| W16 | ADR 0040 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W20** | GI atlas→GPU+lit；软影 mask→FrameCB；VT/HLOD/MS/WorldText/热更；device 按域拆分 + OneShot；Profiler/Budget HUD；VULKAN_PARITY |
| **W20 尾巴** | VK upload/oneshot/`UploadLitGeometry` fence（`WaitGpuSubmitted`）+ 文档对齐（Sandbox Medium / PLAN W17–W20） |
| **W18** | light tile GPU；WorldText/Path2D；VT packed；软影/MS/HLOD/dxc |
| **W17** | meshoptimizer Prefer；VK 粒子；软影 blur；glTF 多 prim |
| **W16** | 零尾巴收口 |
