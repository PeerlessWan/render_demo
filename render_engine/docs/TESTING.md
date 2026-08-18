# 测试方案：单测 · 集成测试 · 自动化

> 与 [PLAN.md](PLAN.md)、[DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)、[THIRD_PARTY.md](THIRD_PARTY.md)、[SANDBOX_MCP.md](SANDBOX_MCP.md) 配套。  
> 目标：在 **无显示器人工盯帧** 的前提下，尽可能拦截回归；渲染正确性用 **黄金图 + 容差** 兜底。  
> **不做全覆盖**：像素级 + 帧级自动化锁主路径外观与「这一帧没崩」；组合爆炸与 GPU 差异决定了只能 **抽样**。分工、水位、测法见 **§8**。加深策略（准优先于广）见 [PLAN.md](PLAN.md) **§3.1**。

## 1. 测试分层

```text
┌─────────────────────────────────────────┐
│  E2E / 自动化（CI 或夜跑机）              │
│  headless/带 GPU：冒烟、黄金图、特性开关   │
├─────────────────────────────────────────┤
│  集成测试（tests/integration）            │
│  多模块协作：RHI+资源、场景+物理、音视频   │
├─────────────────────────────────────────┤
│  单元测试（tests/unit）                   │
│  纯逻辑：数学、配置、ActionMap、序列化…   │
└─────────────────────────────────────────┘
```


| 层级               | 跑在哪                    | GPU   | 速度目标     | 失败含义      |
| ---------------- | ---------------------- | ----- | -------- | --------- |
| **单元测试**         | 开发机 / CI（可无 GPU）       | 否     | 秒级       | 逻辑回归      |
| **集成测试**         | 开发机 / 带 GPU 的 CI agent | 部分需要  | 分钟级      | 模块协作回归    |
| **自动化 / 冒烟+黄金图** | 固定 GPU 机或自托管 runner    | **是** | 数分钟～十余分钟 | 渲染/驱动相关回归 |


原则：能单测的不集成；能集成的不人工点 Sandbox。  
谁自动化 / 谁人工 / 谁上 PIX、能覆盖多少、像素级与帧级怎么断言：见 **§8**。

---



## 2. 单元测试（Unit）



### 2.1 范围（优先）


| 模块                        | 测什么                                    |
| ------------------------- | -------------------------------------- |
| `core/math`               | 矩阵乘法、逆、投影、四元数、近似相等                     |
| `core/config`             | 解析、默认值、覆盖、非法项                          |
| `core/result` / 错误码       | 传播与格式化                                 |
| `input/ActionMap`         | 绑定、重绑、存盘/加载、死区                         |
| `assets/AssetId` / VFS 路径 | 规范化、根目录沙箱（禁止 `..` 逃逸）                  |
| `scene` 纯逻辑               | Transform 脏更新、层级世界矩阵、视锥平面提取（可用合成 AABB） |
| `material` 变体键            | Keyword 集合 → 稳定哈希；排序键                  |
| `serialization`           | 场景片段 round-trip（无 GPU）                 |
| 物理封装的 **纯适配逻辑**           | 层掩码转换等（不启动完整世界也可 mock）                 |




### 2.2 不做单测（或极低优先级）

- 真实 D3D12 设备创建细节（归集成）  
- 像素级着色器观感（归黄金图）  
- ImGui 点击视觉（可选 UI 自动化后置）



### 2.3 技术选型（现状）


| 项   | 选择                         |
| --- | -------------------------- |
| 框架  | **mini_test**（`tests/unit/mini_test.h`；Catch2 仍为可选目标，未接） |
| 目录  | `tests/unit/**`            |
| 目标  | **`engine_unit_tests`**    |
| 断言  | 浮点用 epsilon；矩阵比元素          |


CMake：`ENGINE_BUILD_TESTS=ON` 时启用。集成用例目前与 unit **同二进制**（标签 `[headless]` / `[gpu_headless]`），尚无独立 `tests/integration/` 目标。

---



## 3. 集成测试（Integration）



### 3.1 范围


