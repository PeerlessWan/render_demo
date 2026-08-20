# 第三方库引入清单

> 原则：**核心渲染抽象与帧图自研**；求解器、编解码周边、成熟中间件、厂商 SDK **优先第三方**。  
> **硬约束：凡引入三方库，必须经引擎抽象层/适配层封装，禁止业务与 Sample（教学对照除外）直接依赖三方头文件；抽象稳定后可替换实现而不改上层调用。**  
> 许可与再分发在实现接入时锁定版本并写入本仓库 `third_party/` 或包管理说明。

## 1. 总览

| 策略 | 含义 |
|---|---|
| **必选三方** | 计划已定或强烈推荐，自研不划算 |
| **推荐三方** | 默认采用，允许替换但需等价能力 |
| **可选三方** | 可自研最小实现，三方可加速 |
| **不宜三方 / 自研** | 引擎差异化与教学核心，保持自控 |

### 1.1 抽象层硬规则（所有三方适用）

1. **接口在前、实现在后**：公开 `I*` / Facade（如 `IPhysicsWorld`、`IHttpClient`、`IUpscaler`、`IImageLoader`、`IAudioDecoder`）；三方仅出现在 `*_impl` / `adapters/`。  
2. **业务零直链**：`samples/Sandbox`、产品 Module、引擎其它子系统 **不得** `#include` 三方头；仅适配层与构建脚本可依赖。  
3. **可替换验收**：换库（如 Jolt→PhysX、cpp-httplib→libcurl、MsQuic→ngtcp2）应只需改适配层 + CMake，上层 API/测试用例尽量不变。  
4. **记录契约**：每个三方在下表注明对应抽象；新引入必须先补接口或扩展现有接口，再接实现。  
5. **CMake**：`ENGINE_WITH_*` 开关；缺库时明确失败或跳过特性（不得把三方类型泄漏到公开头）。  
6. **版本/许可**：实现时填 §7 表。

### 1.2 抽象 ↔ 默认实现对照（摘要）

| 抽象（引擎） | 默认三方实现 | 可替换方向（例） |
|---|---|---|
| `IPhysicsWorld` | Jolt | PhysX |
| `IImmediateUi` / ImGui 适配 | Dear ImGui | 其它即时 UI（须等价调试能力） |
| `IRetainedUi` | RmlUi | 薄自研保留模式 |
| `IUpscaler` | **冻结** DLSS/FSR2 真 SDK | 仅 `builtin_bilinear`（ADR 0043） |
| `IShaderCompiler`（工具链） | DXC | 其它 HLSL→DXIL/SPIR-V 工具（须 ADR） |
| `IVideoDecoder` | D3D12VA / Vulkan Video | 同后端其它硬解（禁止软解降级） |
| `IHttpClient` | cpp-httplib | libcurl / IX HTTP 等 |
| `IWebSocket` | IXWebSocket | 其它 WS 库 |
| `IQuicEndpoint` | MsQuic | ngtcp2 等（须 ADR） |
| `IImageLoader` | stb_image | 其它解码器 |
| `ITextureLoader` | dds-ktx (+ stb 回退 PNG) | DirectXTex / 其它 DDS |
| `IGltfImporter` | cgltf/fastgltf | tinygltf 等 |
| `IAudioDecoder` / `IAudioOutput` | miniaudio 等 | 拆分解码+WASAPI |
| `ISpinePlayer`（或 `ISkinnedSprite`） | spine-cpp 或等价 | DragonBones 等（须抽象） |
| 显存分配器适配 | D3D12MA / VMA | 自研简易分配 |

平台 SDK（Vulkan Loader、Win32、X11）与图形 **RHI 后端** 本身即抽象边界；仍禁止业务直调 `d3d12.h` / `vulkan.h`（走 RHI）。

---

## 2. 必选 / 强烈推荐引入

