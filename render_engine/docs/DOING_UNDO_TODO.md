# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | 滚轮 crash + 地板消失已收口 | |

---

## Todo（下一档）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-rmlui-real | 真 RmlUi | 需 vendor |
| P2 | T-m13-dynref | 动态反射探针 | |
| P2 | T-m14-bindless-hdr | Bindless/HDR/多线程录制 | Feature 已门控 |
| P2 | T-http-tls | OpenSSL HTTPS | 缺 SDK |
| P3 | T-vk-parity | Vulkan 追平 | 暂缓 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `a638fdc` |
| 本档回退 | 还原 Perspective/Frustum 与 Vulkan clip_fix |

验证：`build\tests\Debug\engine_unit_tests.exe`；Sandbox 绕一圈/缩放看地板

---

## Done（近期）

| 项 | 说明 |
|---|---|
| T-ground-vanish | Perspective → D3D/Vulkan Z[0,1]；视锥近平面；去掉 Vulkan 双重 remap |
| T-wheel-crash | D3D12 帧中 VB 扩容 / WaitGpu fence 毒化；UI 滚轮 WantCapture |
| T-readback-real | D3D12 真 GPU Readback |
| T-pick-sandbox | 点击 Pick + AABB 高亮 |
| T-phys-debug | 物理碰撞盒 DebugDraw + half_extents API |
| T-probe-ambient | ProbeVolume → ambient |
| T-sprites-particles | Sprite tint + CPU 粒子 |
| T-morph-demo | Morph 滑条驱动 mesh slot3 |
| T-terrain-mesh | Heightmap 网格 + 植被 LOD |
| T-m13-m15 | 此前 post / retained HUD 已合入 |
