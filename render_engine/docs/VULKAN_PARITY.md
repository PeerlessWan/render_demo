# D3D12 ↔ Vulkan 差异与对齐

> **原则：D3D12 是功能与观感参考路径；Vulkan 负责追上。**  
> **口径（Win 双后端 100%）：** Windows 上 D3D12 + Vulkan 渲染与近端物理对齐到「可同场景发版」——lit/阴影/IBL/探针、完整基础后处理（含 SSR/DoF/MB）、GPU 实例 + Cull/Indirect、Bindless 热路径（Feature 门控）、薄 SoftBody Demo；Sandbox 中英文 UI 可实时切换。  
> **不算进本口径：** Linux / 大气 / 编辑器 / game_kit / 完整多语言框架。  
> 自动化：`python tests/scripts/run_backend_parity.py --config Debug`（见 [TESTING.md](TESTING.md) C4）。

## 1. 结论（怎么读差异）

| 类别 | 含义 | 处理态度 |
|---|---|---|
| **已对齐（本口径）** | 同场景发版水位 | 回归闸门：C4 + golden |
| **有意 API 差** | DXR / D3D12VA vs VK RT / Video 等 | Feature=false / SKIP，文档标明 |
| **布局/约定差** | 描述符槽、FrontFace、Y-flip | 各自自洽即可；移植时勿照抄槽号 |
| **Feature 门控** | Bindless 热路径等 | 默认关保黄金图；opt-in 写清条件 |

## 2. 能力矩阵（Windows）— 本口径 100% 收口

| 能力 | D3D12 | Vulkan (Win) | 状态 |
|---|---|---|---|
| 清屏 / Swapchain | 有 | 有 | 对齐 |
| Lit + 深度 + HDR scene | `R16G16B16A16_FLOAT` | `R16G16B16A16_SFLOAT` | 对齐 |
| CSM / 独立 local shadow | 有 | 有 | 对齐 |
| 自定义 mesh / albedo / ORM | 有 | 有 | 对齐 |
| IBL irradiance + **独立** prefilter + BRDF LUT | 有 | 有 | 对齐 |
| Fresnel **独立**反射探针（与 prefilter 分槽） | t10 probe / t6 prefilter | binding 12 / 8 | 对齐 |
| 半透明 lit PSO | 有 | 有 | 对齐 |
| Skybox / UI / Debug / Quad | 有 | 有 | 对齐 |
| Post：SSAO / TAA / fog / bloom / tonemap / auto-exp | `post_ssao_taa.hlsl` | `post_ssao_taa_vk.hlsl` | 对齐 |
| Post：SSR / DoF / motion blur | 同栈开关 | 同栈开关 | 对齐 |
| Lit FrameCB：jitter / prev_vp | 有 | 有 | 对齐 |
| GPU 实例化 | `DrawIndexedInstanced` | `vkCmdDrawIndexed` instanceCount | 对齐（失败回退 CPU） |
| Cull CS + Indirect | 有 | `instance_cull_vk` + `vkCmdDrawIndexedIndirect` | 对齐（默认柱仍可用 CPU count） |
| Bindless 热路径 | Feature `bindless_hot_path`（默认 OFF） | **W11 SKIP**（见下 §3.4） | 门控对齐黄金图 |
| Light tile cull CS | `light_tile_cull_cs` + Simulate | SPIR-V 校验 + Simulate 同形填充；无 SPIR-V → SKIP | **W11 对齐** |
| GPU 蒙皮主路径 | `SkinOnDevice` → D3D12 CS | `api_kind=Vulkan` → `gpu_skin_vk`；缺 SPIR-V → CPU/SKIP | **W11 对齐** |
| Mesh Shader | MS PSO / DispatchMesh | Feature on + `VK_EXT_mesh_shader` → 最小 Ok；否则 SKIP | **W11 尽力** |
| VK TraceRays 示范 | DXR DispatchRays | `rayTracingPipeline` → 解析 `vkCmdTraceRaysKHR`；否则 SKIP | **W11 尽力** |
| 薄 SoftBody | `IPhysicsWorld` + Jolt | 同（与后端无关） | 对齐；builtin SKIP |
| Sandbox 中/英 UI | ImmediateUi + CJK atlas | 同 | 对齐 |
| 视频硬解 / 光追示范 | D3D12VA / DXR | VK Video / 全屏 RT 产品路径仍外置 | 有意差 |
| `gpu_headless` Readback | RGBA | BGRA→RGBA | 对齐 |

## 3. 结构性差异（代码路径）

### 3.1 后处理

| | D3D12 | Vulkan |
|---|---|---|
| Shader | `post_ssao_taa.hlsl` | `post_ssao_taa_vk.hlsl` |
| 行为 | 完整 PostCB（SSAO/TAA/SSR/DoF/MB/bloom/fog/auto-exp + ACES） | 同语义；UBO + depth/history 采样 |
| 后端 | `ResolvePostEffects` | 同名；history copy 自 swapchain |

Headless / golden dump 仍关 TAA/SSAO 保稳定；交互可两端同开。

### 3.2 Lit / 实例 / 探针槽

