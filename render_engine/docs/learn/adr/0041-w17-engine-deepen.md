# ADR 0041: W17 引擎内加深（可进 engine 的都做）

- 状态: Accepted
- 日期: 2026-08-19
- 关联: ADR 0040、KNOWN_GAPS §3–§4、POSITIONING

## 背景

W16 零尾巴收口后，用户要求继续加深：**凡可进 `engine/` 的都做**。仍遵守外置边界。

## 决策

1. **本波做**：VK GPU 粒子 CS（含 SSBO dispatch）；软影半分辨率 compose 加深；`LoadGltfMeshFile` mesh0 全 prim；Sandbox `draw_parts` 多 slot；meshoptimizer Prefer 在有库时真调、否则改进 AABB；DDGI-lite atlas 可采样加深；VT 近场启发式；Path2D/WorldText 演示可见；ShaderHotReload 可选 dxc。
2. **本波不做（需 SDK / 外置）**：真 FFX/NGX dispatch、MsQuic 全 API、Nanite、真 NVIDIA DDGI、复制、mac、C17、材质节点图、SVG 布尔。
3. **验收**：Feature/路径名诚实；双后端能 SKIP；Sandbox 可感；单元测试清零失败。

## 后果

- 优点：中台与主流差距收窄一档（仍非 UE）。
- 代价：工程量大；缺 SPIR-V/dxc 时路径回退。

## 学习提示

「能进 engine」≠「对标 UE」——加深仍停在渲染中台。

## 收口备注（2026-08-19）

- Prefer：`meshopt_buildMeshlets`（`max_triangles=124`）；单测不再要求 ≡ AABB。
- VK 粒子：`gpu-cs-vk` = ephemeral device + SSBO + push constant + readback；缺 SPIR-V → Unavailable → `cpu-fallback`。
- Sandbox：Path2D debug 勾选；Probe GI 走 `SampleAtlasCpu`。
