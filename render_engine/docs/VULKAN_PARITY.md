# D3D12 ↔ Vulkan 差异与对齐

> **原则：D3D12 是功能与观感参考路径；Vulkan 负责追上。**  
> 不要为迁就 Vulkan 去阉割 D3D12 管线能力。对比观感时可用 Sandbox「parity 剖面」临时关齐 Vulkan 做不到的后处理。  
> 自动化：`python tests/scripts/run_backend_parity.py --config Debug`（见 [TESTING.md](TESTING.md) C4）。

## 1. 结论（怎么读差异）

| 类别 | 含义 | 处理态度 |
|---|---|---|
| **Vulkan 缺口** | 同 API 语义未落地或 stub | 优先补 Vulkan |
| **有意 API 差** | DXR / D3D12VA vs VK RT / Video 等 | Feature=false / SKIP，文档标明 |
| **布局/约定差** | 描述符槽、FrontFace、Y-flip | 各自自洽即可；移植时勿照抄槽号 |
| **Sandbox 剖面** | 为公平对比临时关效果 | 只动 Sample 开关，不动 D3D12 后端能力 |

常见误解：交互时「D3D 更立体 / VK 更平」曾主要来自 **Vulkan 缺 local shadow / Fresnel / HDR / 透明 PSO**，以及 **D3D High 默认 SSAO** 造成的假差异——不是 D3D12「画错了」。

## 2. 能力矩阵（Windows）

| 能力 | D3D12 | Vulkan (Win) | 状态 |
|---|---|---|---|
| 清屏 / Swapchain | 有 | 有 | 对齐 |
| Lit + 深度 + HDR scene | `R16G16B16A16_FLOAT` | `R16G16B16A16_SFLOAT` | 对齐 |
| CSM 阴影 | 有 | 有（atlas Y-flip 对齐 UV） | 对齐 |
| 点光 + **独立** local shadow atlas | 有 | 有（勿与 CSM 共用深度） | 对齐 |
| 自定义 mesh / albedo / ORM | 有 | 有 | 对齐 |
| IBL irradiance + prefilter + BRDF LUT | 有 | 有 | 对齐 |
| Fresnel 反射探针项（lit） | 有 | 有 | 对齐 |
| 半透明 lit PSO | SrcAlpha、depth write off、DepthClip off | blend + depth write off + depthClamp | 对齐 |
| Skybox / UI / Debug / Quad | 有 | 有 | 对齐 |
| Post：tonemap / exposure | 有（强制 tonemap） | 有（`post_tonemap_vk`） | 对齐 |
| Post：真实 SSAO / TAA / SSR / bloom / fog / auto-exp | `post_ssao_taa.hlsl` 全栈 | **仅 tonemap 桩**（开关几乎无效） | **Vulkan 缺口** |
| Lit FrameCB：jitter / prev_vp / velocity | 有 | 无（仅有 mild `enable_taa` hint） | **Vulkan 缺口** |
| GPU 实例化 + Cull CS | `DrawIndexedInstanced` + optional CS | **CPU 展开** `DrawLitCubes` | **Vulkan 缺口**（默认柱体量级通常可接受） |
| GPU reflection probe 捕获 | `CaptureReflectionProbeGpu` | 无（CPU approximate / Upload） | **Vulkan 缺口** |
| 视频硬解 | D3D12VA | Vulkan Video（无扩展 SKIP） | 有意差 |
| 光追示范 | DXR | VK RT（SKIP） | 有意差 |
| `gpu_headless` Readback | RGBA | BGRA surface → 转 RGBA | 对齐（读回侧处理） |

## 3. 结构性差异（代码路径）

### 3.1 后处理

| | D3D12 | Vulkan |
|---|---|---|
| Shader | `shaders/hlsl/post_ssao_taa.hlsl` | `shaders/hlsl/post_tonemap_vk.hlsl` |
| 行为 | SSAO / TAA 历史 / SSR / DoF / motion blur / bloom / fog / auto-exp + ACES | exposure + ACES/Reinhard；`enable_ssao/taa` 仅 ×0.98/×0.99 |
| 后端 | `d3d12_device.cpp` `ResolvePostEffects` 写完整 PostCB | `vulkan_device.cpp` push 4 个 float |

**影响：** ImGui 打开 SSAO/TAA 时两边观感会分叉。Sandbox 默认与 Low/Med/High 按钮保持这些开关关闭，避免假差异；**不要**为此改 D3D12 后端。

### 3.2 Lit 着色器 / FrameCB

| | D3D12 `lit_cube.hlsl` | Vulkan `lit_cube_vk.hlsl` |
|---|---|---|
| 主光照 / CSM / 本地光 / IBL / Fresnel 反射 | 有 | 有（已移植） |
| `g_prev_view_proj` / jitter / velocity | 有 | 无 |
| GPU `g_instances` | 有 | 无（后端 CPU 展开） |
| TAA mild hint | 有 | 有（`g_enable_taa`） |

描述符槽位**不同但各自自洽**（移植时勿按 D3D 槽号改 VK）：

- D3D：t0 shadow，t1 albedo，**t2 local_shadow**，…，t6 reflection/prefilter，t7 irradiance，t8 BRDF  
- VK：t0 shadow，**t1 irradiance**，t2/t3 albedo/ORM，…，t6 prefilter，t7 BRDF，**t8 local_shadow**

