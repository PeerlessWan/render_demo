# tools/

离线 / 构建期工具（着色器编译、IBL / Lightmap 烘焙、纹理压缩、asset cook）。

规范与里程碑绑定见：**[docs/TOOLING.md](../render_engine/docs/TOOLING.md)**、ADR 0025。  
工作区分层：[../docs/LAYERS.md](../docs/LAYERS.md)。

实现随里程碑落地：`shader_compile`（**M2**）→ `ibl_baker`（M5）→ `lightmap_baker` / 压缩（M5–M8）→ `asset_cook`（M9 前）。

M1 清屏可不依赖本目录；见 [docs/GETTING_STARTED_M1.md](../render_engine/docs/GETTING_STARTED_M1.md)。

> 目录目标：随 M1 将本 `tools/` 迁入 `render_engine/tools/`（见 ARCHITECTURE）。
