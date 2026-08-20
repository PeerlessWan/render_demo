# 导入开箱默认（W22）

桌面产品路径小约定，减少灰模感（非完整 Godot 导入器）：

1. glTF：优先带法线/ORM 时写入 `PbrMaterial`；缺贴图用 `base_color` + `roughness≈0.5`。  
2. UV：`uv_scale` 默认 1；地面类 mesh 可用 4（见 `ResolveMeshMaterial("ground")`）。  
3. 纹理：优先压缩 DDS/KTX（若有）；否则 stb PNG。  
4. Low 档：关 Cascade GI / 体积雾 / 软影（`QualitySettings::FromTier(Low)`）。

详见 [ADR 0045](learn/adr/0045-w22-godot-kernel-100.md)。
