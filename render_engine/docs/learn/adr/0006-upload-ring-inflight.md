# ADR 0006: 上传环与多帧 in-flight 资源寿命

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH05, engine/backends, engine/assets

## 背景

CPU 写完立即释放/复用 GPU 仍在读的缓冲会导致闪烁或崩溃。

## 决策

1. 采用 **多帧 in-flight**（典型 2–3 帧）+ 上传环/暂存堆策略。  
2. 资源销毁推迟到 GPU 完成对应 Fence。  
3. 学习轨可开 `learn.force_sync_gpu` 降速理解（见 learn/README）。

## 后果

- 优点：稳定与吞吐兼顾。  
- 代价：内存占用上升；寿命规则必须文档化并单测/集成覆盖。
