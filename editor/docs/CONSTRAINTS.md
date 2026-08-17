# editor 约束

## 1. 依赖与边界

1. 只链 `render_engine` **公开 API**；禁止改引擎内部热路径充当「编辑器专用后门」。  
2. 可选依赖 `game_kit` **schema/公开头**；无 game_kit 时编辑器仍可编纯渲染场景。  
3. 工具 UI（ImGui 等）属 editor 进程依赖，不强迫进引擎 Runtime 发行包。  
4. 保存格式必须与 Runtime/cook **同一套**（[PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)）；禁止「编辑器私有格式只能编辑器读」。  
5. 只使用 [HOST_API.md](../../render_engine/docs/HOST_API.md) 公开面。  

## 2. 进程与寿命

1. 实现前锁定同进程或分进程（ARCHITECTURE §3）；文档与 ADR 一致。  
2. 遵守引擎生命周期：Stopping / GPUDrained；Play 退出必须释放运行时生成对象。  
3. 脚本（若启用）异常不得静默毁掉 Device；需捕获并提示。

## 3. 功能边界

1. 一期不做材质节点图、UMG 编辑器、完整动画工具。  
2. 不替代 DCC；高模/动画/贴图仍走外部 + cook。  
3. 不把 NavMesh/同步编辑默认塞进一期范围。  
4. 对标主流的「不做 / 后置」以 [GAPS.md](GAPS.md) §3–§4 为准，不把刻意不对齐当漏排。

## 4. 性能与质量

1. 编辑视口可用 Low/Med 质量档；不得要求编辑机跑满 High+RT 才能动。  
2. 大场景依赖引擎流式/LOD；编辑器不自造第二套资源系统。

## 5. 相关

- [GAPS.md](GAPS.md)  
- [ARCHITECTURE.md](ARCHITECTURE.md)  
- [../../render_engine/docs/HOSTING.md](../../render_engine/docs/HOSTING.md)  
- [../../render_engine/docs/HOST_API.md](../../render_engine/docs/HOST_API.md)  
- [../../render_engine/docs/PREFAB_SCHEMA.md](../../render_engine/docs/PREFAB_SCHEMA.md)  
- [../../render_engine/docs/RUNTIME_FOUNDATIONS.md](../../render_engine/docs/RUNTIME_FOUNDATIONS.md)  
