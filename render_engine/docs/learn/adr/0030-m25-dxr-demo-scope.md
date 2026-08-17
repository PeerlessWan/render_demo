# ADR 0030: M25/W4/W7 DXR demo scope（Feature 门控 + 最小真 DispatchRays）

- 状态: Accepted
- 日期: 2026-08-15
- 更新: 2026-08-17（W7：BLAS/TLAS + 可选 DispatchRays）
- 关联: CH19、ADR 0007、PLAN M8/M25/W4/W7、`engine/rt`、`samples/learn/19_dxr_intro`

## 背景

M8/M25 要求「DXR 示范 + 可关降级」。W4 先落地探测与 stub contract；W7 在 D3D12 上补最小真 AS 构建与 `DispatchRays` 尝试（仍非生产级 fullscreen 合成）。

## 决策

1. **M25 验收面**：真实设备能力探测（`ProbeDxrHardwareSupport` / D3D12 `OPTIONS5`）+ `CanRunDxrDemo` + `Resolve`/`EnsureSafe` 降级契约；无能力时 **SKIP exit 0**，禁止硬崩。  
2. **W4 stub contract**（保留）：
   - `DxrShadowDemo`：门控为真时记录 WOULD run；
   - `RunDxrFullscreenStub`：Feature 关闭 → Unavailable；
   - `TryEmptyTlasPrebuild`：empty TLAS prebuild 或清晰 Unavailable。  
3. **W7 加深**：
   - `TryBuildCubeBlasTlasAndDispatchRays`：DXR 硬件上建三角 BLAS + TLAS；若 `dxr_shadow_lib.cso` 可用则建 RTPSO 并 `DispatchRays`（8×8）；StateObject/lib 缺失时 **AS 已建仍返回 Ok** 并 LogInfo。  
   - `DxrShadowDemo` / `RunDxrFullscreenStub` 优先走真实路径，失败再回退 empty-TLAS stub。  
4. **不在本 ADR 范围**：生产级多 bounce、画面合成、Vulkan Ray Tracing（VK RT 继续 Feature=false / SKIP）。  
5. `DxrDemoConfig.max_bounces` 仍为预留字段；`Resolve` 不读取。

## 备选方案

- 立刻上完整 DXR 帧 —— 成本高、与 CH19 教学切片冲突。  
- 仅文档门控、不探测硬件 —— 假阳性/假阴性，CI 无法诊断。

## 后果

- 优点：门控诚实；有硬件时可验证 AS + DispatchRays；无 lib/PSO 时不假装已出光追画面。  
- 代价：仍非 sandbox 合成路径；「会跑 DXR」在无 HW 时仍可能是 stub Ok。

## 学习提示

1. 先问「能不能跑」，再问「怎么画」。  
2. `features.raytracing` 可由设备 init 或 `ProbeDxrHardwareSupport` 写入 override。  
3. Vulkan RT 对齐仍是有意差。  
4. W7：`RunDxrFullscreenStub` Ok 可能表示真实 `DispatchRays`，也可能是 AS-only / empty-TLAS 回退——看日志。
