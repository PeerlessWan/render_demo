# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | 本轮加深已验收 | |

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| P2 | T-jolt-real | 真正接入 Jolt 库 | M12 | stub→builtin |
| P2 | T-rmlui-real | 真正接入 RmlUi | M15 | retained-fallback |
| P2 | T-http-tls | 本机装 OpenSSL 后开 `ENGINE_WITH_OPENSSL` | M19 | CMake 已接线；缺 SDK |
| P2 | T-vk-csm | Vulkan CSM/阴影对齐 | M17 | lit cube 已可用 |
| P2 | T-local-cube-real | 点光 cubemap | M11 | Atlas 多灯已可用 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `cbbba69` Productize lit mid-platform… |

验证：`ctest -C Debug -R unit`；Sandbox `--headless --headless_frames 3`；`sample_01_clear --backend=vulkan --mesh`

---

## Done（本轮加深）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | **GPU Pass 时间戳** → F2 Profiler | D3D12 query + Sandbox |
| 2026-08-13 | **透明 AlphaBlend**（glass + 后到前） | unit glass + Sandbox |
| 2026-08-13 | **Vulkan lit cube**（SPIR-V / `--mesh`） | sample_01_clear |
| 2026-08-13 | OpenSSL CMake 可选接线（无 SDK 则跳过） | configure |
| 此前 | CSM / 点光 Atlas / SSAO·TAA / ImGui / PNG·DDS / HTTP / … | `cbbba69` |