| 套件             | 内容                                                                   | GPU     |
| -------------- | -------------------------------------------------------------------- | ------- |
| **RHI 冒烟**     | 创建设备、清屏一帧、创建缓冲/纹理、Fence 等待；**D3D12 与 Vulkan 各至少一套**（按平台可用） | 是       |
| **资源管线**       | 加载 PNG + glTF → 上传 → 绑定一次 Draw（可离屏 RT）                               | 是       |
| **FrameGraph** | 两 Pass 读写依赖编译执行；错误依赖应失败                                              | 是       |
| **物理**         | 世界步进、射线命中盒子、角色站立（无窗口）                                                | 否（Jolt） |
| **音频**         | 解码短 WAV 到 PCM 缓冲（可不播设备）                                              | 否       |
| **视频**         | 当前后端 `Feature::VideoTexture*` 为真时：打开样片解码一帧；无能力 → **SKIP**；有能力但解码失败 → **FAIL** | 是       |
| **输入**         | 合成事件注入 ActionMap（无真实设备）                                              | 否       |
| **UI**         | Retained 布局加载；ImGui 可不测渲染，测捕获标志逻辑                                    | 可选 GPU  |
| **网络**       | Loopback：HTTP 回显、WS 回显、QUIC 可靠流收发；TLS 可选分用例；无外网依赖；HTTPS 可用性随 `ENGINE_WITH_OPENSSL`（系统 SDK / `OPENSSL_ROOT_DIR`） | 否        |
| **Resize**     | Swapchain/RT 重建后仍可 Present 或渲染到纹理                                    | 是       |




### 3.2 约定

- 集成测试可链接 `engine` 静态/动态库，使用 **无窗口或隐藏窗口** + 离屏 RT，避免 UI 弹窗挂死 CI。  
- 需要 GPU 的用例标记 `[gpu]`；CI 无 GPU 时跳过而非红。  
- 视频：当前后端无硬解能力 → **SKIP**；有能力但解码失败 → **FAIL**（无软解/跨 API 降级）。D3D12 与 Vulkan 各自覆盖（Linux 仅 VK）。  
- 后端：GPU 冒烟在 Windows 上应覆盖 **D3D12 + Vulkan**；Linux 仅 Vulkan（可选自托管）。特性分级见 STANDARDS §15 / ADR 0024（PR 可单后端；发版/夜跑双后端）。
- DLSS：无 NGX → SKIP；有则可选短路径（或仅测 `IUpscaler` fallback 选型逻辑）。



### 3.3 目录

```text
tests/
  unit/
  integration/
  data/                 # 极小 glTF、PNG、WAV、短 MP4（许可清晰）
  golden/               # 黄金图与元数据（见下）
  scripts/              # 跑测、比图、生成报告
```

---



## 4. 自动化测试（Automation / CI）



### 4.1 流水线建议


| Job        | 触发                | 内容                                     |
| ---------- | ----------------- | -------------------------------------- |
| **PR 快速**  | 每个 PR             | 配置 + **仅 unit** +（可选）无 GPU integration |
| **GPU 冒烟** | merge 到 main / 每日 | `[gpu]` 集成 + 离屏清屏/三角/PBR 最小帧（**Win：D3D12+Vulkan**；Linux：Vulkan） |
| **黄金图**    | 每日或发版前            | 固定场景多配置截图比对（基线按 **backend/OS** 分目录或容差） |
| **特性矩阵**   | 每周或发版             | 质量档 ×（RT on/off）×（超分 off/fsr）×（backend）抽样        |


Windows + MSVC；Linux + GCC/Clang（M18+）。自托管 runner 需：**GPU 驱动、DXC、Vulkan SDK/校验层、（可选）DLSS 运行时**。

### 4.2 黄金图（Golden Image）


| 项   | 约定                                    |
| --- | ------------------------------------- |
| 场景  | `tests/data/golden_scenes/*` 固定资产与相机  |
| 输出  | 离屏 RT Readback → PNG                  |
| 比对  | 逐像素容差 **或** RMSE / 允许百分比差；深度/法线可用更严阈值 |
| 元数据 | JSON：分辨率、质量档、commit、GPU 名、**backend**、API/驱动版本 |
| 更新  | 仅人工 `approve-golden` 流程更新基线（防静默漂）     |


