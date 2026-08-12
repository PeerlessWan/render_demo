# 运行时基础：资源 · 线程 · 寿命 · Profiling

> 把「通用引擎能跑、能扩、能调」的地基写成契约。  
> 对齐：[PLAN.md](PLAN.md)、[ARCHITECTURE.md](ARCHITECTURE.md)、[TOOLING.md](TOOLING.md)、[STANDARDS.md](STANDARDS.md)、[DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)。  
> 决策：[ADR 0026](learn/adr/0026-runtime-foundations-assets-threads-profiling.md)。

## 1. 总表（必做）

| 能力 | 最低要求 | 里程碑 |
|---|---|---|
| **资源管线（Cook / 加载）** | 清单 + **依赖图** + 可选打包；运行时按 AssetId 解析依赖并加载 | 约定 **M3**；cook/打包落地 **M9**；流式加深 **M10** |
| **异步加载回调** | 请求→后台→**主线程 Pump 后**完成/失败回调；可取消 | **M3** |
| **逻辑 / 渲染分离** | Scene 权威树不在渲染侧写；经 `RenderScene` SoA 抽取；M14 起并行命令录制 | 抽取 **M4+**；并行录制 **M14** |
| **资源寿命 / 引用** | `AssetHandle` 引用计数 + GPU **Fence 世代**延迟销毁；禁悬空 | **M2–M3** 骨架；M3 与异步一并验收 |
| **数据依赖关系** | Cook 依赖图 + 运行时解析；FrameGraph/Module 拓扑；Scene→Handle 引用边 | **M3** 约定；**M4/M9** 加固 |
| **生命周期管理** | Application→Device→资产/场景→逐帧临时资源；创建/销毁序与所有者表 | **M1–M3** 骨架；Resize/流式 **M2/M10** |
| **GPU Profiling** | Pass 时间戳 + PIX/RenderDoc 标记；ImGui/`stat` 面板 | 计数 **M8**；Pass GPU 耗时 **M8–M9** 可用 |

---

## 2. 资源管线（Cook / 加载 / 依赖）

### 2.1 目标

支持 **资产打包** 与 **依赖管理**，使发版与流式可复现（不只「散文件能读」）。

### 2.2 产物

| 产物 | 说明 |
|---|---|
| **AssetId** | 稳定逻辑名（与路径映射可版本化） |
| **Manifest（清单）** | 资产表：类型、路径/包内偏移、内容哈希、**依赖列表** |
| **Dependency graph** | `A → {B, C}`；加载 A 必须能解析并（按策略）拉齐依赖 |
| **Package（可选）** | 单文件/多卷包；开发期可散文件，发版走 cook 包 |
| **Import 缓存** | glTF/纹理等中间结果可进 cook（细节实现定） |

### 2.3 职责划分

| `tools/asset_cook` | `engine/assets` |
|---|---|
| 扫源、建依赖、压缩纹理、写清单/包 | VFS、按 Id 加载、缓存、流式、Pump |
| 可依赖 DirectXTex 等构建机工具 | 业务只拿 Handle / 引擎类型 |

细则目录见 [TOOLING.md](TOOLING.md)。**依赖边**至少覆盖：网格→材质→纹理/着色器变体键；场景清单→网格/Prefab 引用（渲染向）。

### 2.4 验收

- 同一 Manifest 下 Sandbox 与黄金图可复现。  
- 故意缺依赖 → **失败可诊断**（非黑屏假成功）。  
- 打包与散文件两种根路径可切换（配置）。

---

## 3. 异步加载回调

### 3.1 模型

```text
Request(AssetId, priority?) → LoadTicket / Handle(pending)
  后台：读盘 / 解码 /（可选）依赖队列
主线程 Asset.PumpAsync()：
  收割完成项 → 注册缓存 → 调用完成回调 / 发 Event
  失败 → 错误码 + 失败回调 + 可选占位资源
```

### 3.2 契约（强制）

