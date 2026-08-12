# 调试 · 调优 · 排错方法

> 产品与学习共用。工具抓帧细节见 [learn/DEBUG_WORKFLOW.md](learn/DEBUG_WORKFLOW.md)。  
> 引擎能力随 [PLAN.md](PLAN.md) 落地；本文规定 **方法与约定**，实现阶段按此补齐设施。

## 1. 总原则

| 原则 | 说明 |
|---|---|
| 先引擎内，后外部工具 | Debug 视图 / 控制台 / ImGui → 再 PIX / RenderDoc |
| 一次只改一个变量 | 调优与排错都避免同时开关多项 |
| 可复现优先 | 固定种子、固定相机、固定资产路径、关闭异步不确定性（`learn.disable_async_load`） |
| 失败要可诊断 | 视频硬解（D3D12VA/Vulkan Video）、DXR、设备丢失等必须有错误码+日志，禁止黑屏假成功 |
| 分层归因 | 先分清：资源/同步/着色/Pass 顺序/驱动/内容 |

推荐排查顺序：

```text
日志与错误码 → Debug 视图/Draw → 控制台关特性二分 → CPU/GPU Profiler
  → PIX/RenderDoc 抓帧 →（仍不明）最小复现 Sample
```

---

## 2. 调试方法（Debug）

### 2.1 引擎内设施（必做能力）