### 3.3 阴影

| | D3D12 | Vulkan |
|---|---|---|
| CSM atlas | 正 height viewport | `MakeYFlippedViewport`（对齐 `proj.y * -0.5` UV） |
| Local atlas | 独立 2048 | 独立 2048（禁止再写入 CSM 深度） |
| Raster depth bias | `DepthBias=1500`，slope `2.0`（整数单位） | `depthBiasConstantFactor=1.25`，slope `2.0`（**经验对齐，数字不可照抄**） |
| Local bias CB | 直接用 `local_shadow_bias` | 同左 |

### 3.4 光栅 / 坐标系约定

| | D3D12 | Vulkan |
|---|---|---|
| Lit cull | `NONE` | `NONE` |
| Front face | `FrontCounterClockwise=TRUE` | `FRONT_FACE_CLOCKWISE`（配合负 viewport） |
| 透明 DepthClip | `FALSE`（clamp） | `depthClamp`（设备支持时开启） |

Cull=NONE 时正面定义对可见性影响很小；负 viewport + FrontFace 是配对约定，不是单边 bug。

### 3.5 反射 / IBL 槽位

两端 specular 探针与 IBL prefilter **共用同一 cube 槽**（D3D `reflection_cube_` / VK `ibl_prefilter`）。

- 加载 IBL pack 后应以 pack 的 prefilter 为准。  
- D3D 的 `CaptureReflectionProbeGpu` 会覆盖该槽；Sandbox 在 **`enable_ibl` 时跳过** GPU probe，避免长时间运行后两边环境高光分叉。  
- Vulkan 无 GPU probe，保持 Upload / CPU approximate。

### 3.6 实例化

- D3D：GPU instance buffer + 可选 `instance_cull_cs`。  
- Vulkan：`UploadInstanceTransforms` 存 CPU，`DrawLitInstanced` → 展开为多次 `DrawLitCubes`。  
- Sandbox scale 柱默认 ≤1024，通常视觉一致；极限数量 / GPU cull 会分叉。

## 4. Sandbox parity 剖面

文件：`samples/Sandbox/main.cpp`。

| 项 | 行为 |
|---|---|
| 默认质量 | High **阴影**；`enable_ssao/taa/ssr/bloom/fog/auto_exposure = false`（**两端**） |
| Low/Med/High 按钮 | 只恢复 cascade 等；**不把 SSAO 等后处理重新打开**（避免 VK 做不到却 D3D 变暗） |
| GPU probe | 仅 D3D；且 **`!enable_ibl`** 时才跑 |
| Harness / headless | 单 cascade；关 TAA/SSAO（稳定 dump） |

说明：这是 **Sample 对比剖面**，不是「D3D12 永远不能开 SSAO」。产品路径仍可在 D3D 上开完整 post；Vulkan 全栈后处理落地前，双后端发版观感应对齐到 tonemap+lit 水位，或明确标注 Feature。

## 5. 已对齐（勿当回归再改）

- HDR lit → tonemap → LDR swapchain  
- CSM Y-flip / 采样 UV  
- 独立 local shadow atlas + cube face 采样  
- Additive IBL + Fresnel 反射项  
- 透明 lit PSO 语义  
- Readback 输出 RGBA（VK BGRA surface 在读回时交换）  
- 主光照公式（太阳 / 环境 / 本地光）

## 6. 剩余缺口（优先补 Vulkan）

1. **真实 post 栈**（SSAO / TAA 历史 / fog / bloom…）或明确 Feature 分级  
2. Lit **jitter / prev_vp / velocity**（为真 TAA 铺路）  
3. **GPU instancing**（及可选 cull CS）  
4. **GPU reflection probe**（且与 IBL prefilter **分槽**更稳妥）  
5. 长期：shadow bias 用共享常量/文档固化，避免「照抄 1500」

## 7. 怎么验收

```text
# 松闸双后端一致性（默认 ROI 忽略 HUD）
python tests/scripts/run_backend_parity.py --config Debug

# 交互：同一机分别
sample_sandbox.exe --backend=d3d12
sample_sandbox.exe --backend=vulkan
```

目视优先看：砖地色相、绿柱面明暗、棕色立方体棱边、玻璃混合、头盔金属高光、地面接触影。  
像素 RMSE 是回归闸门，**不能替代**路径级差异表；改后端前先更新本文件对应行。

## 8. 相关文件

| 路径 | 角色 |
|---|---|
| `engine/backends/d3d12/d3d12_device.cpp` | D3D12 参考实现 |
| `engine/backends/vulkan/vulkan_device.cpp` | Vulkan 实现对齐处 |
| `shaders/hlsl/lit_cube.hlsl` / `lit_cube_vk.hlsl` | Lit |
| `shaders/hlsl/post_ssao_taa.hlsl` / `post_tonemap_vk.hlsl` | Post |
| `samples/Sandbox/main.cpp` | 双后端 Sample + parity 剖面 |
| `tests/scripts/run_backend_parity.py` | C4 自动化 |

Linux-only 说明见 [LINUX_VULKAN.md](LINUX_VULKAN.md)。测试分层见 [TESTING.md](TESTING.md)。
