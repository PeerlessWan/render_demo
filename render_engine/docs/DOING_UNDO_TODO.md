# Doing / Undo / Todo 工作板

> 与 [PLAN.md](PLAN.md) 里程碑进度表互补：本文件跟踪**当前迭代**的进行中项、待办队列与回退方式。  
> 权威范围仍以 PLAN / [KNOWN_GAPS.md](KNOWN_GAPS.md) / [POSITIONING.md](POSITIONING.md) 为准；此处只写「这一轮正在干什么」。

## 约定

| 栏 | 含义 | 更新时机 |
|---|---|---|
| **Doing** | 正在做、未合入或未验收的一项（通常 ≤1～2 条） | 开干时写入；验收/合入后移入 Done 或删 |
| **Todo** | 下一档可开工项（按优先级） | 每轮打磨结束重排；大项仍须对应 PLAN 里程碑 |
| **Undo** | 如何安全回退本轮改动 | 开干前记下「安全基线」commit；改完补验证命令 |
| **Done（本轮）** | 本迭代已验收条目 | 合入或自测绿后追加；过旧可归档到 PLAN §6 |

---

## Doing

| ID | 项 | 目标 | 验收 |
|---|---|---|---|
| — | （空档） | 本轮 Todo 已清 | |

---

## Todo（下一档）

| 优先级 | ID | 项 | 对应 | 备注 |
|---|---|---|---|---|
| P2 | T-jolt-real | 真正接入 Jolt 库 | M12 | 现为工厂 stub→builtin |
| P2 | T-rmlui-real | 真正接入 RmlUi | M15 | 现为 retained-fallback |
| P2 | T-http-tls | HTTPS/OpenSSL | M19 | HTTP 明文已可用 |
| P2 | T-vk-lit | Vulkan lit/CSM 对齐 D3D12 | M17 | 现为清屏路径 |
| P2 | T-gpu-profiler | GPU Pass 时间戳 | M8–M9 | CPU Profiler 面板已有 |
| P2 | T-local-cube-real | 点光 cubemap | M11 | 现为透视 Atlas 多灯 |

---

## Undo（回退）

### 1. 安全基线

| 标签 | 值 | 说明 |
|---|---|---|
| 已合入 lit 可用路径 | `b809f63` | *Make lit rendering path usable in Sandbox.* |
| 本轮（未提交） | 工作区相对 `b809f63` | 含 CSM/点光/文件纹理/SSAO/TAA/VK/HTTP/DDS 等 |

### 2. 验证

```
cmake --build build --config Debug --target engine_unit_tests sample_sandbox
ctest -C Debug -R unit
ctest -C Debug -L headless
.\build\samples\Sandbox\Debug\sample_sandbox.exe --headless --headless_frames 3
.\build\samples\learn\01_clear\Debug\sample_01_clear.exe --backend=vulkan
```

---

## Done（本轮已验收）

| 日期 | 项 | 验证 |
|---|---|---|
| 2026-08-13 | 方向光 GPU 阴影图 + PCF；简易 metallic/roughness | unit/headless |
| 2026-08-13 | `RenderSystem` Shadow→Opaque→UI2D；屏空间 quad | Sandbox headless |
| 2026-08-13 | **多级 CSM** | unit CSM + Sandbox |
| 2026-08-13 | **程序化 albedo** → **文件 albedo/ORM（PNG）** | unit image + Sandbox |
| 2026-08-13 | 点光 Diffuse + **多灯 GPU Atlas 阴影** | Sandbox 双灯 |
| 2026-08-13 | Sandbox **特效 UI** + **Profiler(F2)** | Sandbox |
| 2026-08-13 | **深度 SSAO** + **历史 TAA（邻域钳制）** post resolve | unit + Sandbox |
| 2026-08-13 | **DDS**（dds-ktx）+ **HTTP**（cpp-httplib） | unit |
| 2026-08-13 | **Vulkan 清屏 IDevice**（Win32 surface） | sample_01_clear |
| 2026-08-13 | Jolt/RmlUi **工厂骨架**（fallback builtin/retained） | unit + Sandbox log |

---

## 与 PLAN 的关系

- **PLAN §6**：里程碑级状态。  
- **本文件**：迭代级看板。  
- **KNOWN_GAPS**：长期缺口；不在此抄全表。