| 设施 | 用途 | 典型命令 / 入口 |
|---|---|---|
| 日志 + 错误码 | 初始化失败、资源丢失、VA/DXR 不支持 | `SetLogCallback`；级别 Error/Warn/Info |
| D3D12 Validation | API 误用、状态错误 | 配置 `r.debug_layer=1`（Debug 构建默认开） |
| Vulkan Validation | 同步/布局/描述符误用 | 校验层 + `VK_EXT_debug_utils`（Debug 默认开） |
| GPU 崩溃/Device Removed | 查 HRESULT/VkResult、Reason、面包屑 | 日志转储 + PIX（D3D12）/ RenderDoc（两后端） |
| Debug 视图模式 | 看 Albedo/Normal/Roughness/Metallic/AO/Cascade/Overdraw/RT | `r.viewmode=...` 或 ImGui |
| DebugDraw | 视锥、AABB、骨骼、光方向、级联、**碰撞体** | `r.debugdraw=1` |
| 控制台 | 运行时改 `r.*` / `learn.*` / 质量档 | `` ` `` 或 ImGui Console |
| ImGui 调试面板 | Profiler、Pass 列表、材质/实体点选 | M8+ |
| 对象命名 / PIX Event | 抓帧可读 | `SetName`、`BeginEvent` |
| Readback / 截图 | 黄金图、贴图内容确认 | `r.screenshot` |
| 教学开关 | 强制同步、强校验、显示 Pass 名 | `learn.*`（见 learn README） |

### 2.2 推荐调试流程

1. **复现**：记下配置（质量档、RT/DLSS/视频是否开）、GPU、驱动版本。  
2. **看日志**：启动到出错的第一条 Error。  
3. **切 Debug 视图**：判断是光照、法线、阴影还是透明问题。  
4. **关特性二分**：TAA→AO→SSR→阴影→后处理，定位哪一层引入。  
5. **抓帧**：PIX 看 Pass 顺序与耗时；RenderDoc 看 RT/输入装配。  
6. **缩小复现**：拷到对应 `samples/learn/NN_*` 或最小 Sandbox 场景。

### 2.3 着色器与材质

| 手段 | 说明 |
|---|---|
| 强制 Fallback / 粉红材质 | 确认是否绑定错误或变体缺失 |
| 逐 Keyword 关闭 | 定位哪个变体宏导致错误 PSO |
| 输出调试颜色 | 临时 PS 输出 world normal / UV / 深度可视化 |
| 在线/离线编译日志 | 保存 DXC 报错全文 |

### 2.4 物理 / UI / 媒体

| 域 | 调试要点 |
|---|---|
| 物理 | 画碰撞体；确认 Transform 权威侧；Raycast 层级过滤 |
| UI | `WantCapture` 是否吃掉输入；DPI/逻辑分辨率；UI Pass 是否在后处理之后 |
| 视频 | `Feature::VideoTexture*`（随后端）；Init 错误码；勿期望软解或跨 API VA |
| 音频 | 欠载/设备切换；与视频时钟是否对齐 |

---

## 3. 调优方法（Tune / Profile）

### 3.1 先定目标

| 目标类型 | 示例 |
|---|---|
| 帧时间 | 1080p 下 CPU/GPU 目标例如小于 16.6ms（可配置） |
| 内存 | 常驻纹理/网格预算（流式淘汰） |
| 画质 | 质量档 Low/Med/High 可感知差异且 High 可玩 |

无目标的「优化」不进计划验收。

### 3.2 引擎 Profiler（必做）

| 指标 | 说明 |
|---|---|
| CPU：主线程帧时 | Update / Cull / Submit / UI |
| GPU：Pass 耗时 | 时间戳查询；Shadow / Opaque / Post / UI |
| 计数 | Draw、Dispatch、管线切换、屏障次数、三角形约数 |
| 内存 | 上传环水位、描述符堆、流式预算占用 |
| 媒体 | 视频解码排队、音频欠载次数 |

ImGui Profiler 面板与控制台 `stat` 类命令同步（实现阶段命名锁定）。

契约与里程碑：[RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md) §6、ADR 0026。

### 3.3 调优顺序（性价比）

```text
1. 分辨率 / 质量档 / 超分（DLSS/FSR）
2. 阴影（级联数、局部光数量与 Atlas 分辨率）
3. 后处理（SSR/AO/Bloom/DoF）
4. 可见性（LOD、遮挡、实例化）
5. CPU 提交（多线程录制、合批、减少 PSO 切换）
6. 资源（流式、压缩格式、mip）
```

### 3.4 质量档与运行时旋钮

| 旋钮 | 调优用途 |
|---|---|
| `r.quality=low/med/high` | 一键裁剪 P0/P1 重特性 |
| `r.shadow.*` | 级联/Atlas/距离 |
| `r.aa=taa/fxaa/off` | AA 成本 |
| `r.upscale=dlss/fsr/off` | 算力换分辨率 |
| `r.ssr` / `r.ao` / `r.bloom` | 单特性开关做 A/B |
| `r.lod.bias` | 几何量 |
| `r.raytracing` | DXR 开关（无能力须降级） |

调优记录建议：保存「配置快照 + 平均帧时 + GPU 名」便于回归。

### 3.5 外部工具调优

| 工具 | 用途 |
|---|---|
| PIX | GPU 火焰图、队列空闲、资源屏障过多 |
| RenderDoc | 单 Draw 状态对比 |
| Windows GPUView（可选） | CPU/GPU 调度气泡 |
| 驱动厂商工具 | Nsight / Radeon/Intel GPA（按硬件） |

---

## 4. 排错方法（Troubleshoot）

### 4.1 症状 → 检查清单

| 症状 | 优先检查 |
|---|---|
| 黑屏 / 清屏色不变 | 交换链 Present、主相机、RT 绑定、VS 是否裁掉全部几何 |
| 粉红 / 洋红色 | Fallback 材质；纹理路径；VFS 根目录 |
| 闪烁 / 花屏 | 资源寿命（in-flight）、屏障缺失、多线程提交竞态 |
| 阴影痤疮 / 条纹 | Bias、级联混合、法线偏移、深度格式 |
| TAA 拖影 / 鬼影 | 运动向量、Jitter、历史缓冲、动态物体标记 |
| 透明错乱 | 队列排序、写入深度、OIT/加权是否启用 |
| Device Removed | Validation 日志、UAV 越界、超时、驱动；PIX 面包屑 |
| Resize 后异常 | Swapchain/依赖尺寸 RT 重建、相机 Aspect、UI DPI |
| 视频打不开 | 确认后端与硬解匹配（D3D12↔D3D12VA，VK↔Vulkan Video）、能力位、驱动/扩展、编码；无软解 |
| 网络失败 | 超时/TLS/对端关闭：看 `Net` 错误码与日志；先 loopback 集成测再外网 |
| 有画无声 / 爆音 | 音频设备、缓冲大小、欠载；与视频时钟 |
| 输入失灵 | UI WantCapture、窗口焦点、ActionMap 绑定 |
| 物理穿透 / 抖动 | 步长、碰撞体尺寸、Transform 双写冲突 |
| DLSS 不可用 | 能力查询；应自动 FSR/内置 fallback（与视频不同） |
| DXR 崩溃 | 关闭 `r.raytracing`；确认降级到 CSM |

### 4.2 二分法模板

1. `r.quality=low` 是否恢复？→ 是则逐项打开 Med/High 特性。  
2. 关后处理整栈是否恢复？→ 再逐 Pass 打开。  
3. 换最小网格/默认材质是否恢复？→ 内容问题 vs 引擎问题。  
4. 关异步加载是否恢复？→ 时序/寿命问题。  
5. 单线程提交（若已实现多线程）是否恢复？→ 竞态。

### 4.3 日志与报告模板（提 Bug 用）

```text
- 版本 / Commit：
- GPU / 驱动：
- 质量档 / 关键 r.*：
- 复现步骤：
- 期望 / 实际：
- 日志摘录（首条 Error）：
- 截图 / PIX 捕获（可选）：
```

---

## 5. 与里程碑的对应（实现清单）

| 阶段 | 调试/调优/排错相关交付 |
|---|---|
| M1–M2 | 日志、Validation、对象命名、基础截图 |
| M3–M4 | DebugDraw、Debug 视图雏形、教学开关 |
| M8 | 控制台、ImGui 调试雏形、Profiler 计数 |
| M9 | 基础段排错清单走查 |
| M10–M14 | Pass 耗时、流式预算统计、质量档覆盖 P0/P1 |
| M12 | 物理碰撞 DebugDraw |
| M15 | UI 捕获指示、HUD/调试层分层说明 |

验收时：Sandbox 须能 **不靠外部工具** 完成常见画质/性能问题的第一轮定位。

---

## 6. 相关文档

- [learn/DEBUG_WORKFLOW.md](learn/DEBUG_WORKFLOW.md) — PIX / RenderDoc 学习向抓帧  
- [ARCHITECTURE.md](ARCHITECTURE.md) — Debug / UI / Profiler 在架构中的位置  
- [PLAN.md](PLAN.md) — 里程碑与必做项  
- [learn/PATH.md](learn/PATH.md) — 章节练习中的「建议断点」  
