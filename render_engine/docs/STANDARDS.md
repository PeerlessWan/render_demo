# 工程规范

> 与 [ARCHITECTURE.md](ARCHITECTURE.md)、[PLAN.md](PLAN.md)、[THIRD_PARTY.md](THIRD_PARTY.md)、[TESTING.md](TESTING.md) 配套。  
> 目标：统一编码、架构边界、模块协作方式，降低耦合与换库成本。  
> 治理：冲突时 **ADR > 本规范 > 口头约定**；修订须更新本文并在 PR 说明。

## 0. 规范索引

| 章 | 主题 |
|---|---|
| §1 | 编码规范 |
| §2 | 架构规范 |
| §3 | 模块耦合与通讯规范 |
| §4 | 命名与目录 |
| §5 | 错误、日志与诊断 |
| §6 | 线程与同步 |
| §7 | 资源寿命与帧语义 |
| §8 | 配置、特性开关与质量档 |
| §9 | 第三方与抽象层 |
| §10 | 测试与质量门禁 |
| §11 | 文档与 ADR |
| §12 | 安全与隐私（网络/文件） |
| §13 | 构建、Git 与 PR |
| §14 | 性能与调试约定 |
| §15 | 双后端特性分级与场景数据布局（降维护/规模成本） |

---

## 1. 编码规范

### 1.1 语言与工具链

