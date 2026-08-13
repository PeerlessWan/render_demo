# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | M1–M25 本轮加深已收口 | |

> **Vulkan / T-ground-vanish / RmlUi vendor / OpenSSL 仍暂缓**。

---

## Todo（下一档）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P1 | T-ground-vanish | 地板消失 | 暂缓 |
| P2 | T-rmlui-real | 真 RmlUi | 需 vendor |
| P2 | T-m13-dynref | 动态反射探针 | |
| P2 | T-m14-bindless-hdr | Bindless/HDR/多线程录制 | Feature 已门控 |
| P2 | T-http-tls | OpenSSL HTTPS | 缺 SDK |
| P3 | T-vk-parity | Vulkan 追平 | 暂缓 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `989cc06` |
| 本档回退 | 还原 Sandbox/VFX/Terrain/GI/Readback/Physics AABB API |

验证：`build\tests\Debug\engine_unit_tests.exe`；Sandbox `--headless --headless_frames 3`

---

## Done（本轮 M1–M25 加深）

| 项 | 说明 |
|---|---|
| T-readback-real | D3D12 真 GPU Readback |
| T-pick-sandbox | 点击 Pick + AABB 高亮 |
| T-phys-debug | 物理碰撞盒 DebugDraw + half_extents API |
| T-probe-ambient | ProbeVolume → ambient |
| T-sprites-particles | Sprite tint + CPU 粒子 |
| T-morph-demo | Morph 滑条驱动 mesh slot3 |
| T-terrain-mesh | Heightmap 网格 + 植被 LOD |
| T-m13-m15 | 此前 post / retained HUD 已合入 |
