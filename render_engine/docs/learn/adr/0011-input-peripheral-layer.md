# ADR 0011: 外设接入层与窗口层分离；ActionMap 优先

- 状态: Accepted
- 日期: 2026-08-12
- 关联: CH07b, engine/input, engine/platform

## 背景

窗口消息（焦点、客户区坐标、DPI）与「键鼠/手柄/未来 XR」等外设能力耦合过紧时，业务会散落 `VK_*` / 原始报文，且难以热插拔与重绑。

## 决策

1. `platform` 只负责窗口与消息泵钩子。  
2. `input`（外设接入层）负责 DeviceHub、Adapters、StateStore、ActionMap。  
3. 相机控制器与玩法优先消费 **Action**，而不是硬件键码。  
4. 一期实装键鼠 + 手柄；其它外设以 `IInputAdapter` 预留。

## 备选方案

- 全部塞进 Win32 WndProc —— 扩展差，难测。  
- 直接依赖第三方完整输入中间件 —— 可后接，但一期保持自研薄层以便教学。

## 后果

- 优点：可测、可重绑、可插适配器；学习路径清晰。  
- 代价：多一层转发；手柄 API（XInput vs GameInput）需在适配器内收敛。

## 学习提示

1. 热插拔时 Action 绑定仍在，设备句柄可能变。  
2. 相对鼠标与绝对鼠标用途不同（Look vs UI）。  
3. 无焦点窗口时应抑制游戏向输入（可配置）。  
