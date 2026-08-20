# render_engine

**W12–W20 已封板**（Win D3D12 + Vulkan 产品 Pass）。推荐体验：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release -R unit --output-on-failure
build\samples\Sandbox\Release\sample_sandbox.exe
```

**Sandbox**：默认质量 **Medium**；WASD/QE；鼠标观察；F1 Effects（SSAO/软影/VT→slot1 **默认关、opt-in**）；CSM/IBL/物理。

**测试**：`ctest -R unit` / `ctest -L headless`；`sample_sandbox.exe --headless --headless_frames 3`。

**水位**：见 [docs/DOING_UNDO_TODO.md](docs/DOING_UNDO_TODO.md)、[docs/PLAN.md](docs/PLAN.md)、[docs/VULKAN_PARITY.md](docs/VULKAN_PARITY.md)。  
W21：解冻 DLSS / FSR2 / MsQuic（可选 SDK）；不宣称 Nanite / 真 DDGI。

ImGui 路径：`third_party/imgui-v1.91.8`（`ENGINE_WITH_IMGUI`）。