1. **完成与失败回调只在主线程、Pump 之后**可见；禁止在 IO 线程直接碰 Scene / RHI 创建（上传经约定线程+上传环）。  
2. 回调内：**禁止重活**、禁止再入死锁；可入队下一帧工作。  
3. 支持 **取消**（未完成则不回调成功；已完成则靠引用释放）。  
4. 同步加载仅为调试/教学（`learn.disable_async_load`），产品默认异步。  
5. STANDARDS：异步完成可用 **回调** 或 **Event**；与 [STANDARDS §3](STANDARDS.md) 通讯优先级一致。

### 3.3 验收（M3）

- 单测/集成：请求→Pump→成功回调；失败路径；取消后无成功回调。  
- 教学 Sample `20_engine_ops` 可演示。

---

## 4. 逻辑 / 渲染分离（性能基础）

### 4.1 原则

| 侧 | 职责 | 禁止 |
|---|---|---|
| **逻辑 / 主更新** | Node 树、动画、物理同步、玩法 Module、输入 | 在热路径直接录制后端命令（应走 RenderSystem） |
| **渲染提交** | 读 `RenderScene`（SoA）、FrameGraph、RHI | **写** Scene 权威树；持有悬空 Node 指针跨帧 |

抽取约定见 ADR 0024 / STANDARDS §15：场景权威为树，渲染热路径为 SoA。

### 4.2 落地阶段

| 阶段 | 内容 |
|---|---|
| **M4+** | 每帧（或逻辑帧末）**提取** `RenderScene`；渲染只读快照 |
| **M3** | 异步 IO **工作线程池**；与主线程分离 |
| **M14** | **多线程命令录制**（多 CommandList / 多线程录制 + 主线程 Execute/Submit） |
| **可选加深** | 独立 Render Thread + 双缓冲 `RenderScene`（不挡 M14 验收；可作为后续打磨） |

「多线程渲染分离」在本引擎的 **验收含义** = **SoA 抽取隔离写权威** + **M14 并行录制** + **资源 IO 不上主线程**；不强制第一天就上独立渲染线程。

### 4.3 验收

- 关闭并行录制仍正确；打开后帧结果稳定（无花屏竞态）。  
- 压力下主线程 Update 与录制可重叠收益可测（Profiler）。

---

## 5. 资源生命周期 / 引用计数

### 5.1 两层寿命

| 层 | 机制 | 目的 |
|---|---|---|
| **CPU 资产** | `AssetHandle` / 侵入或非侵入 **引用计数**；缓存弱引用可淘汰 | 防泄漏、防业务悬空 |
| **GPU 资源** | 销毁请求进入 **退役队列**，待 **Fence 世代**（多帧 in-flight）完成后释放 | 防 GPU 仍在用时释放（ADR 0006） |

### 5.2 契约

1. 业务不持有裸 `ID3D12Resource*` / `VkImage`；只持引擎 Handle。  
2. `Release` 后禁止使用；Debug 可毒化/校验。  
3. 流式淘汰只踢 **引用为 0**（或弱缓存）的条目；有引用则保留或提升优先级。  
4. 单测：引用归零销毁；in-flight 未完成不得真正释放 GPU 对象。

### 5.3 验收

- M3：Handle + 异步加载与释放无 UAF（Debug 层可抓）。  
- M10：预算淘汰与引用协同（有引用不被静默踢掉导致悬空）。

---

## 6. 数据依赖关系（全层）

> 「依赖」不只 cook 文件边，还包括：**谁必须先存在、谁引用谁、谁可以先销毁**。

### 6.1 分层