配置示例维度：`quality=low/med`、`taa=on/off`、`shadows=on`（High+RT 可夜跑）。

**Bindless 热路径（可选基线）**：Feature `bindless_hot_path` 默认 **OFF**，主黄金图 / C4 走 classic `pad=-1`。若要覆盖 SM6.6 `ResourceDescriptorHeap` 热路径，需单独 `SetFeatureOverride("bindless_hot_path", true)` 并维护 **可选** 黄金基线（不替换默认基线；缺基线 → SKIP）。详见 [VULKAN_PARITY.md](VULKAN_PARITY.md) Bindless 行。

### 4.3 性能冒烟（可选）

- 固定场景跑 N 帧，记录平均 GPU/CPU 帧时。  
- 相对基线恶化超过阈值（如 +20%）→ **警告或 fail**（按机器标定）。  
- 不与黄金图混为同一阈值。



### 4.4 本地命令（实现时对齐）

```text
cmake --build build --target engine_tests_unit
ctest -R unit -C Debug

cmake --build build --target engine_tests_integration
ctest -R integration -C Release          # 或带 -L gpu

python tests/scripts/run_golden.py --config med
python tests/scripts/compare_golden.py
```

---



## 5. 与里程碑的挂钩


| 里程碑         | 测试交付                                    |
| ----------- | --------------------------------------- |
| **M1**      | Catch2 接入；`math`/`config` 单测；CTest；清屏集成可选手动 |
| **M2**      | RHI 清屏/三角 **集成**（gpu）；截图 API 雏形；shader_compile 可复现 |
| **M3**      | FrameGraph 集成用例；**异步加载 Pump 回调**；Handle 引用骨架；Manifest 依赖约定 |
| **M4–M5**   | 视锥/场景逻辑单测；PBR 离屏一帧集成；RenderScene 抽取不写回权威树 |
| **M7**      | 音频解码单测/集成；视频硬解 **SKIP/FAIL**（按当前后端）落地 |
| **M8**      | 控制台命令解析单测；序列化 round-trip；**Profiler CPU/GPU 计数冒烟** |
| **M9**      | 基础段 **黄金图 v0**；**cook 清单+依赖图**可复现 |
| **M10–M11** | LOD/实例逻辑单测；TAA/AO 开关黄金图抽样 |
| **M12**     | 物理集成（射线/堆叠） |
| **M13**     | P1 后处理/反射开关冒烟 |
| **M14**     | 质量档矩阵冒烟；**多线程录制可开关**稳定性 |
| **M15**     | UI 输入捕获单测；菜单冒烟（可选） |
| **M16**     | 2D/像素排序与 Tilemap 冒烟；图集契约加载 |
| **M17**     | Vulkan Windows：清屏/三角冒烟；Video Feature 探测 |
| **M18**     | Linux Vulkan 冒烟（自托管 runner） |
| **M19**     | 网络 loopback：HTTP / WS / QUIC 可靠流集成；TLS 抽样 |
| **M20–M21** | 混合/2D 深度开关冒烟 |
| **M22–M25** | 动态 GI / 地形 / GPU Driven / VK RT 按 Feature 抽样 |


**验收：** M9 起 PR 必须过 unit；带 GPU 的 main 流水线必须过 RHI 冒烟；发版过黄金图套件。

---



## 6. Flaky 与环境策略


| 问题        | 策略                                      |
| --------- | --------------------------------------- |
| 驱动/GPU 差异 | 黄金图按 **GPU 族** 分基线，或放宽容差 + 人工审          |
| 时间相关      | 固定 `dt`、关闭异步或抽干队列再截图                    |
| 字体/DPI    | 黄金图固定 DPI=1、固定字体文件                      |
| 并行抢 GPU   | CI 串行 gpu job                           |
| 第三方缺失     | DLSS/VA：**SKIP**；Jolt 为必选集成依赖则 **配置失败** |


---



## 7. 目录与目标命名（现状 + 最小黄金图）

