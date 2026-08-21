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
| 0030 | M25 DXR demo 范围：Feature 门控优先，非完整光追帧 | [0030-…](adr/0030-m25-dxr-demo-scope.md) | CH19 | Accepted（RT 加深 → **0036**） |
| 0031 | QUIC：MsQuic 可选启用（存在则 Feature；否则 Unavailable SKIP） | [0031-…](adr/0031-m19-quic-skip-msquic.md) | CH31 | Accepted |
| 0032 | M26/P3：Forward+ 钉死与 C01/C02/C10/C08/C04/C16/C20 加深簇 | [0032-…](adr/0032-m26-forward-plus-cluster.md) | — | Accepted（C02/C08 加深 → **0036**） |
| 0033 | M27/W6 场景规模加深边界（GI/水面/混合/GPU 蒙皮 stub/Meshlet/PSO 热更） | [0033-…](adr/0033-m27-w6-scene-scale.md) | — | Accepted |
| 0034 | M27/W7 对标加深（云雾/IES/世界字/色差/CS 蒙皮/DXR 真光线） | [0034-…](adr/0034-m27-w7-parity-deepen.md) | — | Accepted |
| 0035 | Mega-W8 加深与尾巴完善（tile 灯/meshlet/VT/MsQuic/天气海浮力/动画2D/GK·ED） | [0035-…](adr/0035-mega-w8-deepen.md) | — | Accepted（MS stub 等见 **0036** 加深） |
| 0036 | Mega-W9 全主题加深、尾巴、学习轨与文档审计 | [0036-…](adr/0036-mega-w9-deepen.md) | CH36 | Accepted |
| 0037 | Mega-W10 半成品收紧、尾巴、大场景、人物、学习轨 | [0037-…](adr/0037-mega-w10-deepen.md) | CH37–CH39 | Accepted |
| 0038 | Mega-W11 拉齐各端（引擎 only；不含 kit/editor） | [0038-…](adr/0038-mega-w11-parity.md) | — | Accepted |
| 0039 | 水位弱项产品化（A+C；W12–W15） | [0039-…](adr/0039-waterline-productization-a-c.md) | Sandbox | Accepted |
| 0040 | W16 零尾巴收口 | [0040-…](adr/0040-w16-zero-tail-closeout.md) | — | Accepted |
| 0041 | W17 引擎内加深 | [0041-…](adr/0041-w17-engine-deepen.md) | — | Accepted |
| 0042 | W18 半落地加深 | [0042-…](adr/0042-w18-partial-deepen.md) | — | Accepted |
| 0043 | W20 中台产品级加深 | [0043-…](adr/0043-w20-product-deepen.md) | Sandbox | Accepted |
| 0044 | W21 Godot 对标加深 | [0044-…](adr/0044-w21-godot-parity-unfreeze.md) | — | Accepted |
| 0045 | W22 Godot 内核（历史≈100%；现行见 ENGINE_VS） | [0045-…](adr/0045-w22-godot-kernel-100.md) | — | Accepted |
| 0046 | W23 Nanite-like + 真 DDGI | [0046-…](adr/0046-w23-nanite-ddgi-gaps.md) | — | Accepted |
| 0047 | W24 分域 Godot（历史≈100%；已审计修订） | [0047-…](adr/0047-w24-godot-domain-100.md) | — | Accepted |
| 0048 | W25 VK+NGX/RTXGI+VG+编辑器（合同收口） | [0048-…](adr/0048-w25-vk-ngx-vg-editor.md) | — | Accepted |
| 0049 | Godot 2D API + Physics2D + 3D 加深 | [0049-…](adr/0049-godot-2d-physics-api.md) | — | Accepted |

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
