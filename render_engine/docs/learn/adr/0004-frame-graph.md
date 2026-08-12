# ADR 0004: 为何使用 FrameGraph

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH11, engine/render/FrameGraph

## 背景

手动散布屏障与临时 RT 易错，Pass 插入（Shadow/Post/UI）难扩展。

## 决策

1. 渲染以 **FrameGraph** 声明 Pass 与资源依赖；Compile 阶段推导屏障。  
2. 标准槽位预留（Opaque / AfterOpaque / Post / UI…），扩展优先注册 Pass。  
3. 先 D3D12 实装，再 Vulkan 对齐。

## 后果

- 优点：依赖可见、教学友好、易插效果。  
- 代价：需维护 Compile 正确性；调试要结合 FG dump。