```text
tests/
  CMakeLists.txt
  unit/...                 # 已落地 → engine_unit_tests
  golden/baselines/...     # 最小黄金图（缺基线则 SKIP）
  scripts/run_golden.py
  scripts/compare_golden.py
  # integration/ data/     # 仍为目标方案，未建独立 suite
```


| CMake 选项               | 含义            |
| ---------------------- | ------------- |
| `ENGINE_BUILD_TESTS`   | 构建 `engine_unit_tests` |
| `ENGINE_GOLDEN_TESTS`  | 注册黄金图 CTest（无基线/无 exe → SKIP） |

本地黄金图：

```bat
python tests/scripts/run_golden.py --approve   # 生成基线
python tests/scripts/run_golden.py            # 比对
```

Sandbox 在 `--gpu-headless` 且设置 `ENGINE_GOLDEN_DUMP=<path.rgba>` 时写出读回帧。

### Headless CI（已落地）

```bat
ctest --test-dir build -C Debug -L headless --output-on-failure
scripts\ci_headless.ps1
scripts\ci_headless.ps1 -Golden
scripts\ci_headless.ps1 -Golden -StrictParity   # 可选：C4 超阈 FAIL（默认仍记对标）
```

- `CreateHeadlessDevice`：无 HWND，支持 Clear / DispatchCompute / ReadbackTextureStub / Present  
- `ApplicationDesc.headless` / `gpu_headless` + `headless_frames`  
- 标签：`headless.engine_unit_tests`、`gpu_headless.sandbox`（`--backend=d3d12`）  
- **窗口路径冒烟**：`sample_sandbox --backend=d3d12 --headless_frames=12`（**无** `--gpu-headless`）覆盖 post 后的 scale 实例化；`ci_headless.ps1` 与 `windowed.sandbox_scale_path` 已接  
- Vulkan `gpu_headless`：隐藏 HWND + 真 Readback（见 [VULKAN_PARITY.md](VULKAN_PARITY.md)）



---



## 8. 分工、覆盖水位与测法

> 决策摘要见 [ADR 0018](learn/adr/0018-testing-strategy.md)。抓帧排错见 [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md) 与 [learn/DEBUG_WORKFLOW.md](learn/DEBUG_WORKFLOW.md)。Harness 协议见 [SANDBOX_MCP.md](SANDBOX_MCP.md)。

### 8.1 结论

**像素级 + 帧级自动化做不到全覆盖，也不该去追。** 三路分工：

| 谁 | 锁什么 | 不锁什么 |
|---|---|---|
| **自动化** | 逻辑回归、主路径没崩/非全黑、默认场景外观（容差内） | 观感好坏、屏障对不对、驱动独有 bug |
| **人工** | 黄金图基线批准、画质/手感、新特性「看起来对」 | 每 PR 的穷尽点点 |
| **工具** | Pass 顺序、屏障/布局、PSO、Device Removed | 替代 CI；不进引擎自研 PIX |

判定规则（与 §1 同一原则，补一条）：**能脚本断言的不盯帧；必须看 GPU 状态才上 PIX / RenderDoc。**

### 8.2 自动化测

可重复、无显示器、失败含义明确。缺 GPU / 缺能力 / 缺基线 → **SKIP**，禁止假绿。

