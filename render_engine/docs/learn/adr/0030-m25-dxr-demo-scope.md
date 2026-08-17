# ADR 0030: M25/W4 DXR demo scope（Feature 门控 + stub dispatch contract）

- 状态: Accepted
- 日期: 2026-08-15
- 更新: 2026-08-17（W4：stub dispatch contract）
- 关联: CH19、ADR 0007、PLAN M8/M25/W4、`engine/rt`、`samples/learn/19_dxr_intro`

## 背景

M8/M25 要求「DXR 示范 + 可关降级」。完整 BLAS/TLAS、SBT、raygen/miss/closesthit 与 DispatchRays 合成链路过重，且与当前学习 Sample 的 headless 探测目标不符。

## 决策

1. **M25 验收面**：真实设备能力探测（`ProbeDxrHardwareSupport` / D3D12 `OPTIONS5`）+ `CanRunDxrDemo` + `Resolve`/`EnsureSafe` 降级契约；无能力时 **SKIP exit 0**，禁止硬崩。  
2. **W4 加深**：增加 **stub dispatch contract**：
   - `DxrShadowDemo`：在 `raytracing` + shadows 门控为真时，**记录**「阴影示范 pass WOULD run」；
   - `RunDxrFullscreenStub(IDevice&)`：返回 `Ok` / `Unavailable`，表示 fullscreen demo **本会** DispatchRays；
   - `TryEmptyTlasPrebuild`：在已有 DXR 头/设备时查询 **empty TLAS** prebuild 尺寸，否则以清晰 `Unavailable` Status 跳过。  
   **完整 `DispatchRays` + SBT + 画面合成仍属下一里程碑**（过重则不在本波硬塞）。  
3. **不在本 ADR 范围**：生产级 AS 构建、多 bounce 路径追踪、Vulkan Ray Tracing 帧（VK RT 继续 Feature=false / SKIP，见 VULKAN_PARITY）。  
4. 若后续要「一帧 fullscreen DXR 效果」，须另开里程碑；届时仍须保留关闭 RT → 光栅阴影回退。  
5. `DxrDemoConfig.max_bounces` 仍为预留字段；`Resolve` 不读取。

## 备选方案

- 立刻上完整 DXR 帧 —— 成本高、与 CH19 教学切片冲突。  
- 仅文档门控、不探测硬件 —— 假阳性/假阴性，CI 无法诊断。

## 后果

- 优点：门控诚实、可测；W4 有可调用的 stub 契约而不假称已出光追画面。  
- 代价：Sample 仍不发射 rays；「会跑 DXR」表示能力/配置/`would_run`/`Ok` stub，不表示画面已是光追。

## 学习提示

1. 先问「能不能跑」，再问「怎么画」。  
2. `features.raytracing` 可由设备 init 或 `ProbeDxrHardwareSupport` 写入 override。  
3. Vulkan RT 对齐仍是有意差，不要在本 Sample 里假装已对等。  
4. `RunDxrFullscreenStub` 的 Ok ≠ DispatchRays 已执行。
