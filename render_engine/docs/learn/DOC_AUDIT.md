# 文档缺口与冲突审计（Mega-W9）

> 优先级：**ADR 0036 > 旧 ADR 修订注记 > PLAN / KNOWN_GAPS / DOING > Sample README > 过时章节**（延续 ADR 0022）。

## 冲突表

| ID | 旧口径 | 新口径 | 改动 | 状态 |
|---|---|---|---|---|
| D1 | PLAN 封板 tip `99542b6` | W8 tip `0942719`；W9 已收口 | PLAN.md、DOING | **resolved** |
| D2 | ADR 0034「C12 VK SKIP」 | W8+ `gpu_skin_vk`；W9 主路径挂接 | 0034 注记 | **resolved** |
| D3 | ADR 0035「MS PSO stub」 | W9 D3D12 真 MS；VK 探测/示范 | 0035；0036 | **resolved** |
| D4 | ADR 0030/0032 VK RT / mesh_shader SKIP | W9 示范或诚实 SKIP | → 0036 | **resolved** |
| D5 | KNOWN_GAPS §3「无 VT」 | 最小 VT；无 Nanite / 非默认全材质 | KNOWN_GAPS、POSITIONING | **resolved** |
| D6 | DOING「当前波 Mega-W8」 | Mega-W9 收口 | DOING | **resolved** |
| D7 | PATH CH35 含糊 | 钉 Sample/开关 + CH36 | PATH.md | **resolved** |
| D8 | chapters 仅占位 | CH00–CH11 正文 + CH12–CH36 短章 | chapters/ | **resolved** |
| D9 | G13「SVG 级」 | 路径网格+简单填充；布尔外置 | KNOWN_GAPS | **resolved** |
| D10 | HOST_API / PREFAB 草案 | 本波不冻结 | 顶注 | **resolved** |

## 缺口补文档

| 主题 | 落点 | 状态 |
|---|---|---|
| FORWARD_PLUS tile CS / range bin | FORWARD_PLUS.md | **resolved** |
| meshlet / MS | ADR 0036、meshlet_ms.hlsl | **resolved** |
| VT / HLOD | ADR 0036、KNOWN_GAPS | **resolved** |
| Linux X11 | LINUX.md、ADR 0036 | **resolved** |
| C17 钉死 | C17_MULTI_WINDOW.md | **resolved** |

## 学习轨验收

- PATH 所列 Sample 均有目录 + 2A README — **resolved**
- CH00–CH11 章节正文 — **resolved**
- CH36 ↔ `36_w9_deepen` — **resolved**

## 相关

- [ADR 0036](adr/0036-mega-w9-deepen.md)
- [KNOWN_GAPS.md](../KNOWN_GAPS.md)
- [DOING_UNDO_TODO.md](../DOING_UNDO_TODO.md)
