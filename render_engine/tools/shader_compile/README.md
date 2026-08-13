# tools/shader_compile — offline HLSL → DXIL (DXC)

M2 起，业务着色器经本工具链编译，禁止手改字节码进仓。

## 用法（CMake）

见 `cmake/ShaderCompile.cmake`：`engine_compile_hlsl(...)`。

手动示例：

```bat
dxc -T vs_6_0 -E VSMain -Fo triangle.vs.cso shaders\hlsl\triangle.hlsl
dxc -T ps_6_0 -E PSMain -Fo triangle.ps.cso shaders\hlsl\triangle.hlsl
```

Vulkan SPIR-V（`sample_01_clear --backend=vulkan --mesh`）:

```
dxc -T vs_6_0 -E VSMain -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout -Fo lit_cube_vk.vs.spv shaders\hlsl\lit_cube_vk.hlsl
dxc -T ps_6_0 -E PSMain -spirv -fspv-target-env=vulkan1.1 -fvk-use-dx-layout -Fo lit_cube_vk.ps.spv shaders\hlsl\lit_cube_vk.hlsl
```

CMake: `engine_compile_hlsl_spirv()` in `cmake/ShaderCompile.cmake`.
