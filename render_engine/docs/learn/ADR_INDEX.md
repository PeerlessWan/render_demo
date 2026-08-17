# 架构决策记录（ADR）索引

ADR 记录 **为什么这样设计**，是学习封装的核心（比 API 列表更重要）。

## 约定

- 路径：`render_engine/docs/learn/adr/NNNN-title.md`  
- 状态：Proposed / Accepted / Superseded  
- 模板见文末  
- **所有登记项均有正文文件**；实现推进时可增补细节，勿只改索引不改文件  

## 清单

| 编号 | 标题 | 文件 | 关联章 | 状态 |
|---|---|---|---|---|
| 0001 | 为何做引擎主循环而非纯 SDK | [0001-…](adr/0001-engine-owns-main-loop.md) | CH00 | Accepted |
| 0002 | 为何一期仅 D3D12 | [0002-…](adr/0002-d3d12-only-phase1.md) | CH01、CH06 | **Superseded → 0020** |
| 0003 | 为何引入 RHI 而不是业务直调 D3D12 | [0003-…](adr/0003-rhi-abstraction.md) | CH06 | Accepted |
| 0004 | 为何使用 FrameGraph | [0004-…](adr/0004-frame-graph.md) | CH11 | Accepted |
| 0005 | 材质 Keyword 变体与 PSO 缓存策略 | [0005-…](adr/0005-material-variants-pso-cache.md) | CH08 | Accepted |
| 0006 | 上传环与多帧 in-flight 资源寿命 | [0006-…](adr/0006-upload-ring-inflight.md) | CH05 | Accepted |
| 0007 | 阴影：先 CSM 光栅，DXR 为可开关示范 | [0007-…](adr/0007-shadows-csm-then-dxr.md) | CH10、CH19 | Accepted |
| 0008 | 超分：IUpscaler 与强制 fallback | [0008-…](adr/0008-upscaler-fallback.md) | CH18 | Accepted |
| 0009 | 学习轨与产品轨双轨共存 | [0009-…](adr/0009-learn-product-dual-track.md) | CH00 | Accepted |
| 0010 | 场景序列化范围（渲染向，非全能编辑器） | [0010-…](adr/0010-scene-serialization-render-scope.md) | CH20 | Accepted |
| 0011 | 外设接入层与窗口层分离；ActionMap 优先 | [0011-…](adr/0011-input-peripheral-layer.md) | CH07b | Accepted |
| 0012 | 视频解码跟随渲染后端；禁止跨后端/软解降级 | [0012-…](adr/0012-video-decode-follows-backend.md) | CH18b | Accepted |
| 0013 | 音频明确不做特效；仅解码与输出 | [0013-…](adr/0013-audio-decode-render-no-fx.md) | CH18c | Accepted |
| 0014 | 通用渲染补强分 P0/P1，P0 前不宣称通用验收 | [0014-…](adr/0014-general-render-p0-p1.md) | CH22–CH24 | Accepted |
| 0015 | 物理用第三方（Jolt）薄封装，不自研求解器 | [0015-…](adr/0015-physics-third-party.md) | CH25 | Accepted |
| 0016 | UI：ImGui 调试 + 保留模式运行时；FrameGraph 合成 | [0016-…](adr/0016-ui-imgui-retained.md) | CH29 | Accepted |
| 0017 | 第三方须经抽象层；核心自研、中间件可三方可替换 | [0017-…](adr/0017-third-party-boundary.md) | — | Accepted |
| 0018 | 测试分层：unit / integration / golden+CI | [0018-…](adr/0018-testing-strategy.md) | — | Accepted |
| 0019 | 2D/像素混合为渲染能力；玩法系统不进引擎范围 | [0019-…](adr/0019-pixel-hybrid-render-only.md) | CH30 | Accepted |
| 0020 | Windows D3D12+Vulkan；Linux 仅 Vulkan | [0020-…](adr/0020-windows-d3d12-vulkan-linux-vulkan.md) | CH01、CH06 | Accepted |
| 0021 | 网络层：HTTP/WS/QUIC 可靠流；轻量三方 | [0021-…](adr/0021-network-http-ws-quic.md) | CH31 | Accepted |
| 0022 | 工程规范（编码/架构/通讯等）为强制基线 | [0022-…](adr/0022-engineering-standards.md) | — | Accepted |
| 0023 | 引擎缺口补齐纳入 M20–M25（不含 mac/移动） | [0023-…](adr/0023-engine-gap-fill-m20-m25.md) | CH32–CH35 | Accepted |
| 0024 | 双后端特性分级 L0/L1/L2；场景热路径 SoA（非默认 ECS） | [0024-…](adr/0024-backend-feature-tiers-and-soa.md) | — | Accepted |
| 0025 | 最小工具链；**引擎内**不做可视化编辑器（外挂 editor 见 0027） | [0025-…](adr/0025-toolchain-minimum-viable.md) | — | Accepted |
| 0026 | 运行时基础：Cook/依赖、异步、逻辑渲染分离、寿命、**数据依赖与生命周期**、GPU Profiling | [0026-…](adr/0026-runtime-foundations-assets-threads-profiling.md) | CH20、CH27 | Accepted |
| 0027 | 宿主分层：脚本与编辑器在引擎外（或可选插件） | [0027-…](adr/0027-hosting-script-editor-boundary.md) | — | Accepted |
| 0028 | 多品类：薄 game_kit + genre_kits + games | [0028-…](adr/0028-genre-kits-layering.md) | — | Accepted |
| 0029 | 物理加深边界：薄 SoftBody/Cloth 进引擎；服装管线外置 | [0029-…](adr/0029-physics-softbody-boundary.md) | CH25 | Accepted |
| 0030 | M25 DXR demo 范围：Feature 门控优先，非完整光追帧 | [0030-…](adr/0030-m25-dxr-demo-scope.md) | CH19 | Accepted |
| 0031 | M19 QUIC SKIP：本波不捆绑 MsQuic | [0031-…](adr/0031-m19-quic-skip-msquic.md) | CH31 | Accepted |
| 0032 | M26/P3：Forward+ 钉死与 C01/C02/C10/C08/C04/C16/C20 加深簇 | [0032-…](adr/0032-m26-forward-plus-cluster.md) | — | Accepted |
| 0033 | M27/W6 场景规模加深边界（GI/水面/混合/GPU 蒙皮 stub/Meshlet/PSO 热更） | [0033-…](adr/0033-m27-w6-scene-scale.md) | — | Accepted |

## 模板

```markdown
# ADR NNNN: 标题

- 状态: Proposed | Accepted | Superseded
- 日期: YYYY-MM-DD
- 关联: CHxx, 模块路径

## 背景

## 决策

## 备选方案

## 后果（优点 / 代价）

## 学习提示

（学习者应抓住的直觉，3–5 条）
```

## 相关

- [PATH.md](PATH.md)  
- [../ARCHITECTURE.md](../ARCHITECTURE.md)  
- [../HOSTING.md](../HOSTING.md)  
- [../TOOLING.md](../TOOLING.md)  
- [../README.md](../README.md)  