| 项 | 测法 | 命令 / 入口 | 现状 |
|---|---|---|---|
| 纯逻辑 | 单测：epsilon / 元素相等 | `engine_unit_tests`；`ctest -L headless` | **已落地** |
| CPU headless 集成 | 无窗口泵帧、物理射线、序列化 | 同二进制 `[headless]` | **已落地**（尚未拆 `tests/integration/`） |
| 帧级冒烟 | `--gpu-headless` 固定 N 帧；读回拒绝全黑/全白 | `sample_sandbox --gpu-headless --backend=d3d12`；`scripts/ci_headless.ps1` | **D3D12 已落地**；VK 真读回已接 |
| 窗口 scale 冒烟 | 有 HWND、跑 post 后 `DrawLitInstanced`（gpu-headless 会跳过） | `--backend=d3d12 --headless_frames=12`；`windowed.sandbox_scale_path` | **已落地**（防 DEVICE_REMOVED 回归） |
| 像素级黄金图 | 离屏 Readback → `.rgba`；RMSE + 最大通道差 | `python tests/scripts/run_golden.py`；`-Golden` | **落地**（≥1 条 Sandbox 基线；无基线 SKIP） |
| 特性矩阵抽样 | 固定 `dt`；切质量档/TAA/阴影后再 capture | `run_matrix_smoke.py` + Harness | **落地**（d3d12 质量×toggle；VK 抽样） |
| Harness | **保留冻结**：矩阵抽样接口；不再加命令 | `--harness-stdio`；`run_matrix_smoke.py` | **落地** |
| MCP | **冻结**：Cursor 薄适配；不扩；CI 不依赖 | `sandbox_mcp`；本机不用可删 | **落地** |
| Feature / 视频 | 无能力 SKIP；有能力失败 FAIL | `QueryFeature`；集成用例 | 部分落地 |
| 网络 loopback | HTTP/WS/QUIC 回显 | 集成标签 | 按 M19 |
| 性能冒烟 | 固定场景 N 帧，相对基线 +20% 警告 | 不与黄金图混阈值 | **可选 / 未作为门禁** |

Harness：**CI 直连** `--harness-stdio` 或 `run_golden.py` / `run_matrix_smoke.py`。MCP 只给 Cursor Agent 扫开关，**不充当准确度来源、不进门禁、不再加工具**（[PLAN.md](PLAN.md) §3.21）。

### 8.3 人工测

自动化给不出「好不好看」或「这条基线该不该改」时才上。

| 项 | 何时 | 怎么做 |
|---|---|---|
| **批准黄金图** | 换 GPU 族、改默认光照/相机/曝光、着色意图变化 | 确认同机 RMSE 是意图而非噪声后 `run_golden.py --approve` |
| **画质走查** | 发版前；P0/P1 后处理/阴影/IBL 合入 | Sandbox：Low/Med/High 可感知差异；F1 关特性二分 |
| **手感 / 相机 / 输入** | 控制方案或 WantCapture 变化 | 真窗口：键鼠手柄、UI 吃输入、DPI |
| **新 Sample / 新 Pass「看起来对」** | 教学章或新渲染路径首次合入 | 对照 learn 章 + Debug 视图；不把观感写进 CI |
| **黄金图失败归因** | CI 红但本地同 GPU 不过 | 先环境（驱动/分辨率）再改代码；禁止未审就覆盖基线 |
| **多 GPU / 笔记本核显** | 发版抽样 | 人工看主路径；不为每张卡维护黄金图（按 GPU 族或放宽容差） |

不做：用人工点 Sandbox 替代 unit；用「我觉得差不多」静默改基线。

### 8.4 工具测

引擎内排错走完再抓帧。**不自研** PIX 级帧调试器（[TOOLING.md](TOOLING.md)）。

| 工具 | 测 / 查什么 | 何时用 |
|---|---|---|
| **D3D12 / Vulkan Validation** | API 误用、资源状态、描述符、布局 | Debug 默认开；CI 可选 Validation 冒烟（报错即 FAIL） |
| **PIX**（D3D12） | Pass 顺序 vs FrameGraph、屏障、PSO 抖动、Pass 耗时 | 花屏/闪一帧/同步怀疑；Device Removed |
| **RenderDoc** | VS/PS 输入、RT/纹理内容、两后端同一 Draw 对比 | 材质绑错、GBuffer 通道、VK 布局 |
| **GPU 崩溃转储** | HRESULT / VkResult / Reason / 面包屑 | TDR、Present 失败 |
| **DXC 编译日志** | 变体/宏导致的 PSO 失败 | 粉红材质、Keyword 缺失 |
| **引擎内 Debug 视图 / DebugDraw / Profiler** | Albedo/Normal/Cascade/Overdraw；AABB/光锥；CPU/GPU Pass 时间 | 人工走查与调优第一轮；**先于**外部抓帧 |

工具测的产出是诊断，不是 CI 绿。把抓帧结论写进 PR / 章节「建议看什么」，不要把 PIX 截图当黄金图。  
**RenderDoc / PIX 不进 CI 门禁**（[PLAN.md](PLAN.md) §3.1）；GPU 状态类自动化优先 Validation。

