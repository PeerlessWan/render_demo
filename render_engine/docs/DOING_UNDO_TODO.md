# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W21（Godot 渲染内核对标 + 解冻）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0044](learn/adr/0044-w21-godot-parity-unfreeze.md) |
| **水位** | 渲染内核 vs Godot ≈ 80–85%；解冻 DLSS/FSR2/MsQuic（无 SDK → SKIP/bilinear） |
| **仍外置** | Nanite / 真 DDGI / Lumen / FG / mac / 引擎内复制 |
| **单测** | 202 passed / 0 failed |

## Mega-W20（中台产品级加深）

| 项 | 值 |
|---|---|
| **状态** | **已收口并封板** |
| **范围** | [ADR 0043](learn/adr/0043-w20-product-deepen.md) |
| **水位** | 产品 Pass + 真 GPU + 可维护/可度量；不宣称 Lumen/RTXGI/Nanite |
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
当前波：W21 已收口（ADR 0044）— Godot 对标 + 解冻超分/MsQuic
外置：Nanite / 真 DDGI / XR / 节点图 / 蓝图 / mac / C17 / 引擎内复制
解冻：DLSS / FSR2 / MsQuic（可选 SDK；无则 SKIP / bilinear）
禁止：复制进 engine/net；不改 game_kit/（除非他会话）
```

## Doing

| ID | 项 |
|---|---|
| — | （空） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | genre_kits / games | 新品类与内容（game_kit/editor 已接线） |
| 钉死 | C17 / Nanite / 真 DDGI | 外置 |
| 后置 | Q4 WARP / 严 C4 | 测试轨，非封板阻塞 |

## Undo

| 标签 | 值 |
|---|---|
| W21 | ADR 0044 |
| W20 | ADR 0043 |
| W18 | ADR 0042 |
| W17 | ADR 0041 |
| W16 | ADR 0040 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W21** | 解冻 DLSS/FSR2/MsQuic；CascadeGi；Light2D；PbrMaterial 标准字段；粒子碰撞/子发射；Weather↔VolumetricFog；ENGINE_VS≈80–85%；202 unit |
| **W20** | GI atlas→GPU+lit；软影 mask→FrameCB；VT/HLOD/MS/WorldText/热更；device 按域拆分 + OneShot；Profiler/Budget HUD；VULKAN_PARITY |
| **W20 尾巴** | VK upload/oneshot/`UploadLitGeometry` fence（`WaitGpuSubmitted`）+ 文档对齐 |
| **对标记录** | [ENGINE_VS_MAINSTREAM.md](ENGINE_VS_MAINSTREAM.md) |
| **W18** | light tile GPU；WorldText/Path2D；VT packed；软影/MS/HLOD/dxc |
| **W17** | meshoptimizer Prefer；VK 粒子；软影 blur；glTF 多 prim |
| **W16** | 零尾巴收口 |
