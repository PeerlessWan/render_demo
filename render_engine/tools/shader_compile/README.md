# tools/shader_compile — offline HLSL → DXIL (DXC)

M2 起，业务着色器经本工具链编译，禁止手改字节码进仓。

## 用法（CMake）

见 `cmake/ShaderCompile.cmake`：`engine_compile_hlsl(...)`。

手动示例：

```bat
dxc -T vs_6_0 -E VSMain -Fo triangle.vs.cso shaders\hlsl\triangle.hlsl
dxc -T ps_6_0 -E PSMain -Fo triangle.ps.cso shaders\hlsl\triangle.hlsl
```

Vulkan SPIR-V 目标在 M17 再开 `-spirv`。
