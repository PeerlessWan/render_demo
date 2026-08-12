# 学习路径大纲

> 必修：把「RHI 上跑通可维护的渲染引擎骨架」学透（默认 Windows **D3D12**；M17+ 可对照 **Vulkan**）  
> 选修：完整产品能力（含 M17 Vulkan / M18 Linux，与 [PLAN.md](../PLAN.md) 对齐）

建议节奏：每章 1–3 天（视基础而定）。章号与 `samples/learn/NN_*` 对齐。

## 总览

```text
基础数学/术语 ──► 清屏/三角/纹理 ──► 光照与 CB ──► 深度与多 Pass
        ──► RHI 抽象动机 ──► 场景提交 ──► PBR/IBL ──► 阴影
        ──► FrameGraph ──► （选修）蒙皮/特效/后处理/超分/DXR
        ──► Sandbox 总复习
```

## 必修章

| 章 | 主题 | 对齐产品 | Sample | 你应能回答 |
|---|---|---|---|---|
| CH00 | 仓库地图、如何编译运行、教学开关 | M1 | — | 主循环从哪进？模块怎么挂？ |
| CH01 | 设备、队列、交换链、清屏 | M1–M2 | `01_clear` | 一帧谁 Present？多缓冲为何存在？ |
| CH02 | 流水线、根签名、PSO、画三角 | M2 | `02_triangle` | VS/PS 如何连到输入布局？ |
| CH03 | 顶点缓冲、纹理、采样器、深度 | M2 | `03_texture_depth` | sRGB 与线性谁转换？深度测试何时开？ |
| CH04 | 常量缓冲、变换矩阵、基础光照 | M2–M4 | `04_lighting_cbv` | CB 对齐规则？CPU 每帧怎么更新？ |
| CH05 | 上传环 vs 临时 Map、多帧 in-flight | M2–M3 | `05_upload_ring` | 为何不能释放 in-flight 资源？ |
| CH06 | 为何需要 RHI：把后端藏起来 | M1–M3 | `06_rhi_triangle` | 业务怎样做到不直调 d3d12/vulkan 头？ |
| CH07 | 场景节点、Camera、Draw 收集 | M4 | `07_scene_camera` | RenderScene 里有什么？谁负责剔除？ |
| CH07b | 外设接入：DeviceHub、ActionMap、手柄 | M4 | `07b_input_actions` | 为何业务应绑 Action 而非 VK 码？ |
| CH08 | 材质实例与变体（Keyword→PSO） | M5–M6 | `08_material_variants` | 改 Keyword 为何可能卡顿？ |
| CH09 | PBR + IBL（完整，含 baker 使用） | M5 | `09_pbr_ibl` | BRDF LUT / prefilter 各干什么？ |
| CH10 | 方向光阴影（先单 Cascade） | M5 | `10_shadow_map` | 阴影 acne 与 bias？ |
| CH11 | FrameGraph：声明式 Pass 与屏障 | M3–M5 | `11_frame_graph` | 手动屏障 vs FG 插入点？ |

**必修结束标准：** 能独立加一个全屏 Post Pass，并讲清资源依赖与屏障。

## 选修章（产品完整度）