| 层 | 依赖是什么 | 谁维护 | 里程碑 |
|---|---|---|---|
| **A. 资产 Cook 图** | AssetId → 依赖 AssetId 列表（网格→材质→纹理…） | `asset_cook` 写 Manifest；运行时只读 | M3 约定 / M9 落地 |
| **B. 运行时加载序** | 加载 A 前按拓扑拉齐（或并行拉齐后屏障）依赖 | `AssetManager` | M3 |
| **C. 场景引用边** | Node / 组件持有 `AssetHandle`（非裸路径字符串常驻） | Scene / 序列化 | M4 / M8 |
| **D. FrameGraph** | Pass 读写资源；Compile 推导屏障与执行序 | FrameGraph | M3 |
| **E. Module 拓扑** | Module 声明所需服务；Init/Shutdown 逆序 | `ModuleSystem` | M1 |
| **F. 子系统服务** | 如 Render 依赖 RHI Device 已创建 | Application 启动序 | M1–M2 |

玩法对象图（任务→NPC）**不在引擎维护**；引擎只保证 C 层渲染向引用合法。

### 6.2 资产依赖规则（A/B）

1. Manifest 每条记录含 `deps: AssetId[]`（可空）；**禁止环**（cook 时检测失败）。  
2. 运行时 `Load(A)`：计算闭包；策略默认 **依赖优先、可并行 IO、主线程按序提交 GPU 创建**。  
3. 软依赖（可选贴图）：缺失 → 警告 + Fallback，不阻断主资产（须在 Manifest 标注 `optional`）。  
4. 硬依赖缺失 → 失败回调 / Feature 级错误，可诊断。  
5. 卸载：仅当 Handle 引用归零 **且** 无其它资产硬依赖指向它（或依赖方已卸）——实现可用引用计数覆盖「被依赖」边（加载成功时对 deps `AddRef`）。

### 6.3 场景与 FG（C/D）

1. 场景序列化存 **AssetId / Handle 逻辑名**，不存绝对磁盘路径。  
2. 实例化时解析依赖；未就绪可占位网格/洋红材质。  
3. FrameGraph：Pass 不得使用未声明的外部资源；瞬时 RT 寿命 = 帧内（或 FG 延长别名规则，实现时文档化）。

### 6.4 Module / 启动（E/F）

1. Module `Requires` 声明 → 拓扑排序；环 = 启动失败。  
2. 销毁顺序 = Init 逆序。  
3. 不得在 Module 构造期假设 Device/Swapchain 已完整（放到 `OnInit`）。

### 6.5 验收

- cook：人造环依赖 → cook 失败。  
- 运行时：硬依赖缺失 → 失败可诊断；软依赖走 Fallback。  
- FG：错误依赖在 Compile 失败（测试覆盖）。  
- Module：错误 Requires 环 → 启动失败。

---

## 7. 生命周期管理（所有者与阶段）

### 7.1 对象谱系（谁创建 / 谁销毁）

| 对象 | 创建者 | 销毁时机 | 备注 |
|---|---|---|---|
| `Application` | 进程入口 | 退出 `Shutdown` | 拥有 Module / 服务 |
| `IBackend` / Device | Application/RHI 工厂 | Shutdown；Device Removed 特例 | 全局唯一（每后端） |
| Swapchain / 尺寸相关 RT | RHI / RenderSystem | **Resize** 或 Shutdown | 业务听 Resize 事件 |
| `IModule` | ModuleSystem | Shutdown 逆序 | 见 §6.4 |
| CPU 资产（网格/纹理元数据） | AssetManager | Handle 引用 **0** + 可淘汰 | §5 |
| GPU 资源（Buffer/Texture/PSO…） | RHI/后端 | 引用 0 后进 **Fence 退役队列** | ADR 0006 |
| FrameGraph 瞬时资源 | FG Compile/Execute | **当帧结束**（或别名规则） | 禁止缓存跨帧裸指针 |
| `RenderScene` 快照 | RenderSystem 抽取 | 下一逻辑帧覆盖或双缓冲翻页 | 渲染侧只读 |
| 上传环槽位 | 后端 | 对应 Fence 完成后复用 | CH05 |
| 调试 UI / Profiler 查询堆 | Debug | Shutdown；查询结果隔帧读回 | M8 |

### 7.2 阶段机（进程级）