### 8.5 能覆盖多少（水位，非行覆盖率）

下表是 **风险覆盖的主观水位**，不是 `gcov` 百分比。组合（backend × 档位 × 特性 × GPU × 驱动）不可穷尽。

| 面 | 目标水位 | 靠什么 | 明确不覆盖 |
|---|---|---|---|
| 纯逻辑（math/config/视锥/LOD 键/序列化） | **高**：关键函数有断言 | unit | 未调用的死代码 |
| 主演示路径存活（D3D12 Sandbox 能画、非黑） | **高** | 帧级 `gpu-headless` | VK 像素（读回 stub）；Linux 待 M18 |
| 主演示路径外观（默认相机/档位） | **中**：1 条黄金图 + 容差 | 像素级 RMSE≤8、max_abs≤48 | bit-exact；每张 GPU 一张图 |
| P0/P1 特性组合 | **低～中**：抽样 | 夜跑/发版矩阵；Harness 扫 | 笛卡尔积（TAA×SSAO×SSR×阴影×IBL×RT×超分×档位×后端） |
| 资源寿命 / 屏障 / in-flight | **中**（集成 + Validation） | 单测寿命约定 + 校验层 | 所有 Pass 排列 |
| 物理 / 音频 / 网络 | **中**：契约路径 | 集成 loopback / 解码缓冲 | 真设备声学、公网抖动 |
| 画质与手感 | **人工发版** | 走查清单 | CI |
| 驱动/硬件独有 | **低**：SKIP 或人工 | Feature 门控 | 全驱动矩阵 |

流水线与水位对齐（落实 §4.1）：

```text
PR          → unit（逻辑高覆盖）
merge / 每日 → GPU 帧级冒烟（主路径存活）
每日 / 发版  → 黄金图（主路径外观）
每周 / 发版  → 特性矩阵抽样（不是全组合）
发版        → 人工画质走查 + 必要抓帧
```

**合计口径：** 自动化负责「主路径没崩 + 默认画面没漂 + 逻辑没回退」；人工 + 工具补上观感与 GPU 状态。宣称「像素/帧级全覆盖」视为文档跑偏。

### 8.6 测法（怎么断言）

#### 逻辑断言（unit / 部分 integration）

- 浮点：epsilon；矩阵比元素。  
- 场景：Transform 脏更新、视锥 vs 合成 AABB。  
- 失败语义：`Result` / Feature 缺失可诊断，禁止吞错。

#### 帧级验证（冒烟）

问的是：**这一帧有没有画完、输出是否荒唐**，不是「像不像基线」。

1. 固定 `headless_frames`（建议 3～8）、固定后端。  
2. `ReadbackTextureStub` → RGBA8。  
3. 断言：分辨率 > 0；非全黑；非全白（Sandbox `--gpu-headless`）。  
4. 可选：平均亮度落在区间；Profiler 有上一帧 GPU 计数（无则 n/a，不红）。  
5. 时间相关：固定 `dt`，截图前抽干异步加载。

失败含义：Present/绘制/读回路径回归。TAA 开关导致的细微差 **不应** 用帧级全黑断言去抓。

#### 像素级验证（黄金图）

问的是：**固定场景还长得像批准过的基线吗**。

1. 锁死：场景、相机、分辨率、DPI=1、档位、特性开关、`dt`、帧序号。  
2. 离屏读回写成 `.rgba`（`u32 w` + `u32 h` + `w*h*4` RGBA8）。环境变量 `ENGINE_GOLDEN_DUMP`。  
3. `compare_golden.py`：分辨率必须一致；**RMSE ≤ 8**（字节 0–255）；**单通道 max abs ≤ 48**（可调）。  
   - **Q5 ROI**：`--roi-ignore-hud` 将右上 Perf/橙块区域置零后再比（Sandbox 黄金图 / C4 默认开）。  
4. 失败时保留候选图；批准基线用 `run_golden.py --approve`（人工）。  
4. 深度/法线比对若做，用更严阈值，且与 LDR 颜色分文件。  
5. 基线按 **backend / OS / GPU 族** 分目录（目标）；现状仅 D3D12 一条 `sandbox_gpu_headless.rgba`。  
6. 更新基线 **仅** `--approve`；CI 不得覆盖。

