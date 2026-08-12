# 工具链（离线 / 构建期）

> 与 [PLAN.md](PLAN.md) §1.8、[ARCHITECTURE.md](ARCHITECTURE.md) `tools/`、[THIRD_PARTY.md](THIRD_PARTY.md) 配套。  
> 原则：**引擎内不做**完整可视化关卡/材质/UI 编辑器；默认 **外部 DCC + CLI**；独立视口编辑器见 [HOSTING.md](HOSTING.md) / `editor/`（ADR 0025/0027）。  
> 决策摘要见 [ADR 0025](learn/adr/0025-toolchain-minimum-viable.md)。

## 1. 必要工具（必须进计划）

| 工具 / 约定 | 职责 | 对齐里程碑 | 验收要点 |
|---|---|---|---|
| **tools/shader_compile**（或 CMake+DXC） | HLSL → DXIL / SPIR-V；变体列表可复现 | **M2 强制**（M1 清屏可不依赖） | 三角/纹理 Sample 不手改字节码 |
| **资源路径与 AssetId 约定** | 开发期散文件 → 可寻址；禁止 .. 逃逸 | **M2–M3** | 与 VFS/单测一致 |
| **tools/ibl_baker** | 环境贴图 → Irradiance / Prefiltered / BRDF LUT | **M5** | CH09 / Sandbox IBL 可复现烘焙 |
| **纹理压缩路径** | PNG/JPEG → BC（DDS）或 KTX2 等引擎格式；可用 DirectXTex | **M5–M10** | 真场景可不只靠未压缩 PNG |
| **最小 cook / 清单 / 依赖图 / 可选打包** | 资产表 + 依赖边 + 可选包；发版与流式可复现 | **M3 约定，M9 落地** | 见 [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) §2 |
| **tools/lightmap_baker** | 简化 Lightmap / 烘焙 GI 输出运行时可读格式 | **M6–M8** | 探针/Lightmap Sample 可复现；可先简陋 |
| **黄金图跑测脚本** | 离屏截图、比对、报告（见 TESTING） | **M9** | tests/scripts 可跑 v0 |
| **图集格式约定** | 外部打图集（TexturePacker 等），引擎只定 JSON/元数据契约 | **M16 前文档定稿** | 2D Sample 按约定加载 |
| **Tiled JSON 导入** | 运行时/工具侧导入（渲染向） | **M16** | Tilemap 可见 |

## 2. 建议有、可稍后（不挡主路径）

| 工具 | 职责 | 建议时机 |
|---|---|---|
| meshoptimizer 离线 LOD | 网格简化生成 LOD | M10+ 可选 |
| Feature 矩阵导出 | 启动/CLI 打印 L0/L1/L2 能力表 | M1 起日志即可；独立小工具可后置 |
| IBL/Lightmap 批处理封装 | 目录进、目录出，CI 可调 | 与对应 baker 同期打磨 |
| 地形/植被数据校验小工具 | 高度图/实例表合法性 | M23 前后 |
| 2D 骨骼导出检查 | 与 spine 等运行时抽象对齐的校验 | M21 |
| **C20 轻量内容 CLI** | Manifest/AssetId 浏览、场景 JSON 校验、依赖图检查 | 可先于 C21 视口编辑器；推荐 `tools/` 或 `render_engine/tools/` |

## 3. 明确不做（工具侧 / 引擎内）

| 不做 | 说明 |
|---|---|
| 完整关卡编辑器进 `engine/` | 默认 DCC + 序列化/清单；独立视口见 [HOSTING.md](HOSTING.md) C21 / [`editor/`](../../editor/) |
| 材质节点图可视化编辑器 | Keyword/参数工作流；节点图范围外 |
| 可视化 UI 编辑器 | ImGui + 保留模式代码/外部布局 |
| FBX/USD 一站式工业管线 | 主路径 **glTF**；其它格式后置或外部转换 |
| NavMesh / 音频中间件专用工具 | 范围外 |
| 自研 PIX 级帧调试器 | 用 PIX / RenderDoc |
| 资产生态 / 商店 | 不做 |

## 4. 与运行时边界

| 在 tools/ | 在 engine/ |
|---|---|
| 烘焙、编译、压缩、清单生成、批处理 | 加载、流式、运行时采样与播放 |
| 可依赖 DXC、DirectXTex 等（构建机） | 经抽象；业务不直链三方头 |

## 5. 目录（目标）

```
tools/
  shader_compile/     # 或 CMake 自定义命令
  ibl_baker/
  lightmap_baker/
  texture_compress/   # 可薄封装 DirectXTex CLI
  asset_cook/         # 清单 / 可选打包
  # 可选：feature_dump、validate_tilemap、…
tests/
  scripts/            # 黄金图跑测（属测试工具链）
```

## 6. 相关文档

- [README.md](README.md)  
- [HOSTING.md](HOSTING.md)  
- [HOST_API.md](HOST_API.md)  
- [PREFAB_SCHEMA.md](PREFAB_SCHEMA.md)  
- [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)  
- [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md)（M1 清屏可不依赖本工具链）  
- [PLAN.md](PLAN.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [THIRD_PARTY.md](THIRD_PARTY.md)  
- [TESTING.md](TESTING.md)  
- [STANDARDS.md](STANDARDS.md)  
- [learn/adr/0025-toolchain-minimum-viable.md](learn/adr/0025-toolchain-minimum-viable.md)  
