# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | 下一档可选：T-vk-csm / T-rmlui-real / T-local-cube | |

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| **P1** | **T-ground-vanish** | **Sandbox 地板在缩放/移动/旋转时仍会消失** | 观感 | **暂缓**。现象：只有特定视角可见或操作时底部消失。已试：视锥 slack/`never_cull`、D3D NDC Z[0,1]、Cull NONE、32×32 细分平面——后两档曾回退（变严重）。后续需系统查：投影/矩阵约定、DepthClip、背面、近平面大三角、SSAO/阴影误伤。基线 `14400c3`。 |
| P2 | T-rmlui-real | 真正接入 RmlUi | M15 | retained-fallback |
| P2 | T-http-tls | 本机装 OpenSSL 后开 `ENGINE_WITH_OPENSSL` | M19 | CMake 已接线；缺 SDK |
| P2 | T-vk-csm | Vulkan CSM/阴影对齐 | M17 | lit cube 已可用 |
| P2 | T-local-cube-real | 点光 cubemap | M11 | Atlas 多灯已可用 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `14400c3` Sandbox polish（地板 bug 仍在） |
| 本档回退 | `-DENGINE_WITH_JOLT=OFF` 或移除 `third_party/JoltPhysics` → builtin |

验证：`ctest -C Debug -R unit`；Sandbox `--headless --headless_frames 3`

---

## Done（本轮加深）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | 网格 / RGB 坐标轴 + F3/F4 开关 | Sandbox |
| 2026-08-13 | Debug line GPU 路径（`debug_line.hlsl`） | D3D12 |
| 2026-08-13 | 拖动视角 / 滚轮缩放 / MMB 平移 / 曝光 | Sandbox |
| 2026-08-13 | DamagedHelmet + Poly Haven + 双材质槽 | Sandbox |
| 2026-08-13 | 地板：仅部分缓解（never_cull）；**完整修复记入 T-ground-vanish** | 未收口 |
| 2026-08-13 | **T-jolt-real**：Jolt v5.6.0 真接入（`jolt_world`） | unit 绿；Sandbox headless |
