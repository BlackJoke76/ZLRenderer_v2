# Spec Delta

## Purpose

定义 Windows 桌面窗口程序的最小运行契约，使开发者能够观察启动、事件响应和退出行为，并以一致的方式验证后续渲染功能所依赖的应用生命周期。

## ADDED Requirements

### Requirement: Visible window on startup

程序 SHALL 在受支持的 Windows 环境启动后显示一个带项目名称标题的桌面窗口。

#### Scenario: Successful startup
- **WHEN** 用户运行按 README 构建的可执行文件
- **THEN** 出现可见桌面窗口，且进程保持运行直到收到关闭请求

### Requirement: Responsive event loop

程序 SHALL 在主循环中处理窗口事件，使窗口在移动、调整大小和请求关闭时保持响应。

#### Scenario: Window interaction
- **WHEN** 用户移动窗口或调整窗口大小
- **THEN** 窗口响应操作，后续关闭请求仍可被处理

### Requirement: Orderly shutdown

程序 SHALL 在用户通过系统关闭按钮请求退出后结束主循环、释放窗口和窗口库资源，并以退出码 0 结束进程。

#### Scenario: User closes window
- **WHEN** 用户点击窗口关闭按钮
- **THEN** 窗口消失且进程正常退出，无残留程序进程

### Requirement: Startup failure is reported

程序 SHALL 在窗口系统初始化或窗口创建失败时输出可理解的错误，释放已成功创建的资源，并以非零退出码结束。

#### Scenario: Window creation fails
- **WHEN** 窗口系统初始化或窗口创建失败
- **THEN** 程序报告失败原因，且不进入主循环，以非零退出码结束
