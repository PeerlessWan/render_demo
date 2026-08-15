# ADR 0032: M26 / P3 加深簇 — Forward+ 钉死与候选开工

- 状态: Accepted
- 日期: 2026-08-15
- 关联: KNOWN_GAPS §4（C01/C02/C10/C08/C04/C16/C20）、[FORWARD_PLUS.md](../../FORWARD_PLUS.md)、ADR 0023

## 背景

M1–M25（含 Win 双后端 100% 口径）已收口。KNOWN_GAPS §4 中多项「中台加深」此前未排期。本波打开 **P3 / M26** 簇，对高影响项做**可验收的最小加深**，不改 POSITIONING、不扩 mac/移动、不动 `editor/`。

## 决策

1. **开簇**：以本 ADR 立项 M26 加深；实现以引擎 `render_engine/` 为界。  
2. **C01**：产品 lit 路径钉死为 **Forward+**（不透明 lit + 局部光；无 deferred G-buffer）。Pass 名冻结见 [FORWARD_PLUS.md](../../FORWARD_PLUS.md)。  
3. **C02**：FrameCB 上传 **最多 8** 盏局部光；CPU 列表可接受至 **16**（按相机距离优先）；**最多 2** 盏进入 Shadow Atlas（其余无阴影）。完整屏幕分块/集群光剔除后置。  
4. **C10**：落地最小 `AnimationStateMachine`（states / transitions / `Sample` → `SampleClip`）；不做混合树产品化。  
5. **C08**：`Path::MeshShader` 保留枚举；本波 **Feature SKIP**（无 mesh-shader PSO）。Indirect + CPU `CullInstancesToIndirect` 为现行 GPU-driven 路径。  
6. **C04**：PostCB 增加 vignette / film grain（默认 0），经 `EffectTuning` 旋钮。  
7. **C16**：`ShaderHotReload::Poll` 仅 mtime 探测（`.hlsl` / `.cso`）；全量 PSO 热重建可选后置。  
8. **C20**：`tools/content_lint` 校验 `manifest.json` 存在并打印依赖列表；非视口编辑器。

## 备选方案

- 本波改 deferred —— 否决（与现有 OpaqueLit / 单 HDR RT 冲突，成本高）。  
- FrameCB 扩到 16 灯 —— 暂缓（体积与双后端打包风险）；8 已满足「>4 + 远灯无阴影」。  
- Mesh Shader 伪实现 —— 否决（禁止假成功）。

## 后果

- 优点：路径与 Pass 名可对外说清；局部光容量与动画/热更/工具有可测落点。  
- 代价：非真集群光；Mesh Shader / 完整热重载 / 电影级后处理仍浅。

## 学习提示

1. Forward+ ≠ deferred：先 lit，再屏幕空间后处理。  
2. 「上传 8、阴影 ≤2」是容量契约，不是最终集群光。  
3. SKIP 的 Feature 仍应在 Path/API 上留位，避免假能力。
