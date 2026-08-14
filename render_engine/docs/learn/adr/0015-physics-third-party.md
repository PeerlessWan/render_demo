# ADR 0015: 物理用第三方 + 抽象层

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH25, engine/physics；配合 ADR 0017

## 背景

通用渲染引擎常需碰撞、射线、刚体与角色控制器；自研求解器成本高且易错。

## 决策

1. 集成 **Jolt Physics**（或等价 PhysX）为**实现**；引擎侧仅 `IPhysicsWorld` 等抽象 API。  
2. 业务与其它子系统只依赖抽象；Jolt 头文件不得泄漏到公开 include。  
3. 能力：刚体、形状、触发器、Raycast/ShapeCast、Character Controller、DebugDraw；**薄 SoftBody/Cloth** 见 **ADR 0029** / C22。  
4. **不自研** 约束求解器；**不做**服装管线、破坏专用求解器、载具轮胎产品化。

## 后果

- 优点：稳定、可维护、与渲染解耦；可换 PhysX 而不改业务。  
- 代价：第三方许可与构建集成；API 受抽象模型约束。

## 学习提示

1. 物理权威 vs 渲染表现：谁写回 Transform 要约定清楚。  
2. 调试先画碰撞体，再查滤波层。  
