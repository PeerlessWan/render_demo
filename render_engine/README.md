# render_engine

M1–M25 已具备可用主路径骨架；当前推荐体验：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -L headless --output-on-failure
build\samples\Sandbox\Debug\sample_sandbox.exe
```

**Sandbox（产品可用）**：WASD/QE；鼠标观察；**Dear ImGui 特效面板**（F1；字体+控件）；CSM/albedo/点光；物理。

**测试**：`ctest -R unit` / `ctest -L headless`；`sample_sandbox.exe --headless --headless_frames 3`。

仍待加深：点光 GPU Atlas、文件纹理/IBL、真 SSAO/TAA、RmlUi HUD、Vulkan Device。

详见 [docs/PLAN.md](docs/PLAN.md)、[docs/DOING_UNDO_TODO.md](docs/DOING_UNDO_TODO.md)、[docs/TESTING.md](docs/TESTING.md)。

ImGui 路径：`third_party/imgui-v1.91.8`（`ENGINE_WITH_IMGUI`）。代理拉取示例：`git -c http.proxy=http://127.0.0.1:7897 clone ...`。
