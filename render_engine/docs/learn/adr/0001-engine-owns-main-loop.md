# ADR 0001: 为何做引擎主循环而非纯 SDK

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH00, POSITIONING, engine/application

## 背景

纯 RHI/SDK 把主循环与场景留给调用方；本项目定位为可交付的通用 2D·3D 渲染引擎，需统一帧节奏、模块挂载与 Sandbox 验收。

## 决策

1. 引擎拥有 **Application 主循环** 与 Module 挂载点。  
2. 对外仍可嵌入，但默认产品形态是引擎驱动帧，而非「只给画三角形的库」。  
3. 细节见 [POSITIONING.md](../../POSITIONING.md)。

## 后果

- 优点：子系统协同、教学路径与验收一致。  
- 代价：嵌入场景需适配引擎生命周期。
