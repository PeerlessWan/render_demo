# render_engine

**Windows（D3D12 + Vulkan）/ Linux（Vulkan）通用 2D·3D 渲染引擎**。

M1–**M25** 逻辑骨架与加深项已落地；真 GPU 全路径（PBR/Vulkan Device/Jolt/ImGui/MsQuic 等）仍可按 PLAN 继续加深。

## 编译与测试

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -R unit --output-on-failure
ctest --test-dir build -C Debug -L headless --output-on-failure
```

Headless：`ApplicationDesc.headless=true` 使用无窗口 + `CreateHeadlessDevice`（Clear/Compute/Readback/Present），适合 CI。

Samples：`01_clear` / `02_triangle` / `sample_sandbox`；工具：`ibl_baker` / `asset_cook`。

文档入口：[docs/PLAN.md](docs/PLAN.md) · [docs/TESTING.md](docs/TESTING.md) · [docs/HOSTING.md](docs/HOSTING.md)
