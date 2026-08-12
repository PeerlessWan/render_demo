# ADR 0008: 超分 IUpscaler 与强制 fallback

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH18, engine/post, PLAN M7

## 背景

DLSS 绑定 NVIDIA；无厂商支持时不能黑屏或假成功。

## 决策

1. 超分经 **`IUpscaler` 抽象**；默认实现链：**DLSS → FSR → 内置（或关）**。  
2. 无 DLSS 时 **强制可诊断的 fallback**，不宣称「已开 DLSS」。  
3. Frame Generation **不做**。

## 后果

- 优点：跨 GPU 可验收。  
- 代价：多实现与运动向量/Jitter 契约要对齐。