| 项 | 约定 |
|---|---|
| 语言 | **C++20**（无特殊理由不降到 C++17 以下） |
| 风格基线 | **[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**（格式以 clang-format 为准） |
| 编译器 | Windows：**MSVC**（与目标 SDK 对齐）；Linux：**GCC 或 Clang**（版本在 CMake 中写明下限） |
| 字符集 | 源文件与文档 **UTF-8**；禁止提交 UTF-16 源/文档 |
| 格式化 | 工作区根 [`.clang-format`](../../.clang-format)：`BasedOnStyle: Google`；PR 须 `clang-format` 一致 |
| 头/源扩展名 | 头文件一律 **`.h`**；实现 **`.cpp`**。**禁止 `.hpp` / `.cc` / `.cxx`**（新代码与存量均按此统一） |
| 静态检查 | 开启合理 Warning 为错误（逐步收紧）；禁止无理由关闭告警 |

### 1.2 风格要点

- **与 Google 对齐的要点**：2 空格缩进（由 clang-format 执行）、左对齐指针/`&`、短函数/控制流规则以 `.clang-format` 为准。  
- **RAII**：资源用智能指针 / 引擎句柄；禁止裸 `new`/`delete` 散落业务路径（adapters 内若必须，立即包进 RAII）。  
- **所有权**：函数参数用 `T*`/`T&` 表示非拥有；拥有用 `std::unique_ptr` / 引擎 `Handle`；共享所有权需书面理由。  
- **头文件**：扩展名 **`.h`**；公开头自洽、最小依赖；禁止在公开头拉三方或 `windows.h`/`vulkan.h`/`d3d12.h`。  
- **include 顺序**：对应头 → 工程公开头 → 其它工程头 → 标准库 → 三方（仅 `.cpp` / adapters）。`SortIncludes` 当前关闭，人工保持上述顺序。  
- **禁止**：在头文件中 `using namespace`；异常作为常规控制流（见 §5）；隐式窄化转换；**新增 `.hpp` 头文件**。  
- **const / [[nodiscard]]**：查询与易误用返回值加 `[[nodiscard]]`；能 const 则 const。  
- **宏**：优先 `constexpr` / 内联；宏仅用于平台/特性门控，名称带 `ENGINE_` 前缀。

### 1.3 注释与可读性

- 注释写 **意图与不变量**，不复述代码。  
- 公开 API 用简短中文或英文说明「能做什么 / 不能做什么 / 线程要求」。  
- 复杂算法、同步、屏障处允许较长注释；魔法数必须命名或注释单位。

### 1.4 禁止清单（编码）

| 禁止 | 原因 |
|---|---|
| 业务 `#include` 三方或图形原生头 | 破坏可替换与分层 |
| 静默吞掉错误（空 `catch`、忽略 `HRESULT`/`VkResult`） | 不可诊断 |
| 全局可变单例扩散（除明确的 `Application`/`Log` 入口） | 隐式耦合 |
| 在头文件定义非模板重逻辑 | 编译膨胀与循环依赖 |
| 使用 `.hpp`（或 `.cc`/`.cxx`） | 统一 Google 惯例：头 `.h`、源 `.cpp` |

---

## 2. 架构规范

### 2.1 分层与依赖方向

依赖只允许 **向下**（箭头指向被依赖方）：

```text
Sample / 产品 Module
    → Application / ModuleSystem
        → 子系统公开 API（Scene、Render、Net、Physics、UI、Media、Input…）
            → RHI / core
                → backends / platform / adapters（三方）
```

| 规则 | 说明 |
|---|---|
| 单向依赖 | 下层 **不得** include 上层（如 RHI 不知 Scene；Physics 不知玩法 Module） |
| 公开 API 面 | 业务只依赖 `include/engine/...`（或等价公开目录） |
| 后端隔离 | D3D12/Vulkan 细节仅在 `backends/*`；经 RHI 暴露 |
| 扩展点 | 优先：`IModule`、FrameGraph Pass、Material Keyword、`I*` 适配实现 |
| 能力探测 | 可选能力走 `QueryFeature` / 配置；禁止假设永远存在 |

### 2.2 子系统边界

| 子系统 | 可依赖 | 不可依赖 |
|---|---|---|
| core | 标准库 | 渲染/物理/网络实现 |
| platform | core | RHI 后端细节 |
| input | core、platform 事件 | 渲染 |
| assets | core、platform 文件 | 具体 GPU 后端类型 |
| render/rhi | core | Scene 玩法、Net |
| scene | core、math | 后端原生类型 |
| physics | core、scene 变换约定 | RHI |
| media | core、RHI 设备抽象 | 直接三方解码器类型（经 adapter） |
| net | core | 渲染、物理 |
| ui | core、input 捕获标志、RHI 纹理抽象 | 玩法状态机 |
| debug | 多数只读访问 | 不得成为业务必经路径 |

### 2.3 变更纪律

- 破坏公开 API → 改版本策略说明 + 更新文档/Sample。  
- 跨层「临时打洞」→ 必须 ADR 或 issue，并设拆除期限。  
- 新里程碑能力先补 **接口与 Feature**，再接实现。

---

## 3. 模块耦合与通讯规范

### 3.1 耦合原则

| 级别 | 允许方式 | 示例 |
|---|---|---|
| **编译期依赖** | 稳定抽象头 | Module → `IPhysicsWorld` |
| **运行时服务** | Application 上的服务定位 / 显式注入 | `app.Get<RenderSystem>()` |
| **事件** | 弱耦合通知 | Resize、设备热插拔、资源加载完成 |
| **帧序约定** | 主循环固定阶段 | 权威：[HOSTING](HOSTING.md) / [ARCHITECTURE](ARCHITECTURE.md) §4.1：Input → Net.Pump → **Asset.PumpAsync** → **Module.OnUpdate** → Video/Audio（若启用）→ Physics/World → Extract → Render |
| **禁止** | 隐藏全局、友元穿透、互相 include 形成环 | A.cpp ↔ B.cpp 循环 |

### 3.2 Module 规范

- 实现 `IModule`：`OnInit` / `OnUpdate` / `OnShutdown`（名称以实现为准）。  
- **声明依赖**：所需子系统在注册时声明；`ModuleSystem` 按依赖拓扑初始化/销毁。  
- Module **不**直接 new 后端对象；向 Application 取服务或工厂。  
- 产品逻辑放 Module；引擎核心保持无玩法。

### 3.3 通讯方式优先级

1. **直接调用稳定接口**（同线程、明确依赖）— 默认首选。  
2. **Event / 消息**（一对多、生命周期短、可忽略）— 如窗口 Resize、热插拔。  
3. **回调 / 委托**（完成通知）— 如 `Net.Pump` 派发、异步加载完成；回调内禁止重活与再入陷阱。  
4. **共享数据板**（慎重）— 仅限明确的 Frame 数据结构（如 `RenderScene`）；写权限单一、读多。

| 通讯 | 线程 | 要求 |
|---|---|---|
| Event 订阅 | 默认主线程派发 | 订阅者短小；禁止在回调里卸订阅导致迭代失效（用延迟卸） |
| Net/Asset 完成 | 经 `Pump` 回主线程 | 适配层不得对业务线程乱调 |
| 渲染提交 | 主线程或文档约定的录制线程 | 资源状态遵循 RHI 规则 |

### 3.4 反模式

- Module 之间互相硬依赖对方具体类（应抽共享接口或经 Application 协调）。  
- 用字符串魔法命令代替类型化 Event（控制台 `r.*` 除外，且只进 debug）。  
- 在物理回调里直接调 GPU；在渲染线程里做阻塞 HTTP。

---

## 4. 命名与目录

### 4.1 命名（Google C++ Style）

| 种类 | 约定 | 例 |
|---|---|---|
| 文件 | **全小写 + 下划线** `snake_case`；头 `.h`、源 `.cpp` | `path_resolver.h`、`d3d12_device.cpp` |
| 类型 | `PascalCase` | `RenderSystem`、`TextureDesc`、`ColorRgba` |
| 函数 / 方法 | `PascalCase`（动词短语） | `BeginFrame`、`PumpEvents`、`Create` |
| 访问器 / 简单互变 | 可按变量风格 `snake_case` | `width()`、`set_clear_color()`、`ok()` |
| 变量 | `snake_case` | `frame_index`、`native_window` |
| 成员变量 | `snake_case` + **尾下划线** | `device_`、`frame_index_` |
| 常量 | `k` + `PascalCase` | `kMaxFramesInFlight`、`kFrameCount` |
| 枚举器 | `PascalCase`（`enum class`） | `ErrorCode::InvalidArgument` |
| 接口 | `I` 前缀 | `IModule`、`IDevice`、`IHttpClient` |
| 命名空间 | 全小写，可用下划线 | `engine`、`engine::rhi` |

> 与 [Google C++ Style — Naming](https://google.github.io/styleguide/cppguide.html#Naming) 对齐；格式化仍以 [`.clang-format`](../../.clang-format) 为准。

### 4.2 目录

- 公开头：`include/engine/<module>/...`（或模块内 `include/`，全仓统一）。  
- 实现：`engine/<module>/src/...`；适配：`engine/<module>/adapters/...`。  
- 三方：`third_party/<name>/`，仅 adapters/工具链引用。  
- 测试：`tests/unit`、`tests/integration`、`tests/golden`。  
- 禁止：在公开头路径下放置 adapters 实现头。

---

## 5. 错误、日志与诊断

### 5.1 错误模型

- 可恢复/预期失败：返回 **`Result<T, E>` / 错误码**（与 `core` 统一），不抛异常。  
- 不变量破坏（内部 bug）：`ENGINE_ASSERT` / 日志 + 可选择终止；Release 可降级为日志。  
- GPU/驱动失败：映射为引擎错误码；**禁止**黑屏假成功（视频/网络/设备丢失同此）。

### 5.2 日志

| 级别 | 用途 |
|---|---|
| Error | 失败且影响功能 |
| Warn | 可降级或可疑 |
| Info | 生命周期、配置摘要 |
| Debug/Trace | 开发期；可由 `learn.*` / 配置关闭 |

- 日志带 **模块标签**（如 `[net]`、`[rhi.d3d12]`）。  
- 用户可见失败须可关联到错误码与下一步（文档/控制台）。

### 5.3 诊断

- Debug 构建：D3D12 Validation / Vulkan Validation 默认可开。  
- 对象命名、PIX/RenderDoc 事件：关键 Pass/资源要有名字。  
- 方法细节见 [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)。

---

## 6. 线程与同步

| 规则 | 说明 |
|---|---|
| 主线程 | 默认拥有：输入、Module 更新、Event 派发、Net/Asset Pump、提交编排 |
| 后台 | 加载、编码、网络 IO、可选命令录制；**不**直接碰未同步的 Scene 可变状态 |
| 跨线程 | 显式队列 / `Pump`；禁止无文档的数据竞争 |
| GPU | Fence/时间线同步按 RHI；多帧 in-flight 资源不得提前释放 |
| 锁 | 细粒度；禁止在持锁时调用未知业务回调 |

---

## 7. 资源寿命与帧语义

- **帧序号**：CPU 帧与 GPU 完成用 Fence 对齐；上传环按帧推进。  
- **句柄**：销毁后不得使用；悬空句柄 Debug 下可毒化。  
- **Swapchain Resize**：统一走平台事件 → 渲染重建；业务监听而非私自重建。  
- **资产**：异步加载完成仅在 Pump 后可见；失败要有占位与错误；Handle 引用与 Fence 寿命见 [RUNTIME_FOUNDATIONS.md](RUNTIME_FOUNDATIONS.md)。  
- **数据依赖 / 生命周期**：资产图、FG、Module 拓扑与所有者/阶段机见 RUNTIME_FOUNDATIONS **§6–§7**（ADR 0026）。  

---

## 8. 配置、特性开关与质量档

- 配置键分层：`r.*`（渲染）、`net.*`、`audio.*`、`learn.*`、`app.*`。  
- 质量档（Low/Med/High）只映射 **已知开关组合**，不隐藏新魔法路径。  
- Feature 缺失：降级路径写进 POSITIONING/PLAN；测试用 SKIP/FAIL 语义（见 TESTING）。  
- 命令行与配置文件等价覆盖，优先级文档化（实现时写清）。

---

## 9. 第三方与抽象层

完整规则见 [THIRD_PARTY.md](THIRD_PARTY.md) 与 [ADR 0017](learn/adr/0017-third-party-boundary.md)。

摘要：

1. 先有引擎 `I*` / Facade，再接三方。  
2. 业务零直链三方头。  
3. 换库只改 adapters + CMake。  
4. 新三方必须更新对照表与许可表。

---

## 10. 测试与质量门禁

见 [TESTING.md](TESTING.md)。补充：

| 门禁 | 要求 |
|---|---|
| PR | 通过 unit；格式/告警不回归 |
| 公开 API 变更 | 有单测或集成测覆盖关键路径 |
| GPU | 按里程碑挂 `[gpu]`；无 GPU CI 跳过而非红 |
| 网络 | loopback 集成；不依赖外网稳定性 |

---

## 11. 文档与 ADR

- 架构行为变更 → 更新 ARCHITECTURE / PLAN 对应节。  
- 决策性取舍 → 新增或修订 ADR，并登记 [ADR_INDEX.md](learn/ADR_INDEX.md)。  
- 学习 Sample 与产品能力双轨同步（见 learn/）。  
- 文档编码 UTF-8；中文产品文档、关键术语进 GLOSSARY。

---

## 12. 安全与隐私（网络 / 文件）

- **TLS**：HTTPS / WSS / QUIC 默认验证证书；显式配置才能放宽（仅调试）。  
- **路径**：VFS/资产路径防 `..` 逃逸；不任意执行下载内容。  
- **密钥**：不把密钥写入仓库；用环境变量/本地未跟踪配置。  
- **日志**：避免打印完整 token、隐私 URL 查询串。  
- 不做反作弊/安全产品；但传输层不故意关闭校验。

---

## 13. 构建、Git 与 PR

| 项 | 约定 |
|---|---|
| 构建 | 以 CMake 为唯一入口；选项 `ENGINE_WITH_*` |
| 分支 | 主分支可发版；特性分支短生命周期 |
| Commit | 说明「为什么」；中英文均可，风格与仓库历史一致 |
| PR | 说明动机、风险、测试方式；大改附文档 diff |
| 秘密 | 禁止提交 `.env`、证书私钥、专有 SDK 违规再分发物 |

---

## 14. 性能与调试约定

- 先正确，再优化；优化须有测量（Profiler / PIX / 计时）。  
- Debug 视图与 `learn.*` 慢路径不得污染 Release 默认热路径（用宏或配置剥离）。  
- 避免每帧无界分配；容器可复用。  
- 新 Pass 默认考虑质量档可关。

---

---

## 15. 双后端特性分级与场景数据布局

> 决策依据：[ADR 0024](learn/adr/0024-backend-feature-tiers-and-soa.md)。  
> 目标：控制 D3D12+Vulkan **维护成本**；在 **非全量 ECS** 前提下提升规模路径。

### 15.1 特性分级（L0 / L1 / L2）

| 级 | 含义 | 落地要求 | 示例 |
|---|---|---|---|
| **L0** | 双后端必须对齐 | D3D12 与 Vulkan（及 Linux）均可验收；缺一不可合入「通用主路径」 | 清屏、网格/纹理、PBR 主光、CSM、P0 阴影/TAA/AO、主提交路径 |
| **L1** | 允许暂单端或后对齐 | 须 QueryFeature；文档与 Sample 写清；有明确追平里程碑 | DXR 先 D3D12，Vulkan RT → M25 |
| **L2** | 允许永久能力差 | 启动可打印 Feature 矩阵；无能力 → SKIP/占位/明确错误，禁止假成功 | 某驱动无 Vulkan Video；DLSS 仅部分 API |

规则：

1. 新特性合入前必须标定 **L0/L1/L2**（PR / ADR / 能力矩阵择一处写明）。  
2. **禁止**在业务 Module 里按后端写两套玩法分支；只查 Feature 后走统一降级路径。  
3. L0 变更：两端实现 + 至少一端 GPU 冒烟；另一端不得无故落后超过一个里程碑（除非改标为 L1）。

### 15.2 落地顺序：先 D3D12，再 Vulkan

1. 默认在 **Windows + D3D12** 完成正确性与 Sample。  
2. 再追平 **Vulkan**（Windows），然后 **Linux Vulkan**（与 PLAN M17/M18 一致）。  
3. P2 大特性（动态 GI、地形等）**禁止**两端从零并行开干；一侧稳定后再移植。  
4. CI 建议：PR → unit +（可选）单后端冒烟；夜跑/发版 → D3D12 + Vulkan。

### 15.3 场景模型：场景树 + 热路径 SoA（非全量 ECS）

| 层 | 约定 |
|---|---|
| 编辑/逻辑权威 | World/Node 场景树（层级、序列化、2D/3D 混合心智） |
| 渲染热路径 | 每帧提取为紧凑结构：RenderScene、实例矩阵/可见列表等 **SoA 或连续数组** |
| 规模手段（优先） | 实例化、间接绘制、GPU Cull、流式/LOD（见 PLAN P0/P1/P2） |
| 全量 ECS | **不作为默认**；仅当 CPU 更新或实体规模有测量证据时，可对子集（Transform/实例/粒子）引入局部表或局部 ECS |

规则：

1. 禁止在深树上每帧随机访问作为提交热路径。  
2. Update 尽量按系统批处理（动画一批、2D 一批），再写入提取缓冲。  
3. 引入局部 ECS / Archetype 须 ADR，并说明与 Node 的同步边界。

### 15.4 共享资产与测试

- 着色器：业务 HLSL 一源；DXC → DXIL / SPIR-V。  
- 黄金图：场景共用，基线按 `backend`（及 OS）分目录或元数据。  
- 启动诊断：Debug/开发构建可打印 Feature 摘要。

---

## 16. 相关文档

- [README.md](README.md)  
- [GETTING_STARTED_M1.md](GETTING_STARTED_M1.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [PLAN.md](PLAN.md)  
- [TOOLING.md](TOOLING.md)  
- [THIRD_PARTY.md](THIRD_PARTY.md)  
- [TESTING.md](TESTING.md)  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)  
- [learn/ADR_INDEX.md](learn/ADR_INDEX.md)  
- [learn/adr/0024-backend-feature-tiers-and-soa.md](learn/adr/0024-backend-feature-tiers-and-soa.md)  
- [learn/adr/0025-toolchain-minimum-viable.md](learn/adr/0025-toolchain-minimum-viable.md)  
