# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | D3D12 下一档：T-m13-post / T-rmlui / T-ground-vanish | |

> **Vulkan 暂缓**：CSM lit 可用；纹理/UI/Post 不对齐。

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| **P1** | **T-ground-vanish** | **Sandbox 地板旋转/缩放时消失** | 观感 | **暂缓** |
| P2 | T-rmlui-real | 真正接入 RmlUi | M15 | retained-fallback |
| P2 | T-http-tls | OpenSSL HTTPS | M19 | 缺本机 SDK |
| P2 | T-m13-post | SSR / DoF / 运动模糊 / 自动曝光 / 体积雾 / 动态反射 | M13 | 现为开关骨架 |
| P2 | T-m14-submit | Morph、Bindless、多线程录制、HDR 输出 | M14 | 骨架 |
| P3 | T-vk-parity | Vulkan 追平 D3D12 Sandbox | M17 | **暂缓** |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `0edcc07` |
| 本档回退 | 还原 local cube 相关：`local_lights` / `render_system` / `lit_cube.hlsl` / `d3d12_device` FrameCB |

验证：`ctest -C Debug -R unit`；Sandbox `--headless --headless_frames 3`

---

## Done（本轮加深）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | T-jolt-real | unit；Sandbox |
| 2026-08-13 | T-vk-csm（随后 **Vulkan 暂缓**） | `--backend=vulkan --frames=3` |
| 2026-08-13 | **T-local-cube-real**：点光 6-face cubemap atlas（D3D12，最多 2 灯） | unit；Sandbox headless |
