# 阶梯 Sample 规范

> 每个 demo 的 `README.md` **必须**含完整教学块（2A）。全局术语可链 [GLOSSARY.md](GLOSSARY.md)、数学见 [BASICS.md](BASICS.md)。

## 目录约定

```text
samples/
  Sandbox/                 # 产品验收（完整能力）
  learn/
    01_clear/
    02_triangle/
    03_texture_depth/
    ...
```

每个 Sample 必须包含：

| 文件 | 要求 |
|---|---|
| `README.md` | **完整教学块**（见下模板）；可链 GLOSSARY / chapters |
| 入口源码 | 尽量短；复杂逻辑调用引擎，不复制一整份 D3D12 |
| （可选）`assets/` | 本章专用小资产 |

## README 完整教学块模板（2A，强制）

```markdown
# Learn NN — 标题

> 一句话目标。选修章在标题旁标注「选修」。

## 怎么跑

```powershell
cmake --build build --config Debug --target sample_NN_...
build\samples\learn\NN_...\Debug\sample_NN_....exe ...
```

## 知识点

1. …
2. …

## 名词解释

| 术语 | 含义 |
|---|---|
| … | … |

（可写：详见 [GLOSSARY.md](../../docs/learn/GLOSSARY.md)#锚点）

## 原理

数据流 / 管线 / 算法步骤（可用 mermaid 或有序列表）。
必须对应本 demo `main.cpp` 真实路径，不写未实现能力。

## 代码地图

| 符号 / 文件 | 说明 |
|---|---|
| … | … |

## 必做练习

1. …
2. …

## 常见坑

- …
```

篇幅建议约 **150–400 行中文**（含表格）；过短（仅「怎么跑+练习」）视为未完成。

## 设计约束

1. **一章一个认知目标**，不在 `01_clear` 里塞材质系统。  
2. **依赖单向**：高编号可依赖引擎已实现部分；不得要求学习者先懂选修章。  
3. **默认打开相关教学开关**（见 [README.md](README.md)），产品 Sandbox 默认关。  
4. **可失败得漂亮**：缺资产、无 DXR、当前后端无视频硬解时打印可诊断信息（不可静默软解）。  
5. **紧贴代码**：README 与 `main.cpp` 同步；能力以 Feature / Status 为准。

## Sample 与章节对照

见 [PATH.md](PATH.md)。CMake：`ENGINE_BUILD_LEARN_SAMPLES`。

## 练习题存放

- 短练习：写在 Sample `README.md`  
- 参考答案（可选）：`samples/learn/NN_*/solutions/`

## 验收（学习轨）

某章 Sample 算完成当且仅当：

1. 能编译运行并看到预期画面  
2. README 含完整教学块（知识点 / 名词解释 / 原理）  
3. 「必做练习」作者走过一遍  
4. 对应「你应能回答」可以口头讲清  