```text
Starting
  → DeviceReady          # RHI 设备 OK
  → ModulesInited
  → Running              # 主循环
  → (Resizing)           # 可选子状态：停提交 → 重建 → 恢复
  → Stopping             # 取消异步、抽干队列
  → GPUDrained           # 等 in-flight Fence
  → ModulesShutdown
  → DeviceLost/Exit
```

约束：

1. **Stopping** 后不再接受新的异步加载成功副作用（可失败/取消）。  
2. **GPUDrained** 之前不得销毁 Device。  
3. Resize 期间禁止业务仍持有旧尺寸 RT 的 Handle（旧 Handle 应失效或延迟到重建后替换）。

### 7.3 帧级寿命

| 类别 | 寿命 |
|---|---|
| 输入状态 / 本帧 Event | 当帧 |
| `RenderScene` | 抽取 → 提交完成（或双缓冲的一页） |
| FG 瞬时 | 当帧 |
| in-flight 命令与上传 | N 帧（Fence 世代，典型 2–3） |
| 常驻资产 | 直到引用 0 且退役完成 |

### 7.4 Debug 义务

- 开发构建可统计：活 Handle 数、退役队列长度、异步在途数。  
- UAF / 二次 Release：断言或毒化（实现选型）。  
- `learn.force_sync_gpu`：缩短寿命窗口便于单步（见 learn/README）。

### 7.5 验收

- M1：启动/退出无泄漏噪声（基础）。  
- M2：Resize 反复无崩溃；Fence 寿命不花屏。  
- M3：异步取消 + Shutdown 抽干。  
- M10：流式压力下引用与退役仍正确。

---

## 8. GPU Profiling（调优必需）

### 8.1 引擎内（必做）

| 项 | 说明 | 时机 |
|---|---|---|
| **GPU 时间戳** | 按 Pass（Shadow/Opaque/Post/UI…） | M8–M9 |
| **CPU 区间** | Update / Cull / Submit / UI / Asset.Pump | M8 |
| **计数器** | Draw/Dispatch/PSO 切换/屏障/三角形约数 | M8 |
| **ImGui / `stat`** | 与 DEBUG 文档一致 | M8 |
| **事件标记** | PIX / RenderDoc 可读的 Pass 名 | M2 起尽量；M8 齐 |

外部工具：PIX（D3D12）、RenderDoc（两后端）— 见 [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)。

### 8.2 验收

- Sandbox 不依赖外部工具即可看到 **CPU 帧时 + 主要 Pass GPU 耗时**。  
- 开关重特性（阴影/AO/SSR）时 Profiler 数字有可感知变化。

---

## 9. 与里程碑挂钩（摘要）

| 里程碑 | 交付 |
|---|---|
| **M1** | Module 依赖拓扑；Application 启停序骨架 |
| **M2** | GPU Fence 寿命；Resize 生命周期；PIX 事件名开始 |
| **M3** | 异步 Pump 回调；Handle/引用；**资产依赖图约定**；FG 依赖 Compile；Shutdown 抽干 |
| **M4** | RenderScene 抽取；场景→Handle 引用边 |
| **M8–M9** | Profiler；cook 清单+依赖图+打包落地 |
| **M10** | 流式 + 预算 + 引用/依赖协同 |
| **M14** | 多线程命令录制 |

---

## 10. 相关文档

- [TOOLING.md](TOOLING.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md) §4 Assets / §5 / §7  
- [STANDARDS.md](STANDARDS.md) §2 / §3 / §7 / §15  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) §3  
- [learn/adr/0006-upload-ring-inflight.md](learn/adr/0006-upload-ring-inflight.md)  
- [learn/adr/0024-backend-feature-tiers-and-soa.md](learn/adr/0024-backend-feature-tiers-and-soa.md)  
- [learn/adr/0026-runtime-foundations-assets-threads-profiling.md](learn/adr/0026-runtime-foundations-assets-threads-profiling.md)  
