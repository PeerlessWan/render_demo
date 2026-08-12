# ADR 0020: Windows D3D12+Vulkan；Linux 仅 Vulkan

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH01、CH06、M17、M18；supersedes ADR 0002

## 背景

早期一期策略为「仅 D3D12」。产品目标扩展为 Windows/Linux 通用引擎后，需要第二图形 API：Vulkan 在 Windows 可与 D3D12 并存，在 Linux 为唯一合理主路径。D3D11/OpenGL/GLES 仍不实装，避免矩阵爆炸。

## 决策

1. **Windows**：实装 **D3D12** 与 **Vulkan**；运行时可选；默认 **D3D12**。  
2. **Linux**：仅实装 **Vulkan**（X11 必做窗口；Wayland 目标内）。  
3. **D3D11 / OpenGL / GLES**：工厂可注册类型，返回 `NotImplemented`。  
3b. **明确不做**：**macOS / 任何移动端 / Metal**（非延期）。  
4. **能力差须 QueryFeature 文档化**：  
   - 视频纹理：**跟随渲染后端**——D3D12→D3D12VA；Vulkan→Vulkan Video；**无软解、不跨 API 降级**。  
   - DXR：主路径 D3D12；Vulkan RT 可后置或暂 NotSupported。  
   - 超分：有 DLSS 用 DLSS；否则 **FSR/内置 fallback**（两后端均须能关超分或走 FSR）。  
5. 着色器：HLSL→DXIL（D3D12）；DXC→SPIR-V（Vulkan）；业务不直调后端 API。  
6. 里程碑：**M17** Windows Vulkan（含 Vulkan Video 视频纹理）；**M18** Linux + Vulkan（同上）。

## 备选方案

- 继续仅 D3D12 —— 无法覆盖 Linux，与跨平台定位冲突。  
- Windows 只 Vulkan —— 放弃 D3D12 原生调优与 DXR 便利；视频仍可用 Vulkan Video。  
- 同时实装 D3D11/GL —— 维护成本过高，收益低。

## 后果（优点 / 代价）

- 优点：Win/Linux 产品路径清晰；RHI 有第二实装；视频与后端成对。  
- 代价：双后端 + 双硬解维护；CI/黄金图矩阵增大；Vulkan Video 驱动差异需 SKIP/FAIL 约定。

## 学习提示

1. 「后端可选」不等于「能力等价」——先查 Feature（含视频扩展）。  
2. Linux 视频走 **Vulkan Video**，不是 D3D12VA。  
3. Validation Layer（VK）与 PIX（D3D12）是两条调试习惯。  
4. 新 Pass/特性默认两边落地，或写明「仅 D3D12」。
