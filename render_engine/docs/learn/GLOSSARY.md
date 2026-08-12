# 术语表（学习向）

| 术语 | 含义（直觉） |
|---|---|
| RHI | Render Hardware Interface，把 D3D12/Vulkan 等差异挡在后端后的抽象层 |
| Vulkan | 跨平台图形 API；本引擎在 Windows 与 D3D12 并存，在 Linux 为唯一实装后端 |
| D3D12VA | Direct3D 12 Video Acceleration；**D3D12 后端**下的视频硬解路径 |
| Vulkan Video | Vulkan 视频解码扩展；**Vulkan 后端**下的视频硬解路径 |
| FrameGraph | 用「Pass 读写资源」描述帧，再编译出执行顺序与屏障 |
| Swapchain | 与窗口表面相连的一组可翻转图像，Present 把画面交给桌面 |
| Command List | GPU 命令的录制缓冲；录完再提交到 Queue |
| Queue | Graphics / Compute / Copy 等提交队列 |
| Fence | CPU↔GPU 同步原语；用于多帧 in-flight |
| in-flight | 已提交但 GPU 可能尚未完成的帧/资源，不能立刻释放或覆盖 |
| PSO | Pipeline State Object，着色器+固定功能状态的不可变组合 |
| Root Signature | DX12 的资源绑定布局约定（类 VK Pipeline Layout） |
| Descriptor | 描述「着色器怎么看见」缓冲/纹理/采样器的句柄 |
| Barrier | 资源状态转换与执行依赖（读↔写、SRV↔RT 等） |
| Manifest（清单） | 资产表：Id、类型、路径、哈希、**依赖列表**；见 RUNTIME_FOUNDATIONS |
| AssetHandle | 引用计数句柄；业务不持有后端裸指针 |
| Fence 退役 | GPU 资源延迟到 in-flight 完成后再销毁 |
| RenderScene | 从场景树抽取的渲染 SoA 快照（逻辑/渲染分离） |
| CBV / SRV / UAV / RTV / DSV | 常量/着色器资源/无序访问/渲染目标/深度模板视图 |
| Upload Ring | 环形暂存上传区，按帧推进，避免每帧新建 staging |
| PBR | 基于物理的着色；本引擎默认 Metallic-Roughness |
| IBL | Image Based Lighting，用环境贴图近似间接光 |
| Irradiance / Prefiltered / BRDF LUT | IBL 三件套：漫反射辐照、镜面预滤波、分割和近似 LUT |
| CSM | Cascaded Shadow Maps，方向光多级联阴影 |
| BLAS / TLAS | 底/顶层加速结构（光追） |
| SBT | Shader Binding Table，DXR 命中组调度表 |
| Jitter | 投影矩阵亚像素抖动，供 TAA/超分 |
| Motion Vectors | 屏幕运动向量，超分/TAA 常用 |
| Material Keyword | 编译期宏开关，生成不同 PSO 变体 |
| RenderScene | 一帧剔除与收集后的可提交列表 |
| Environment | 天空、雾、IBL、清除色等环境一体配置 |
| 三方抽象层 | 所有第三方库外的引擎 `I*`/adapters；保证可替换而不改业务 |
| 工程规范 | 见 `docs/STANDARDS.md`：编码、分层、模块通讯、错误/线程、**§15 双后端分级与 SoA** |
| 特性分级 L0/L1/L2 | 双后端对齐级别：必齐 / 可暂单端 / 可永久差（ADR 0024） |
| SoA 提取 | 从场景树每帧抽出连续数组供渲染；不等于全量 ECS |
| Upscaler | DLSS/FSR 等超分接口抽象 |
| Learn Layer | 本目录教学封装，不改变产品架构 |
| 外设接入层 / InputSystem | 外设枚举、热插拔、状态采样与 Action 映射，独立于渲染后端 |
| DeviceHub | 外设设备中心：列表、连接/断开事件 |
| ActionMap | 逻辑动作到键/按钮/轴的绑定表，可重绑 |
| IInputAdapter | 具体外设后端（Win32 键鼠、XInput/GameInput 等）的适配器接口 |
| VideoTexture | 由视频流驱动的动态纹理资产，可绑到材质 |
| NetSystem | 网络传输层：HTTP / WebSocket / QUIC 可靠流；主循环 Pump |
| QUIC 可靠流 | 基于 QUIC 的可靠、有序字节流（非不可靠游戏 UDP 包语义） |
| AudioSystem | 音频层：解码与设备输出渲染；不做 DSP 特效 |
| AudioClip / AudioSource | 音频资源与播控实例 |
| IPhysicsWorld | 物理世界封装（刚体/查询/角色等） |
| P0 / P1（通用渲染） | 通用渲染补强优先级：P0 刚需，P1 标配（见 PLAN M10–M14） |
| Shadow Atlas | 多局部光源阴影共用的图集/打包技术 |
| TAA | Temporal Anti-Aliasing |
| GTAO / SSAO | 屏幕空间环境光遮蔽类技术 |
| ImmediateUi / ImGui | 即时模式调试/工具 UI |
| RetainedUi | 保留模式运行时 UI（HUD/菜单） |
| UI Pass | FrameGraph 中在 3D 之后绘制界面的 Pass |
| Sprite / Billboard | 2D 精灵与朝向相机的公告板 |
| SpriteAtlas | 图集与帧动画裁剪源 |
| Tilemap | 瓦片地图渲染（引擎负责绘制/导入，不做 RPG 逻辑） |
| Y-sort / SortLayer | 2D 与混合场景的排序策略 |
| Pixel pipeline | Nearest、整数缩放、可选像素网格对齐 |
| LAYERS | 工作区分层权威文档：`docs/LAYERS.md` |
| game_kit | 品类无关玩法壳 + 脚本 VM（挂在引擎之上） |
| genre_kits | 可选品类玩法层（如 rpg_kit / shooter_kit）；依赖 game_kit |
| games/<title> | 具体游戏工程：内容与数值；可选用 0..N 个 genre kit |
| Host API | 引擎向宿主暴露的公开 API 面（见 HOST_API.md） |

更细的 API 名以代码与 [ARCHITECTURE.md](../ARCHITECTURE.md) 为准。  
分层见 [../../docs/LAYERS.md](../../docs/LAYERS.md)、ADR 0028。
