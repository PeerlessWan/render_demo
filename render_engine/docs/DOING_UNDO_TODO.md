# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补。

## Mega-W23（缺口 + Nanite-like + 真 DDGI）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0046](learn/adr/0046-w23-nanite-ddgi-gaps.md) |
| **水位** | VirtualGeometry；RTXGI 可选；Post/RT/Mat/Phys 缺口；NGX+RTXGI 一并安装 |
| **仍外置** | Lumen / FG / XeSS / mac / C17 / 引擎内复制 |
| **单测** | 215 passed / 0 failed |

## Mega-W22（Godot 渲染内核 ≈100%）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0045](learn/adr/0045-w22-godot-kernel-100.md) |
| **水位** | 渲染内核 vs Godot ≈ 100%；DLSS+FSR 设备绑定；XeSS 不做 |
| **单测** | 208 passed / 0 failed |

## Mega-W21（Godot 渲染内核对标 + 解冻）

| 项 | 值 |
|---|---|
| **状态** | **已收口** |
| **范围** | [ADR 0044](learn/adr/0044-w21-godot-parity-unfreeze.md) |
| **水位** | 渲染内核 vs Godot ≈ 80–85% |
| **单测** | 202 passed / 0 failed |

## Mega-W20

| 项 | 值 |
|---|---|
| **状态** | **已收口并封板** |
| **范围** | [ADR 0043](learn/adr/0043-w20-product-deepen.md) |

## 主线水位

```text
当前波：W23 已收口（ADR 0046）— 缺口 + Nanite-like + 真 DDGI
外置：Lumen / FG / XR / 蓝图 / mac / C17 / XeSS / 引擎内复制
SDK：fetch_nvidia_ngx_rtxgi.ps1 → third_party/ngx + rtxgi（本机 drop-in）
禁止：复制进 engine/net；不改 game_kit/（除非他会话）
```

## Doing

| ID | 项 |
|---|---|
| — | （空） |

## Todo

| 优先级 | ID | 项 |
|---|---|---|
| 他会话 | genre_kits / games | 新品类与内容 |
| 钉死 | C17 / Lumen / FG / XeSS | 外置 |
| 后置 | Q4 WARP / 严 C4 | 测试轨 |

## Undo

| 标签 | 值 |
|---|---|
| W23 | ADR 0046 |
| W22 | ADR 0045 |
| W21 | ADR 0044 |
| W20 | ADR 0043 |
| W18 | ADR 0042 |
| W17 | ADR 0041 |
| W16 | ADR 0040 |

## Done（近期）

| 项 | 说明 |
|---|---|
| **W23** | VirtualGeometry；RTXGI Bind；NGX+RTXGI 安装；GTAO/FXAA/LUT/fog；RT 产品软影；detail/triplanar GPU；粒子 mesh；Joints/Vehicle/Shatter；215 unit |
| **W22** | CascadeGi 加深；2D 遮挡阴影；材质 detail；粒子吸引子/trail；雾盒；DLSS+FSR Bind；Low 档；ENGINE_VS≈100%；208 unit |
| **W21** | 解冻超分/MsQuic；CascadeGi/Light2D 初版；ENGINE_VS≈80–85% |
| **W20** | GI atlas→GPU；device 拆分；产品 Pass |
