# Learn 02 — Textured Triangle（纹理三角形）

> 在 D3D12 上加载 **编译后的 VS/PS（DXIL/.cso）**，用 `SetupSimpleMesh` / `DrawSimpleMesh` 画出带纹理的全屏三角，理解「着色器产物路径」与上传环之前的最小绘制。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_02_triangle
build\samples\learn\02_triangle\Debug\sample_02_triangle.exe
```

着色器由 CMake/DXC 编到 `ENGINE_SHADER_DIR_A`（`triangle.vs.cso` / `triangle.ps.cso`）。

## 知识点

1. **离线编译**：业务 HLSL → DXC → DXIL（`.cso`）；运行时只加载字节码，不在本课现场编译。
2. **PathResolver**：用根目录解析相对名，避免硬编码绝对路径。
3. **SimpleMesh 路径**：比完整 Lit/PBR 更短的 RHI 演示：建 PSO、绑 VB、画一个（或少数）三角形。
4. **CMake 注入宏**：`ENGINE_SHADER_DIR_A` 必须由构建系统定义，否则 `#error` 直接失败——这是刻意的「缺配置可诊断」。
5. **与 Clear 的递进**：01 只证明 Present；02 证明「着色器+顶点数据」进入管线。

## 名词解释

| 术语 | 含义 |
|---|---|
| **HLSL** | DirectX 高级着色语言；本引擎业务着色器源格式。 |
| **DXC** | DirectX Shader Compiler；产出 DXIL 或 SPIR-V。 |
| **DXIL** | D3D12 着色器字节码中间层；常落盘为 `.cso`。 |
| **PSO** | Pipeline State Object：光栅/混合/深度/着色器入口的组合状态。 |
| **Vertex Buffer (VB)** | GPU 可读的顶点数组；三角形至少 3 顶点。 |
| **SRV** | Shader Resource View；像素着色器采样纹理时绑定。 |
| **Root Signature / 描述符** | D3D12 资源绑定模型；SimpleMesh 封装在设备内。 |

## 原理

```text
CMake: HLSL --DXC--> triangle.vs.cso / triangle.ps.cso
main:
  PathResolver.AddRoot(ENGINE_SHADER_DIR_A)
  Resolve("triangle.vs.cso" / "triangle.ps.cso")
  SetupSimpleMesh(shaders)   // 创建设备侧 PSO + 网格
  每帧 DrawSimpleMesh()
```

1. **为何不运行时编译？** 产品路径把 DXC 留在构建/工具链，运行时确定性更好、启动更快。  
2. **纹理从哪来？** SimpleMesh 演示路径通常绑定默认或内嵌棋盘/示例纹；重点在「采样管线通了」。  
3. **失败模式**：缺 `.cso` → Resolve 失败 → 进程退出码非 0；这比黑屏更利于学习。

## 代码地图

| 符号 / 文件 | 说明 |
|---|---|
| `samples/learn/02_triangle/main.cpp` | 解析着色器路径并每帧 Draw |
| `PathResolver` | `engine/assets/path_resolver.h` |
| `SimpleMeshShaders` | `vs_dxil` / `ps_dxil` 路径 |
| `IDevice::SetupSimpleMesh` / `DrawSimpleMesh` | RHI 入口 |
| CMake `ENGINE_SHADER_DIR_A` | 编译输出目录宏 |

## 必做练习

1. 故意改错 `triangle.vs.cso` 文件名，确认日志与退出行为。
2. 打开 PIX/RenderDoc，抓一帧，指出 VS/PS 与 Draw 调用。
3. 对比本课与 `06_rhi_triangle`：谁更「教学封装」、谁更贴近完整 RHI。
4. （口头）说明为何 Sample 要依赖 CMake 编译着色器，而不是仓库提交二进制唯一真相。

## 常见坑

- **直接跑未构建的 exe**：旁路没有 `.cso` 会失败；先 `cmake --build ... --target sample_02_triangle`。
- **把 Vulkan SPIR-V 路径套到本课**：本课是 D3D12 DXIL SimpleMesh。
- **修改 HLSL 不重编**：必须触发 DXC 自定义命令，否则仍是旧 `.cso`。
- **与 01_clear 混淆**：01 可不加载着色器；02 的核心就是着色器产物。
