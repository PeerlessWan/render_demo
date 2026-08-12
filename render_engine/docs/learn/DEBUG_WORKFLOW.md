# 调试工作流（引擎 + PIX / RenderDoc）

学习引擎时，**抓帧比看最终画面更重要**。

完整的产品向 **调试 / 调优 / 排错方法** 见：  
**[../DEBUG_TUNE_TROUBLESHOOT.md](../DEBUG_TUNE_TROUBLESHOOT.md)**（本文是其「工具抓帧」子集 + 学习节奏）。

## 1. 引擎内（先用）

| 手段 | 用途 |
|---|---|
| Debug 视图模式 | Albedo/Normal/Roughness/Cascade/RT 等，验证 GBuffer/材质 |
| DebugDraw | 视锥、AABB、光方向、级联盒、碰撞体 |
| 控制台 `r.*` | 开关 Bloom、质量档、教学开关 |
| ImGui 调试面板 | Profiler、Pass 列表（M8+） |
| `learn.show_pass_names` | 确认 FrameGraph Pass 是否按预期执行 |
| Readback / 截图 | 回归对比 |

## 2. PIX（推荐，D3D12）

建议每章至少抓一帧，看：

1. **命令列表顺序**是否与 FrameGraph 文档一致  
2. **资源屏障**是否在 RT↔SRV 等切换处出现  
3. **PSO / Root Signature** 是否每 Draw 异常频繁切换（变体过多？）  
4. **时长**：哪个 Pass 最贵（阴影？Bloom？）  

章节文的「建议看什么」应写出具体 Pass 名。

## 2b. Vulkan Validation / RenderDoc（M17+）

- Debug 构建开校验层；关注 **布局转换与同步** 报错。  
- RenderDoc 对 Vulkan 与 D3D12 均可抓帧；对照同一 Sample 的 `--backend=` 差异。

## 3. RenderDoc

适合看：

- 输入装配、VS/PS 调试（若驱动/配置允许）  
- 纹理内容、RT 附件  
- 管线状态对比两次 Draw 的差异  

与 PIX 可互补；教学文档不绑定单一工具。

## 4. 学习轨推荐节奏

```text
跑 Sample → 改一处练习 → 引擎 Debug 视图确认
         → PIX 抓帧看 Pass/屏障 → 写一句「我看到了什么」
```

排错时可对照主文档 **§4 症状表** 做二分。

## 5. 常见误解

| 误解 | 澄清 |
|---|---|
| 画面对了就学完了 | 可能状态错误但碰巧正确；要会看屏障与寿命 |
| 教学开关会让程序变慢 | 预期内；只在 learn sample 打开 |
| 只有厂商工具能学 DX12 | 引擎内统计是入门；工具是进阶 |

## 相关

- [../DEBUG_TUNE_TROUBLESHOOT.md](../DEBUG_TUNE_TROUBLESHOOT.md)  
- [PATH.md](PATH.md)  
- [README.md](README.md)  
