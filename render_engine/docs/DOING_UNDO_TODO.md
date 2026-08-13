# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | D3D12 下一档：T-rmlui / bindless 真路径 / T-ground-vanish | |

> **Vulkan 暂缓**：CSM lit 可用；纹理/UI/Post 不对齐。

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| **P1** | **T-ground-vanish** | **Sandbox 地板旋转/缩放时消失** | 观感 | **暂缓** |
| P2 | T-rmlui-real | 真正接入 RmlUi（vendor） | M15 | Sandbox retained HUD 已通；真 Rml 仍待 |
| P2 | T-http-tls | OpenSSL HTTPS | M19 | 缺本机 SDK |
| P2 | T-m13-dynref | 动态反射探针 | M13 | 剩余 |
| P2 | T-m14-bindless-hdr | Bindless 真路径、HDR 输出、多线程录制 | M14 | Feature 已门控 |
| P3 | T-vk-parity | Vulkan 追平 D3D12 Sandbox | M17 | **暂缓** |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `35861cd`（点光 cubemap） |
| 本档回退 | 还原 post / morph / feature 相关改动 |

验证：`build\tests\Debug\engine_unit_tests.exe`；Sandbox `--headless --headless_frames 3`

---

## Done（本轮加深）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | T-jolt-real | unit；Sandbox |
| 2026-08-13 | T-vk-csm（随后 **Vulkan 暂缓**） | `--backend=vulkan --frames=3` |
| 2026-08-13 | **T-local-cube-real**：点光 6-face cubemap atlas（D3D12，最多 2 灯） | unit；Sandbox headless |
| 2026-08-13 | **T-m13-post**：ACES/AutoExposure/Bloom/雾/SSR/DoF/运动模糊（D3D12） | unit；Sandbox headless |
| 2026-08-13 | **T-m14-morph**：CPU Morph + SubmitConfig/Feature 门控 | unit |
| 2026-08-13 | **T-retained-hud**：Sandbox retained-fallback → ScreenQuad 端到端 | unit；Sandbox headless |
