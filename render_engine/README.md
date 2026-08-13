# render_engine

M1–M25 已具备可用主路径骨架；当前推荐体验：

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DENGINE_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug -L headless --output-on-failure
build\samples\Sandbox\Debug\sample_sandbox.exe
```

**Sandbox（可用）**：WASD/QE 移动、鼠标观察、Esc 退出；方向光 lit 立方体；物理箱掉落；`RenderSystem`+FrameGraph Opaque Pass。

**测试**：`ctest -R unit` / `ctest -L headless`（无窗口 Clear/Lit/Pump）。

仍待产品级加深：CSM GPU 阴影、完整 PBR 材质纹理、Vulkan Device、Jolt/ImGui/真 HTTP。

详见 [docs/PLAN.md](docs/PLAN.md)、[docs/TESTING.md](docs/TESTING.md)。
