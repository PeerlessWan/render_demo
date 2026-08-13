# 测试方案：单测 · 集成测试 · 自动化

> 与 [PLAN.md](PLAN.md)、[DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)、[THIRD_PARTY.md](THIRD_PARTY.md) 配套。  
> 目标：在 **无显示器人工盯帧** 的前提下，尽可能拦截回归；渲染正确性用 **黄金图 + 阈值阈值** 兜底。

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



### 2.3 技术选型


| 项   | 选择                         |
| --- | -------------------------- |
| 框架  | **Catch2**（推荐）或 GoogleTest |
| 目录  | `tests/unit/**`            |
| 目标  | `engine_tests_unit`        |
| 断言  | 浮点用 epsilon；矩阵比元素          |


CMake：`ENGINE_BUILD_TESTS=ON` 时启用。

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
| **网络**       | Loopback：HTTP 回显、WS 回显、QUIC 可靠流收发；TLS 可选分用例；无外网依赖               | 否        |
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



## 7. 目录与目标命名（建议）

```text
tests/
  CMakeLists.txt
  unit/...
  integration/...
  data/...
  golden/baselines/...
  scripts/run_golden.py
  scripts/compare_golden.py
```


| CMake 选项               | 含义            |
| ---------------------- | ------------- |
| `ENGINE_BUILD_TESTS`   | 构建测试          |
| `ENGINE_RUN_GPU_TESTS` | 启用 `[gpu]` 标签 |
| `ENGINE_GOLDEN_TESTS`  | 启用黄金图脚本目标     |

### Headless CI（已落地）

```bat
ctest --test-dir build -C Debug -L headless --output-on-failure
```

- `CreateHeadlessDevice`：无 HWND，支持 Clear / DispatchCompute / ReadbackTextureStub / Present  
- `ApplicationDesc.headless` + `headless_frames`：固定跑 N 帧后退出  
- 标签：`headless.engine_unit_tests`（LABELS=headless）；与 `unit.engine_unit_tests` 共用同一二进制  



---



## 8. 相关文档

- [PLAN.md](PLAN.md)  
- [TOOLING.md](TOOLING.md)（黄金图脚本、cook 与测试工具链边界）  
- [THIRD_PARTY.md](THIRD_PARTY.md)（Catch2、测试数据许可）  
- [DEBUG_TUNE_TROUBLESHOOT.md](DEBUG_TUNE_TROUBLESHOOT.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)

