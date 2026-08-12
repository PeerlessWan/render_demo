# 阶梯 Sample 规范

## 目录约定

```text
samples/
  Sandbox/                 # 产品验收（完整能力）
  learn/
    01_clear/
    02_triangle/
    03_texture_depth/
    04_lighting_cbv/
    05_upload_ring/
    06_rhi_triangle/
    07_scene_camera/
    07b_input_actions/
    08_material_variants/
    09_pbr_ibl/
    10_shadow_map/
    11_frame_graph/
    12_csm/                # 选修
    18_upscale/            # 选修
    18b_video_texture/     # 选修：随后端硬解（D3D12VA / Vulkan Video）
    18c_audio_playback/    # 选修：解码+输出，无特效
    29_ui/                 # 选修：ImGui + Retained HUD
    30_pixel_hybrid/       # 选修：Sprite/Tilemap/像素管线
    ...
```

每个 Sample 必须包含：

| 文件 | 要求 |
|---|---|
| `README.md` | 目标、怎么跑、代码地图、练习、常见坑（可链到 `docs/learn/chapters/`） |
| 入口源码 | 尽量短；复杂逻辑调用引擎，不复制一整份 D3D12 |
| （可选）`assets/` | 本章专用小资产；大资产放仓库 `assets/` 引用 |

## 设计约束

1. **一章一个认知目标**，不在 `01_clear` 里塞材质系统。  
2. **依赖单向**：高编号可依赖引擎已实现部分；不得要求学习者先懂选修章。  
3. **默认打开相关教学开关**（见 [README.md](README.md)），产品 Sandbox 默认关。  
4. **可失败得漂亮**：缺资产、无 DXR（可降级）、**当前后端无视频硬解（不可软解降级，必须明确报错/SKIP）** 时打印可诊断信息。  
5. **与章节文同步**：无 `docs/learn/chapters/CHXX_*.md` 时，Sample 内 README 先写最小版。

## Sample 与章节对照

见 [PATH.md](PATH.md) 表格。CMake 中建议 `option(ENGINE_BUILD_LEARN_SAMPLES "Build learn ladder samples" ON)`。

## 练习题存放

- 短练习：写在 Sample `README.md`  
- 参考答案（可选）：`samples/learn/NN_*/solutions/`（默认不加入教学编译，避免剧透；或单独 target）

## 验收（学习轨）

某章 Sample 算完成当且仅当：

1. 能编译运行并看到预期画面  
2. README 中「必做练习」作者本人走过一遍  
3. 对应章节的「你应能回答」可以口头讲清  