| 子系统 | 推荐库 | 用途 | 备注 |
|---|---|---|---|
| **物理** | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) | 刚体、查询、角色控制器；薄 SoftBody 见 C22 | PhysX 可替代；见 ADR 0015 / **0029** |
| **调试 UI** | [Dear ImGui](https://github.com/ocornut/imgui) | 控制台、Profiler、工具面板 | D3D12 + Vulkan backend |
| **超分（NVIDIA）** | NVIDIA **DLSS / Streamline（NGX）** | `IUpscaler`（**产品冻结**，ADR 0043） | 许可与再分发单独合规；现网仅 bilinear |
| **着色器编译** | **DirectXShaderCompiler (DXC)** | HLSL → DXIL；HLSL → SPIR-V（Vulkan） | Windows SDK / NuGet / 官方包；构建时调用 |
| **Vulkan** | **Vulkan SDK**（LunarG） | Device/Swapchain/校验层 | M17+；Linux 必选 |
| **视频硬解** | **D3D12VA**（Win+D3D12）；**Vulkan Video**（Win/Linux+Vulkan） | 与当前渲染 Device 绑定 | 跟随 RHI；**无软解、不跨 API** |
| **HTTP(S)** | [cpp-httplib](https://github.com/yhirose/cpp-httplib) | HTTP 客户端（可兼调试服务端） | header-only；**HTTPS**：CMake `find_package(OpenSSL)` 成功且 `ENGINE_WITH_OPENSSL=ON` 时启用；未找到则 `Unavailable`（见 `http_httplib.cpp`）；**引擎不安装 OpenSSL**（可设 `OPENSSL_ROOT_DIR`）；见 ADR 0021 / 0031 |
| **WebSocket** | [IXWebSocket](https://github.com/machinezone/IXWebSocket) | WS / WSS 客户端（可服务端） | 轻量、跨平台、少依赖 |
| **QUIC** | [MsQuic](https://github.com/microsoft/msquic) | IETF QUIC **可靠流** | **产品冻结**（ADR 0043）；现网 Probe/Unavailable |

---

## 3. 推荐引入（默认采用）

| 子系统 | 推荐库 | 用途 | 备选 |
|---|---|---|---|
| **运行时 / HUD UI** | [RmlUi](https://github.com/mikke89/RmlUi) | 保留模式菜单/HUD | 薄自研保留模式（成本更高） |
| **网格导入** | [cgltf](https://github.com/jkuhlmann/cgltf) 或 [fastgltf](https://github.com/spnda/fastgltf) | glTF 2.0 静态+蒙皮 | tinygltf |
| **图像解码** | [stb_image](https://github.com/nothings/stb) | PNG/JPEG | [dds-ktx](https://github.com/septag/dds-ktx) 等补 DDS/KTX |
| **超分 fallback** | **FidelityFX FSR**（**产品冻结**） | 无 DLSS 时超分 | 现网 **仅** 引擎内置双线性 |
| **JSON / 配置** | [nlohmann/json](https://github.com/nlohmann/json) 或 [simdjson](https://github.com/simdjson/simdjson) | 配置、序列化辅助 | 自研极简解析（不推荐） |
| **字体光栅** | ImGui 内置 + [stb_truetype](https://github.com/nothings/stb) 或 FreeType | 调试 UI / 保留 UI 字形 | RmlUi 自带字体路径 |

---

## 4. 可选引入（可加速，非唯一方案）

| 子系统 | 可选库 | 用途 | 说明 |
|---|---|---|---|
| **数学** | 自研 `floatN/mat` **优先**；可选 [GLM](https://github.com/g-truc/glm) | 向量矩阵 | 教学与 ABI 稳定更倾向自研头文件 |
| **音频解码** | [miniaudio](https://github.com/mackron/miniaudio)（解码+输出一体）或 **WASAPI 自研输出** + [dr_mp3](https://github.com/mackron/dr_libs)/stb 类解码 | 薄音频层 | **明确不做特效**；与视频时钟对齐自研 |
| **音频仅解码** | dr_wav / dr_mp3 / [librosa 不适合运行时] | 文件 → PCM | 容器音轨可与 demux 共用 |
| **媒体 Demux** | [h264bitstream] 等过窄；可选轻量 MP4 demux 自研或 **MF（Media Foundation）Source Reader** 仅拆轨 | 视频/音轨分离 | **解码帧仍走当前后端硬解**；MF 限 demux，不作软解降级 |
| **压缩纹理** | [DirectXTex](https://github.com/microsoft/DirectXTex) | BC 压缩、mip、DDS | 工具链友好 |
| **网格处理** | [meshoptimizer](https://github.com/zeux/meshoptimizer) | 顶点缓存优化、简化 LOD 生成 | LOD 运行时切换自研；离线简化可用 |
| **哈希 / 字符串** | [xxHash](https://github.com/Cyan4973/xxHash)、[fmt](https://github.com/fmtlib/fmt) | AssetId、日志格式 | C++20 `std::format` 可替代 fmt |
| **Tilemap / 2D 资源** | 自研 Tiled JSON 导入或轻量解析 | Tilemap 渲染 | 地图用 Tiled 编辑，引擎只负责渲染导入 |
| **2D 骨骼（M21）** | [spine-cpp](https://github.com/EsotericSoftware/spine-runtimes) 等 | 骨骼精灵播放 | **强制抽象**；商业许可自审；可换其它运行时 |
| **内存分配（D3D12）** | [D3D12MA](https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator) | 显存子分配 | 可先自研简易分配器，复杂后接入 |
| **内存分配（Vulkan）** | [VulkanMemoryAllocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | 显存子分配 | M17+ 可选 |
| **螺旋/噪声等** | stb 等 | 程序化内容 | Sample 用 |
| **手柄进阶** | GameInput（系统） | 现代手柄 API | 与 XInput 二选一或适配器并存 |

---

## 5. 明确自研（不宜用三方替代核心）

| 模块 | 原因 |
|---|---|
| **RHI 抽象与 D3D12 / Vulkan 后端主体** | 引擎核心差异化；三方 RHI（bgfx/Sokol）会抢架构主权 |
| **FrameGraph** | 与资源/屏障/质量档深度绑定 |
| **RenderSystem / 可见性 / 实例提交** | 与场景模型一体 |
| **Material / 变体 / PSO 缓存策略** | 教学与扩展点 |
| **后处理算法实现**（TAA/SSAO/SSR 等） | 可参考论文/开源实现思路，代码纳入引擎；不整包依赖“后处理中间件” |
| **外设 ActionMap 与 DeviceHub** | 薄层自研利于教学；不强制 SDL/GLFW（窗口也可自研 Win32） |
| **Application / ModuleSystem** | 引擎主循环主权 |
| **Net 对外 API 与 Pump 模型** | 可换三方实现，但接口与线程约定自研 |

窗口：**Win32 自研**（Windows）；**X11 自研**（Linux，Wayland 目标内）。跨平台可后评估 SDL3/GLFW（仅平台层）。

---

## 6. 按里程碑的引入时机

| 里程碑 | 可引入的三方 |
|---|---|
| M1–M2 | DXC（着色器，**M2 强制**）、（可选）stb_image、fmt；**Catch2**（FetchContent 推荐；**M1 暂用** `tests/unit/mini_test.h`）；shader_compile 工具 |
| M3–M5 | cgltf/fastgltf；**ibl_baker**；纹理压缩可用 **DirectXTex**；asset 清单约定 |
| M6–M8 | DLSS + FSR；音频（解码+输出）；**lightmap_baker** |
| M8 / M15 | Dear ImGui；RmlUi（M15） |
| M9 | 黄金图脚本；**asset_cook 最小落地** |
| M10 | meshoptimizer（离线 LOD，**可后置**） |
| M12 | **Jolt** |
| M16 | Tiled 导入；图集由外部工具生成（引擎定契约） |
| M17 | Vulkan SDK、DXC SPIR-V、ImGui Vulkan、**Vulkan Video**、（可选）VMA |
| M18 | 同上 + Linux 构建依赖（X11/Wayland、校验层） |
| M19 | **cpp-httplib**、**IXWebSocket**、**MsQuic**（及 TLS 依赖） |
| M21 | 2D 骨骼运行时（如 spine-cpp，**经抽象**；注意许可） |
| M2+ 可选 | D3D12MA（描述符/堆压力上来后） |

---

## 7. 许可与合规检查表（实现时填写）

| 库 | 版本 | 许可证 | 动态/静态 | 再分发注意 |
|---|---|---|---|---|
| mini_test（自研） | M1 | 本仓库 | 头文件 | 临时单测 harness；计划替换为 Catch2 |
| Jolt | **v5.6.0**（`third_party/JoltPhysics`） | MIT | 静态 | 经 `IPhysicsWorld`/`jolt_world`；Sample 不直链；缺源码则 `ENGINE_WITH_JOLT=OFF` |
| Dear ImGui | **v1.91.8**（`third_party/imgui-v1.91.8`） | MIT | 静态 | 经 `ImmediateUi` 封装；Sample 不直链 |
| RmlUi | **6.0**（`third_party/RmlUi`） | MIT | 源码 vendor；本档薄适配 | 经 `CreateRetainedUiBackend`；`ENGINE_WITH_RMLUI`；Sample 不直链 |
| cgltf / fastgltf | TBD | TBD | TBD | |
| stb / dr_libs | **stb_image**（`third_party/stb/stb_image.h`） | 公有域/MIT | 头文件 | 经 `IImageLoader`；Sample 不直链 |
| FSR | TBD | AMD 许可 | TBD | 遵守 NOTICE |
| DLSS / Streamline | TBD | NVIDIA 协议 | 通常动态 | **不可随意再分发**；随安装包条款 |
| DXC | TBD | 随工具链 | 构建机 | 运行时不一定需要 |
| D3D12MA | TBD | MIT | TBD | |
| miniaudio | TBD | 可选许可 | 单文件 | |
| cpp-httplib | header vendored (`third_party/cpp-httplib`) | MIT | 头文件 | 经 `IHttpClient`/`http_httplib`；HTTPS：系统 OpenSSL 已可发现时 `ENGINE_WITH_OPENSSL` 链入并定义 `CPPHTTPLIB_OPENSSL_SUPPORT`；否则清晰 Unavailable（**不安装 OpenSSL**）；Win 链 `ws2_32`/`crypt32` |
| dds-ktx | ~v1.1+ (`third_party/dds-ktx`) | BSD-2-Clause | 头文件 | 经 `ITextureLoader`；Sample 不直链 |
| IXWebSocket | TBD | BSD | 静态/动态 | |
| MsQuic | TBD | MIT | 通常动态 | **本波未捆绑**（ADR 0031 QUIC SKIP）；启用须另批 |

### 7.1 内容素材（非代码库）

| 素材 | 来源 | 许可 | 仓库位置 | 备注 |
|---|---|---|---|---|
| Kloppenheim 06 Pure Sky（1K HDR） | [Poly Haven](https://polyhaven.com/a/kloppenheim_06_puresky) | **CC0** | `content/ibl/src/`（源）→ `ibl_pack.ibl1` / `sky_kloppenheim06.sky1`（烘焙产物） | 经 `ibl_baker`；见 `content/ibl/README.md` |

---

## 8. 相关文档

- [STANDARDS.md](STANDARDS.md) — 工程规范（含三方抽象纪律）  
- [TOOLING.md](TOOLING.md) — 工具链（DXC / DirectXTex 等构建机依赖）  
- [PLAN.md](PLAN.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [POSITIONING.md](POSITIONING.md)  
- [TESTING.md](TESTING.md) — 单测 / 集成 / 自动化  
- [learn/adr/0015-physics-third-party.md](learn/adr/0015-physics-third-party.md)  
- [learn/adr/0029-physics-softbody-boundary.md](learn/adr/0029-physics-softbody-boundary.md)  
- [learn/adr/0016-ui-imgui-retained.md](learn/adr/0016-ui-imgui-retained.md)  
- [learn/adr/0012-video-decode-follows-backend.md](learn/adr/0012-video-decode-follows-backend.md)  
- [learn/adr/0021-network-http-ws-quic.md](learn/adr/0021-network-http-ws-quic.md)  
- [learn/adr/0025-toolchain-minimum-viable.md](learn/adr/0025-toolchain-minimum-viable.md)  
