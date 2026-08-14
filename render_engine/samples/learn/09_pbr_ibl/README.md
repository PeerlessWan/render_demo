# Learn 09 — PBR + IBL

## 目标

设置 **`Environment` IBL 路径字段**（可为空）并渲染高金属立方体，理解 `has_ibl()` 与无 IBL 资产时的降级行为。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_09_pbr_ibl
build\samples\learn\09_pbr_ibl\Debug\sample_09_pbr_ibl.exe --headless --headless_frames=2
```

## 代码地图

| 字段 | 说明 |
|---|---|
| `ibl_irradiance` / `ibl_prefilter` / `ibl_brdf_lut` | IBL 三件套路径 |
| `Environment::has_ibl()` | 三者非空才为 true |
| `mesh_id = "metal"` | 高 metallic / 低 roughness 变体 |

## 必做练习

1. 用 `tools/ibl_baker` 生成 IBL 并填入三个路径，对比画面。
2. 只填 `ibl_irradiance` 观察 `has_ibl()` 仍为 false 的原因。
3. 调整 `metal` 的 roughness，对比环境反射锐度。

## 常见坑

- **空路径是合法配置**：本 sample 故意留空，依赖直射光 + 默认 ambient。
- **IBL 纹理格式**：须与引擎加载器约定一致（见 ibl_baker 文档）。
- **Headless**：不验证 IBL 采样，仅验证 DrawFrame 成功。