不要：逐像素要求相等；为过 CI 把阈值放到无限大；无 GPU 时 FAIL（应 SKIP）。

#### 特性矩阵抽样（Harness，扩覆盖面）

准确度仍靠黄金图；Harness 只负责 **换配置再截**。

建议抽样格（发版，非每 PR）：

| 格 | 目的 |
|---|---|
| D3D12 × 默认 High | 主路径黄金图 |
| D3D12 × Med × TAA off | 无历史帧时仍可画 |
| D3D12 × 阴影 off | 直射光路径 |
| Vulkan × 默认（Feature 允许的子集） | 对标冒烟；像素待真读回 |

每格一份基线。Agent 经 MCP 扫 Feature 可以，但 **合并门禁仍跑脚本**。

#### 工具测法（抓帧，不进 CTest）

1. 引擎内：日志 → Debug 视图 → 关特性二分 → Profiler。  
2. PIX：命令列表是否与 FrameGraph 一致；RT↔SRV 屏障；异常频繁的 PSO 切换。  
3. RenderDoc：贴图绑定、RT 附件、VK 布局。  
4. 结论用于修 bug 或补 **一条** 自动化（能变成断言的才下沉到 unit/golden）。

### 8.7 按子系统速查

| 子系统 | 自动化 | 人工 | 工具 |
|---|---|---|---|
| math / config / ActionMap / 序列化 | unit | — | — |
| 场景抽取 / LOD 选择 / 视锥 | unit + 抽出不写回权威树 | 远景 pop 观感 | — |
| RHI 清屏/三角/上传 | gpu 集成 + 帧级读回 | — | Validation |
| 着色 / IBL / 阴影 / 后处理 | 黄金图抽样；开关冒烟 | 发版画质；F1 二分 | Debug 视图；PIX/RenderDoc |
| TAA / 运动矢量 | 数学单测 + 开/关两格黄金图 | 鬼影/拖影 | 抓帧看历史 RT |
| FrameGraph / 屏障 | 依赖编译失败用例；Validation | — | PIX 屏障 |
| 实例化 / Indirect | Feature + 条数/回退单测；gpu 冒烟 | 大规模闪烁 | PIX Draw 次数 |
| 物理 | Jolt 射线/堆叠集成 | 角色手感 | DebugDraw 碰撞体 |
| 音频 | 解码到 PCM | 听感/欠载 | — |
| 视频 | Feature SKIP/FAIL；硬解一帧 | 音画同步 | 厂商工具可选 |
| UI | WantCapture 逻辑单测 | 真窗口点击/DPI | — |
| 网络 | loopback 集成 | — | — |
| DXR / DLSS / Mesh Shader | Feature 门控；无则 SKIP | 有硬件时目视 | PIX/NGX 日志 |
| Vulkan 对标 | 能跑 + Feature 可诊断 | IBL/post 可感知 | RenderDoc + 校验层 |

### 8.8 加深策略（下一档）

要更高准确度 / 覆盖面：先 **确定性截帧** 与 **Vulkan 真 Readback**，再 Validation CI、learn 小场景与中间缓冲黄金图。矩阵格用现有 harness `capture` 升级为比图。可选 WARP / SSIM·FLIP / 双后端一致性。

权威顺序、验收与不做项：[PLAN.md](PLAN.md) **§3.1**。不扩 Harness 命令、不扩 MCP。

---



## 9. 相关文档

- [PLAN.md](PLAN.md)（**§3.1 测试加深策略**；§3.21 Harness/MCP 冻结）  
- [TOOLING.md](TOOLING.md)（黄金图脚本、cook 与测试工具链边界）  
- [THIRD_PARTY.md](THIRD_PARTY.md)（Catch2、测试数据许可）  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)  
- [learn/DEBUG_WORKFLOW.md](learn/DEBUG_WORKFLOW.md)  
- [SANDBOX_MCP.md](SANDBOX_MCP.md)  
- [VULKAN_PARITY.md](VULKAN_PARITY.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)