| 章 | 主题 | 对齐产品 | Sample | 备注 |
|---|---|---|---|---|
| CH12 | CSM 多级联与级联染色调试 | M5 | `12_csm` | 先学 CH10 |
| CH13 | Environment 雾与质量档 | M5–M6 | `13_environment_quality` | |
| CH14 | 蒙皮与 skinned glTF | M6 | `14_skinning` | |
| CH15 | 反射探针与简化 Lightmap | M6/M8 | `15_probes_gi` | |
| CH16 | 后处理栈（Bloom/Tonemap/FXAA…） | M6–M7 | `16_post_stack` | |
| CH17 | CPU/GPU 粒子、Trail、Decal | M7 | `17_vfx` | |
| CH18 | Motion Vectors、Jitter、超分（DLSS/FSR） | M7 | `18_upscale` | 厂商 API 为黑盒，重在接入契约 |
| CH18b | 视频纹理：随后端硬解（无软解） | M7 / M17 | `18b_video_texture` | 为何必须与渲染共享 Device？D3D12VA vs Vulkan Video？ |
| CH18c | 音频：解码 + 输出渲染（无特效） | M7 | `18c_audio_playback` | Clip/Source/Output 分工？为何不做效果器？ |
| CH19 | DXR 入门：AS + 一条示范与降级 | M8 | `19_dxr_intro` | 无 DXR 硬件须跑通降级 |
| CH20 | 异步加载、序列化、控制台、**引用寿命** | M3/M8 | `20_engine_ops` | Pump 回调？Handle 与 Fence？ |
| CH20b | 调试·调优·排错方法实践 | M4/M8/M9 | 对照主文档操作 | 见 DEBUG_TUNE_TROUBLESHOOT |
| CH21 | Sandbox 基础段总复习 | M9 | `Sandbox` | §1.1 验收 |
| CH22 | LOD、实例化、流式与内存预算 | M10 | `22_lod_instancing_streaming` | P0 |
| CH23 | 遮挡剔除 | M10 | `23_occlusion_culling` | P0 |
| CH24 | 点/聚光阴影 Atlas、TAA、SSAO/GTAO、透明策略 | M11 | `24_local_shadows_taa_ao` | P0 |
| CH25 | 物理：刚体、查询、角色控制器 | M12 | `25_physics` | Jolt 封装 |
| CH26 | SSR、DoF、运动模糊、曝光、体积雾、动态反射 | M13 | `26_p1_post_reflect` | P1 |
| CH27 | Morph、间接绘制、Bindless、多线程录制 | M14 | `27_gpu_submit_mt` | P1 |
| CH28 | HDR 输出与色彩管理 | M14 | `28_hdr_color_sandbox` | P1 |
| CH29 | UI：ImGui + 保留模式 HUD/菜单、输入捕获 | M8/M15 | `29_ui` | 与 ActionMap 如何分工？ |
| CH30 | 2D/像素/混合：Sprite、Tilemap、排序与像素管线 | M16 | `30_pixel_hybrid` | 为何 Nearest+整数缩放？Y-sort 何时失效？ |
| CH31 | 网络：HTTP / WS / QUIC 可靠流与 `Net.Pump` | M19 | `31_net_loopback` | 为何回调进主循环？与图形后端无关？ |
| CH32 | Vulkan 对照：同一 RHI Sample 切后端 | M17 | 复跑 `01`/`02`/`06` | Feature / L0 差在哪？ |
| CH33 | Linux + Vulkan：窗口与构建 | M18 | Linux 清屏 | X11 必做点？ |
| CH34 | 混合打磨与 2D 深度（拣选/MV/分层） | M20–M21 | `34_hybrid_2d_depth` | 与 CH30 增量是什么？ |
| CH35 | 动态 GI / 地形水体植被 / GPU Driven / VK RT | M22–M25 | 分 Sample 或 Sandbox 开关 | 各能力属于 L几？ |

## 每章固定结构（章节文模板）

后续 `chapters/CHXX_title.md` 统一包含：

1. **目标**（学完能做什么）  
2. **前提**（依赖章）  
3. **原理**（1–2 屏，不写说明书）  
4. **代码地图**（关键文件与函数）  
5. **建议断点 / PIX 看什么**  
6. **练习**（必做 1–2 + 选做 1）  
7. **常见坑**  
8. **延伸阅读**（官方文档链接）  

## 与 PLAN 里程碑映射

| 产品里程碑 | 建议同步推进的学习章 |
|---|---|
| M1 | CH00–CH01 |
| M2 | CH02–CH05 |
| M3 | CH05–CH06、CH11 初版 |
| M4 | CH07、CH07b |
| M5 | CH08–CH11、CH12–CH13（选修可并行文档） |
| M6 | CH14–CH16 |
| M7 | CH17–CH18、CH18b、CH18c |
| M8 | CH19–CH20 |
| M9 | CH21 |
| M10 | CH22–CH23 |
| M11 | CH24 |
| M12 | CH25 |
| M13 | CH26 |
| M14 | CH27–CH28 |
| M15 | CH29 |
| M16 | CH30 |
| M17 | CH32（Vulkan 对照） |
| M18 | CH33（Linux） |
| M19 | CH31（网络） |
| M20–M21 | CH34 |
| M22–M25 | CH35 |

原则：**产品代码可以一次实现完整能力；学习 Sample 按章裁剪场景，避免第一章就打开 CSM+DLSS+DXR。** 默认学习路径仍以 **D3D12** 为主；双后端差异见 [ADR 0020](adr/0020-windows-d3d12-vulkan-linux-vulkan.md)；网络见 [ADR 0021](adr/0021-network-http-ws-quic.md)。编译运行见 [GETTING_STARTED_M1.md](../GETTING_STARTED_M1.md)。

## 练习难度约定

| 标记 | 含义 |
|---|---|
| ★ | 改参数/改颜色即可 |
| ★★ | 仿照现有 Pass 加一个小功能 |
| ★★★ | 需理解同步或资源生命周期 |

## 相关文档

- [README.md](README.md)  
- [SAMPLES.md](SAMPLES.md)  
- [ADR_INDEX.md](ADR_INDEX.md)  
