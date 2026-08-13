# Jolt Physics（引擎 vendoring）

本目录为 [Jolt Physics](https://github.com/jrouwe/JoltPhysics) **v5.6.0** 源码树，供 `ENGINE_WITH_JOLT` 使用。

若缺失，可在 `render_engine/` 下执行：

```powershell
$env:HTTPS_PROXY="http://127.0.0.1:7897"
git clone --depth 1 --branch v5.6.0 https://github.com/jrouwe/JoltPhysics.git third_party/JoltPhysics
```

CMake 检测到 `Jolt/Jolt.h` 后会默认打开 `ENGINE_WITH_JOLT`。
