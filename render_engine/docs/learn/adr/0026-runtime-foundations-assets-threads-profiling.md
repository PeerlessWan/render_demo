# ADR 0026: 运行时基础（Cook/依赖、异步回调、逻辑渲染分离、引用寿命、数据依赖、生命周期、GPU Profiling）

- 状态: Accepted
- 日期: 2026-08-12
- 关联: PLAN M1–M4/M8–M10/M14, docs/RUNTIME_FOUNDATIONS.md, TOOLING, DEBUG, STANDARDS §7

## 背景

通用渲染引擎若只「能画出东西」，但缺少打包依赖、异步完成语义、逻辑/渲染数据面隔离、引用寿命、**跨层数据依赖**与**统一生命周期**、以及 GPU Profiling，则无法支撑流式、多线程与性能验收。

## 决策

1. **Cook / 加载**：Manifest + **依赖图** + 可选 Package；运行时按 AssetId 解析依赖。  
2. **异步回调**：完成后台加载；**仅主线程 `Asset.PumpAsync` 之后**派发成功/失败；可取消；回调禁重活。  
3. **逻辑/渲染分离**：权威 Scene 树只被逻辑写；渲染读 `RenderScene` SoA；M14 落地多线程命令录制；独立 Render Thread 为可选加深。  
4. **寿命（资源）**：CPU 侧 Handle 引用计数；GPU 侧 Fence 世代延迟销毁（与 ADR 0006 一致）。  
5. **数据依赖（全层）**：资产 Cook 图、运行时加载序、场景→Handle、FrameGraph Pass/资源、Module `Requires` 拓扑、启动服务序——分层维护，禁止资产依赖环。  
6. **生命周期管理**：明确所有者表与阶段机（Starting→Running→Stopping→GPUDrained→Shutdown）；帧级瞬时 vs in-flight vs 常驻；Resize/取消异步有序。  
7. **GPU Profiling**：引擎内 Pass 时间戳 + CPU 区间 + ImGui/`stat` 为 **必做**（M8–M9）。

细则与验收见 [RUNTIME_FOUNDATIONS.md](../../RUNTIME_FOUNDATIONS.md) §6–§7。

## 后果

- 优点：依赖可诊断、销毁有序、性能有抓手。  
- 代价：M3/M8/M9 契约面变宽；须维护 Manifest 与 Handle 纪律。

## 学习提示

1. 依赖环在 cook / Module 启动期就应失败，不要拖到运行时花屏。  
2. 引用计数解决不了 GPU in-flight —— 还要 Fence 退役队列。  
3. 生命周期先问「谁拥有、谁销毁、哪一帧还能用」。  
4. 有 Profiler 再谈优化。  
