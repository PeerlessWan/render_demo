# render_engine

**Windows（D3D12 + Vulkan）/ Linux（Vulkan）通用 2D·3D 渲染引擎**。

仅 D3D12 与 Vulkan；明确不做 macOS / 移动 / Metal。含物理、UI、2D/像素混合、网络、P2 缺口补齐（M20–M25）、最小工具链与教学层。

玩法 / 脚本 / 完整编辑器不在本引擎内实现。工作区分层见 [../docs/LAYERS.md](../docs/LAYERS.md)；外挂见同级 [`game_kit/`](../game_kit/)、[`genre_kits/`](../genre_kits/)、[`games/`](../games/)、[`editor/`](../editor/) 与 [docs/HOSTING.md](docs/HOSTING.md)。

## 文档

| 文档 | 用途 |
|---|---|
| [docs/README.md](docs/README.md) | **文档总索引** |
| [../docs/LAYERS.md](../docs/LAYERS.md) | **工作区分层（权威）** |
| [docs/PLAN.md](docs/PLAN.md) | 范围与里程碑 M1–M25（**M1–M19 骨架已标完成**） |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构与模块边界 |
| [docs/HOSTING.md](docs/HOSTING.md) | 玩法/脚本/编辑器外挂 |
| [docs/RUNTIME_FOUNDATIONS.md](docs/RUNTIME_FOUNDATIONS.md) | 资源/依赖/生命周期 |
| [docs/LINUX_VULKAN.md](docs/LINUX_VULKAN.md) | M18 Linux/Vulkan 占位说明 |
| [docs/TRANSPARENCY.md](docs/TRANSPARENCY.md) | M11 透明策略 |

## 编译运行（M1–M19 骨架）

在 `render_engine/` 下：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R unit --output-on-failure
```

Samples / tools：

```bat
build\samples\learn\01_clear\Debug\sample_01_clear.exe
build\samples\learn\02_triangle\Debug\sample_02_triangle.exe
build\samples\Sandbox\Debug\sample_sandbox.exe
build\tools\ibl_baker\Debug\ibl_baker.exe
build\tools\asset_cook\Debug\asset_cook.exe
```

里程碑水位：API/帧序/单测骨架已铺到 **M19**；GPU 全路径（真 PBR/CSM/ImGui/Vulkan/Jolt/MsQuic 等）仍为加深项，见 PLAN 各条「骨架」备注。

## 相关

- 工作区根：[../README.md](../README.md)  
- 工具占位：[../tools/README.md](../tools/README.md)  
