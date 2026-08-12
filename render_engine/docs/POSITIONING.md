# 产品定位与缺陷

## 1. 定位

本项目是 **Windows / Linux 通用 2D·3D 渲染引擎**：

- **是**：Windows（D3D12 + Vulkan）、Linux（Vulkan）、P0/P1 + P2 缺口补齐、2D/像素混合、物理、UI、媒体、网络传输、教学双轨。  
- **不是**：macOS / 任何移动端；Metal；D3D11/GL/GLES 实装；全能游戏引擎；**引擎内**玩法/脚本 VM/完整可视化编辑器/状态同步；**音频特效（DSP/空间音频）**。（外挂 `game_kit` / `genre_kits` / `games` / `editor` 见 [HOSTING.md](HOSTING.md)、[../../docs/LAYERS.md](../../docs/LAYERS.md)）  

一句话：

> 自研 Win/Linux 通用 2D·3D 渲染引擎（**仅 D3D12 与 Vulkan**），对标桌面渲染中台而非 UE 全家桶。

缺口追踪：[KNOWN_GAPS.md](KNOWN_GAPS.md)。

### 与纯 RHI / 渲染库的边界

| | 纯 RHI | 本引擎 |
|---|---|---|
| 主循环 | 调用方 | 引擎 |
| 场景/2D/物理 | 自建 | 一等公民 |
| 适用 | 嵌入 | 产品/Sandbox |

## 2. 缺陷与限制（引擎本身，不含已排除的 mac/移动）

### 2.1 平台与产品边界

| 缺陷 | 影响 |
|---|---|
| **无 macOS / 移动** | 非全平台（**明确不做**，非延期） |
| D3D11 / OpenGL / GLES 未实装 | 旧硬件、纯 GL 环境不可用 |
| Linux 以 X11 为必做 | Wayland 若延期，部分桌面体验差 |
| 无可视化完整编辑器 / 材质节点图 | 内容靠外部 DCC + CLI；可选外挂 `editor/`（[HOSTING.md](HOSTING.md)、C21） |
| 无脚本 VM / NavMesh 产品 / 玩法 / 状态同步 | 玩法+脚本在外层 `game_kit`；品类玩法在 `genre_kits`（HOSTING / LAYERS）；引擎仅传输层等 |
| 无商业资产生态 | 无商店/官方内容包 |

### 2.2 渲染与场景（即便 M20–M25 做完仍弱于主流）

| 缺陷 | 影响 |
|---|---|
| 动态 GI 为 DDGI/探针体积级 | 不及 Lumen 等产品级全局光 |
| 光追为示范/可用级，非完整内容管线 | 缺生产级 RT 资产与混合管线深度 |
| 地形/水体/植被仅为「基础可用」 | 非开放世界全家桶；无 VT 产品化 |
| 矢量/路径及中台加深项 | 见 [KNOWN_GAPS.md](KNOWN_GAPS.md) §4（G13、C01–C18）；**未排进 M1–M25** |
| 非 ECS | 超大规模模拟/数据导向架构需上层自建；引擎用场景树 + 渲染 SoA 提取缓解（ADR 0024） |
| 双后端特性差与维护成本 | 用 L0/L1/L2 分级 + 先 D3D12 后 VK 控制（ADR 0024）；视频/DXR 等仍须 Feature |
| 视频仅硬解、随后端 | 无 VA 则视频不可用（无软解） |
| DLSS 依赖厂商 | 无 N 卡走 FSR/关 |

### 2.3 子系统能力边界

| 缺陷 | 影响 |
|---|---|
| **音频明确不做特效** | 无 EQ/压缩/混响等 DSP、无效果器总线、无 HRTF/完整 3D 空间音频；仅解码+输出+增益混合 |
| 物理无布料/软体/载具产品化 | 仅刚体/查询/角色控制器级 |
| 2D 骨骼依赖三方格式+许可 | 适配与授权成本；非自研完整 2D 动画生态 |
| UI 无可视化编辑器 | 保留模式+ImGui，靠代码/外部布局 |
| 网络无复制/匹配/反作弊 | 仅 HTTP/WS/QUIC 传输 |

### 2.4 工程与生态

| 缺陷 | 影响 |
|---|---|
| 范围 M1–M25，代码未开始 | 分段验收；先 M1–M19 再 P2 |
| Win+Linux × D3D12+VK CI/黄金图矩阵 | 基线与驱动差异大 |
| 第三方许可栈 | Jolt、ImGui、RmlUi、DLSS、MsQuic、2D 骨骼运行时等 |
| 教学双轨维护 | 文档/Sample 与产品需同步 |

## 3. 风险锁死

1. 无 DLSS → FSR/内置 fallback。  
2. 无 DXR/关 RT → 光栅阴影；按后端标注。  
3. Frame Generation 不做。  
4. **仅** Win D3D12+Vulkan、Linux Vulkan；**明确不做** macOS/移动/Metal；D3D11/GL/GLES 不实装。  
5. 视频随后端硬解；无软解、不跨 API。  
6. **音频明确不做特效**（ADR 0013）；物理第三方 + 抽象层。  
7. P0 前不宣称 3D 通用验收；P2 完成前不宣称「已对齐主流渲染中台」。M25 后候选见 KNOWN_GAPS §4，立项须另开里程碑。  
8. 玩法/状态同步/脚本/完整编辑器不进引擎核心；外挂方式见 [HOSTING.md](HOSTING.md)、ADR 0027。  
9. 测试：unit + D3D12/Vulkan 冒烟；网络 loopback。  
10. 凡三方经抽象层；遵守 STANDARDS；缺口补齐见 ADR 0023；双后端分级与 SoA 见 ADR 0024。  
11. 工具链按 [TOOLING.md](TOOLING.md) 最小可行集落地（ADR 0025）；**引擎内不做**完整可视化内容编辑器；独立 `editor/` / C20 见 ADR 0027、HOSTING。  

## 4. 相关文档

- [README.md](README.md) — 文档总索引  
- [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md)  
- [HOSTING.md](HOSTING.md)  
- [../../docs/LAYERS.md](../../docs/LAYERS.md)  
- [KNOWN_GAPS.md](KNOWN_GAPS.md)  
- [TOOLING.md](TOOLING.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [STANDARDS.md](STANDARDS.md)  
- [PLAN.md](PLAN.md)  
- [TESTING.md](TESTING.md)  
- [THIRD_PARTY.md](THIRD_PARTY.md)  
- [learn/README.md](learn/README.md)  
