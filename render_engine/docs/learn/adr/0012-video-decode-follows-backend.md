# ADR 0012: 视频解码跟随渲染后端；禁止跨后端/软解降级

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH18b, engine/media, backends/d3d12, backends/vulkan；配合 ADR 0020

## 背景

引擎需要将外部视频作为动态纹理绑到材质。视频帧须尽量留在 GPU、与**当前渲染 Device**一致，避免跨 API 拷贝与双栈。渲染后端已定为 Windows D3D12|Vulkan、Linux Vulkan，视频解码必须与之绑定，而非固定死在 D3D12VA。

## 决策

1. **解码路径跟随当前 RHI 后端**（运行时切换后端时，视频栈一并切换；不得混用）：  
   | 渲染后端 | 视频硬解 |
   |---|---|
   | **D3D12** | **D3D12VA**（共享同一 `ID3D12Device`） |
   | **Vulkan** | **Vulkan Video**（decode queue / Video Session，与同一 `VkDevice` 协作） |
2. **禁止降级**：不实现软件解码；不因失败自动切到另一图形 API 的 VA；不在 Vulkan 上调用 D3D12VA，反之亦然。  
3. 当前后端硬解能力不足或初始化失败 → **明确错误 / Feature=false**，业务占位或提示；禁止黑屏假成功。  
4. 上层 `VideoTexture` / `IVideoDecoder` 对业务隐藏后端；内部按 `IBackend` 选择实现。  
5. 能力查询：`Feature::VideoTexture`（或分项 `VideoTextureD3D12VA` / `VideoTextureVulkan`）；文档与 Sample 写清驱动/扩展前提。

## 备选方案

- 仅 D3D12VA、Vulkan 永不支持 —— 与 Linux/Vulkan 产品路径冲突。  
- 多解码后端 + 自动软解降级 —— 覆盖广，但零拷贝目标破坏、测试矩阵膨胀。  
- CPU 解码再上传 —— 带宽与延迟差，且违背本 ADR。

## 后果

- 优点：画面与解码同 Device；Win/Linux Vulkan 路径可播视频纹理。  
- 代价：两套硬解实现与驱动差异；Vulkan Video 扩展/驱动成熟度需在 M17/M18 验收中写明 SKIP/FAIL 规则。

## 学习提示

1. 「跟随后端」= 解码实现与 RHI 成对，不是业务自己选 VA。  
2. 「无降级」= 可诊断失败，不是崩溃；也不是静默换栈。  
3. 关注 YUV→可采样 RGB 的转换 Pass 与队列同步（D3D12 Fence / VK semaphore）。  
