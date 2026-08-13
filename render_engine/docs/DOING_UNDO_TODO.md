# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | 下一档可选：T-rmlui-real / T-local-cube | |

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| **P1** | **T-ground-vanish** | **Sandbox 地板在缩放/移动/旋转时仍会消失** | 观感 | **暂缓**。基线 `14400c3`。 |
| P2 | T-rmlui-real | 真正接入 RmlUi | M15 | retained-fallback |
| P2 | T-http-tls | 本机装 OpenSSL 后开 `ENGINE_WITH_OPENSSL` | M19 | CMake 已接线；缺 SDK |
| P2 | T-local-cube-real | 点光 cubemap | M11 | Atlas 多灯已可用；Vulkan 本地阴影仍 stub |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `6edd1d3` Jolt 真接入 |
| Jolt 回退 | `-DENGINE_WITH_JOLT=OFF` |
| Vulkan CSM 回退 | 还原 `vulkan_device.cpp` / `lit_cube_vk.hlsl` / Sandbox 接线 |

验证：`ctest -C Debug -R unit`；Sandbox `--headless --headless_frames 3`；Vulkan：`--backend=vulkan --frames=3`

---

## Done（本轮加深）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | **T-jolt-real**：Jolt v5.6.0 | unit；Sandbox |
| 2026-08-13 | **T-vk-csm**：Vulkan 2048 CSM atlas + comparison 采样 | `--backend=vulkan --frames=3` |
| 2026-08-13 | Sandbox `--backend=vulkan` / `--frames=N`；SPIR-V t/s shift | 冒烟绿 |
