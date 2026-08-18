# CH37 — 演示级服装 / 披风裙摆（选修）

## 目标

理解 ADR 0037「演示挂接」与服装管线的边界；能跑通 `GarmentCloth` Verlet + 可选薄 SoftBody。

## 前提

CH25 物理；建议浏览 ADR 0029 / 0037。

## 原理

半页收口：程序化 Cape/Skirt → 钉点 → Verlet/胶囊 → 可选 `TryWirePhysicsSoftBody`。细节以 sample README 与 ADR 为准。

## 代码地图

- Sample：`samples/learn/37_clothing/`
- API：`engine/clothing/garment_cloth.h`
- 对照 [PATH.md](../PATH.md) CH37 行

## 练习

1. 跑通 `sample_37_clothing`，记录 SoftBody Ok 或 SKIP。  
2. 口头回答：为何本引擎不做 DCC 服装管线？

## 常见坑

把演示披风当成产品服装系统；以 Feature / 物理后端 / SKIP 为准。
