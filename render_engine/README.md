# render_engine

**Windows（D3D12 + Vulkan）/ Linux（Vulkan）通用 2D·3D 渲染引擎**。

仅 D3D12 与 Vulkan；明确不做 macOS / 移动 / Metal。含物理、UI、2D/像素混合、网络、P2 缺口补齐（M20–M25）、最小工具链与教学层。

玩法 / 脚本 / 完整编辑器不在本引擎内实现。工作区分层见 [../docs/LAYERS.md](../docs/LAYERS.md)；外挂见同级 [`game_kit/`](../game_kit/)、[`genre_kits/`](../genre_kits/)、[`games/`](../games/)、[`editor/`](../editor/) 与 [docs/HOSTING.md](docs/HOSTING.md)。

## 文档

| 文档 | 用途 |
|---|---|
| [docs/README.md](docs/README.md) | **文档总索引** |
| [../docs/LAYERS.md](../docs/LAYERS.md) | **工作区分层（权威）** |
| [docs/GETTING_STARTED_M1.md](docs/GETTING_STARTED_M1.md) | **M1 可执行清单** |
| [docs/PLAN.md](docs/PLAN.md) | 范围与里程碑 M1–M25 |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 架构与模块边界 |
| [docs/POSITIONING.md](docs/POSITIONING.md) | 定位与缺陷 |
| [docs/HOSTING.md](docs/HOSTING.md) | **玩法/脚本/编辑器外挂** |
| [docs/HOST_API.md](docs/HOST_API.md) | Host API v0 |
| [docs/PREFAB_SCHEMA.md](docs/PREFAB_SCHEMA.md) | Prefab/场景 schema |
| [../game_kit/docs/README.md](../game_kit/docs/README.md) | 通用玩法壳 + 脚本 |
| [../genre_kits/README.md](../genre_kits/README.md) · [../games/README.md](../games/README.md) | 品类层 / 游戏工程 |
| [../editor/docs/README.md](../editor/docs/README.md) | 编辑器规格 |
| [docs/RUNTIME_FOUNDATIONS.md](docs/RUNTIME_FOUNDATIONS.md) | 资源/依赖/生命周期/Profiling |
| [docs/TOOLING.md](docs/TOOLING.md) | 离线工具链 |
| [docs/STANDARDS.md](docs/STANDARDS.md) | 工程规范 |
| [docs/TESTING.md](docs/TESTING.md) | 测试策略 |
| [docs/learn/README.md](docs/learn/README.md) | 教学封装 |

## 编译运行（实现后）

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R unit --output-on-failure
```

细节见 [docs/GETTING_STARTED_M1.md](docs/GETTING_STARTED_M1.md)。

## 相关

- 工作区根：[../README.md](../README.md)  
- 工作区文档索引：[../docs/README.md](../docs/README.md)  
- 工具占位（暂在工作区根）：[../tools/README.md](../tools/README.md)  