| | D3D12 | Vulkan |
|---|---|---|
| jitter / prev_vp | FrameCB | FrameGpu 对齐 |
| GPU instances | t9 StructuredBuffer | binding 11 SSBO |
| IBL prefilter | t6 | binding 8 |
| Reflection probe | t10 | binding 12 |
| Irradiance / BRDF | t7 / t8 | binding 3 / 9 |

描述符槽位**不同但各自自洽**。

### 3.3 阴影 / 光栅

与既有约定相同：CSM Y-flip、独立 local atlas、Cull=NONE、负 viewport + FrontFace 配对。Bias 数字不可照抄。

**画质债（W0）**：柱面 CSM 已加 Poisson PCF、tile clamp、法线/斜率 bias、近级联 log 偏置与 overlap；主观残留记看板 `T-csm-pillar-shimmer`（可再录盘绿 mask MAD 对照）。

### 3.4 Bindless（Mega-W11）

- D3D：能力位 `bindless`（Tier≥2）；热路径默认 `pad=-1`（classic）。`bindless_hot_path=true` 且非 `gpu_headless` 时按 `tex_slot` 映射 heap 1/4。  
- Vulkan：**W11 明确 SKIP** — 即使物理设备有 `VK_EXT_descriptor_indexing`，也不设置 Feature `bindless` / `bindless_hot_path`，`ProbeBindlessMinimalPath` 返回 Unavailable（无 albedo 索引热路径；classic 描述符 only）。不假装可用。  
- 黄金图 / C4 默认路径不漂。

### 3.5 Mega-W11 Win VK Status 路径（摘要）

| 路径 | Ok 条件 | SKIP |
|---|---|---|
| `Setup/DispatchLightTileCull` | SPIR-V 可建 module；Dispatch 同形填充 | 无/坏 SPIR-V |
| `SkinOnDevice` | Feature `gpu_skinning` + 对应后端 CS | Feature off → CPU；CS 缺 → CPU message |
| `TryMeshShaderPath` | Feature on +（D3D12 MS 或 `VK_EXT_mesh_shader`） | Feature off / 无扩展 |
| `TryVkTraceRaysDemoStub` | `VK_KHR_ray_tracing_pipeline` + `vkCmdTraceRaysKHR` 可解析 | 无 pipeline 扩展 |
| Bindless | （D3D 见上） | **VK 钉死 SKIP** |

## 4. Sandbox 行为

| 项 | 行为 |
|---|---|
| 默认质量 | High 阴影；交互可开 SSAO/TAA/SSR 等（两端真有效） |
| Headless | 单 cascade；关 TAA/SSAO（稳定 dump） |
| GPU probe | IBL 开时仍可跑（分槽后不再互盖） |
| 语言 | Effects 面板 `Language / 语言` 即时切换；CJK 系统字体进 atlas |
| SoftBody | 场景旁 soft cube DebugDraw 线框 |

## 5. 已对齐清单（本口径）

- HDR lit → 完整 post → LDR  
- CSM / local shadow / IBL / Fresnel 分槽探针  
- GPU instancing + Cull/Indirect（可选）  
- SoftBody 薄 API + Sandbox Demo  
- EN/ZH UI  

## 6. 本口径外（明确不做进 100%）

1. Linux / M18  
2. 大气 / 体积云 / 天气（C05）  
3. mac / 移动 / Metal、服装布料、Deferred 大改、开放世界 VT  
4. `editor/` / `game_kit`  
5. 完整 i18n / 运行时换字体文件  

## 7. 怎么验收

```text
# 松闸双后端一致性（默认 ROI 忽略 HUD）
python tests/scripts/run_backend_parity.py --config Debug

# Headless + golden
pwsh scripts/ci_headless.ps1 -Golden

# 交互
sample_sandbox.exe --backend=d3d12
sample_sandbox.exe --backend=vulkan
```

目视：砖地、绿柱、玻璃、头盔金属、接触影；切换语言后中文无「?」；两端同开 SSAO。

## 8. 相关文件

| 路径 | 角色 |
|---|---|
| `engine/backends/d3d12/d3d12_device.cpp` | D3D12 参考 |
| `engine/backends/vulkan/vulkan_device.cpp` | Vulkan 对齐（W11：tile cull / bindless SKIP / api_kind） |
| `engine/animation/gpu_skin_main.cpp` / `gpu_skin_vk.*` | GPU 蒙皮主路径按 `api_kind` 路由 |
| `engine/gpu_driven/meshlet.cpp` | Feature `mesh_shader` + `VK_EXT_mesh_shader` |
| `engine/rt/raytracing.cpp` | `TryVkTraceRaysDemoStub` → `vkCmdTraceRaysKHR` |
| `tests/unit/test_m36.cpp` | Mega-W11 `[w11]` Status 路径 |
| `shaders/hlsl/lit_cube.hlsl` / `lit_cube_vk.hlsl` | Lit |
| `shaders/hlsl/post_ssao_taa.hlsl` / `post_ssao_taa_vk.hlsl` | Post |
| `shaders/hlsl/instance_cull_vk.hlsl` | VK Cull CS |
| `samples/Sandbox/main.cpp` / `sandbox_ui_strings.h` | Sample + i18n |
| `tests/scripts/run_backend_parity.py` | C4 |

Linux 见 [LINUX.md](LINUX.md)。测试见 [TESTING.md](TESTING.md)。
