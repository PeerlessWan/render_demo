# ADR 0030: M25 DXR demo scope（Feature 门控优先，非完整光追帧）

- 状态: Accepted
- 日期: 2026-08-15
- 关联: CH19、ADR 0007、PLAN M8/M25、`engine/rt`、`samples/learn/19_dxr_intro`

## 背景

M8/M25 要求「DXR 示范 + 可关降级」。完整 BLAS/TLAS、SBT、raygen/miss/closesthit 与 DispatchRays 合成链路过重，且与当前学习 Sample 的 headless 探测目标不符。

## 决策

1. **本阶段 M25 deepen 验收面**为：真实设备能力探测（`ProbeDxrHardwareSupport` / D3D12 `OPTIONS5`）+ `CanRunDxrDemo` + `Resolve`/`EnsureSafe` 降级契约；无能力时 **SKIP exit 0**，禁止硬崩。  
2. **不在本 ADR 范围**：生产级 AS 构建、SBT、多 bounce 路径追踪、Vulkan Ray Tracing 帧（VK RT 继续 Feature=false / SKIP，见 VULKAN_PARITY）。  
3. 若后续要「一帧 fullscreen DXR 效果」，须另开里程碑；届时仍须保留关闭 RT → 光栅阴影回退。  
4. `DxrDemoConfig.max_bounces` 仍为预留字段；`Resolve` 不读取。

## 备选方案

- 立刻上完整 DXR 帧 —— 成本高、与 CH19 教学切片冲突。  
- 仅文档门控、不探测硬件 —— 假阳性/假阴性，CI 无法诊断。

## 后果

- 优点：门控诚实、可测、与 ADR 0007 降级策略一致。  
- 代价：Sample 不发射 rays；「会跑 DXR」仅表示能力与配置允许，不表示画面已是光追。

## 学习提示

1. 先问「能不能跑」，再问「怎么画」。  
2. `features.raytracing` 可由设备 init 或 `ProbeDxrHardwareSupport` 写入 override。  
3. Vulkan RT 对齐仍是有意差，不要在本 Sample 里假装已对等。
