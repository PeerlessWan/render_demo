# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| T-ground-vanish | 悬浮浅色层切物体 | 近平面裁大三角假平面 + HDR | 浅色层消失，砖纹与网格同高 |

---

## Todo（下一档）

| 优先级 | ID | 项 | 备注 |
|---|---|---|---|
| P2 | T-taa-mv | TAA + 运动向量重投影 | 修好前默认关 |
| P2 | T-rmlui-real | 真 RmlUi | 需 vendor |
| P2 | T-m13-dynref | 动态反射探针 | |
| P2 | T-m14-bindless-hdr | Bindless/HDR/多线程录制 | Feature 已门控 |
| P2 | T-http-tls | OpenSSL HTTPS | 缺 SDK |
| P3 | T-vk-parity | Vulkan 追平 | 暂缓 |

---

## Undo

| 标签 | 值 |
|---|---|
| 安全基线 | `8d27fab` |
| 本档回退 | 还原 ground slot4 / Cull / TAA jitter 相关 |

验证：Sandbox 拖动视角；默认 TAA 关闭

---

## Done（近期）

| 项 | 说明 |
|---|---|
| T-ground-z | Perspective → D3D Z[0,1]（`8d27fab`） |
| T-wheel-crash | D3D12 帧中 VB / WantCapture（`a638fdc`） |
