# ADR 0008: 超分 IUpscaler 与强制 fallback

- 状态: Accepted
- 日期: 2026-08-12
- 更新: 2026-08-17（W4：明确 FSR-absent fallback + `ENGINE_UPSCALER` 偏好）
- 关联: CH18, engine/post, PLAN M7 / W4

## 背景

DLSS 绑定 NVIDIA；无厂商支持时不能黑屏或假成功。当前仓库**未接入** AMD FidelityFX Super Resolution SDK。

## 决策

1. 超分经 **`IUpscaler` 抽象**；默认实现链：**DLSS → FSR → 内置（或关）**。  
2. 无 DLSS 时 **强制可诊断的 fallback**，不宣称「已开 DLSS」。  
3. Frame Generation **不做**。  
4. **`BuiltinBilinearUpscaler` + `ResolutionScale` 即为 FSR-absent fallback**：在无 FidelityFX SDK 时的唯一落地路径；`EffectTuning.render_resolution_scale` 驱动内部渲染尺寸，`upscale_jitter_x/y` → `UpscaleParams` 在双线性采样中偏移 UV（TAA-like 文档契约，非厂商 FSR）。  
5. **`CreateUpscaler()` 偏好**：读环境变量 `ENGINE_UPSCALER`；若值为 `fsr` 且本树无 FSR SDK，仍返回 `BuiltinBilinearUpscaler`，并 **Log 一次** `FSR SDK absent → builtin`。禁止假名「FSR」作为 `IUpscaler::name()`。  
6. **FSR = future**：待 AMD FidelityFX SDK 授权/vendor 后再接真实适配器。

## 后果

- 优点：跨 GPU 可验收；偏好请求可诊断。  
- 代价：多实现与运动向量/Jitter 契约要对齐；内置双线性画质远低于 FSR。
