# Proposal

## Why

现有程序只有 GLFW 空窗口；要继续建设 renderer，需要先建立可诊断、可回滚且生命周期明确的 Vulkan 初始化基础。本 change 将验证 instance、surface、设备及 validation 的连接与退出，不提前引入帧渲染。

## What Changes

- 为现有 Windows CMake/C++20 工程接入 Vulkan SDK 与 loader。
- 在现有窗口上建立 instance 和 surface，选择满足图形与该 surface 呈现支持的物理设备，创建逻辑设备并获取相应 queues。
- 启用 Khronos validation 和诊断回调；Debug、Release 均要求 validation 可用，缺失时明确报错，不能静默降级。
- 输出初始化成功、所选设备与 queue family，以及清理阶段信息，便于验收。
- 正常退出或任意初始化阶段失败时，仅释放已创建对象，保证依赖对象先销毁，并明确记录对象所有权和销毁顺序。
- 更新 README 的 SDK 与运行条件、ARCHITECTURE 的初始化和清理时序、ROADMAP 和 STATUS 的实际进度。
- 范围不包含 swapchain、MAILBOX 选择、command buffers、GPU 提交、图像资源或首帧渲染；沿用当前空窗口事件循环。

## Capabilities

### New Capabilities

- `vulkan-context`: 现有窗口关联的 Vulkan 环境初始化、可用设备选择、validation 诊断、正常退出及部分初始化失败的回滚。

### Modified Capabilities

无。`desktop-window` 的窗口交互和退出契约继续适用；`project-onboarding` 已要求文档随环境与状态更新，无需重复新增其要求。

## Impact

修改 CMakeLists.txt 和 src/main.cpp，新增少量 Vulkan 生命周期与设备选择源文件、必要的验证，以及相关文档；不修改 bootstrap-project 归档。构建需要 Vulkan SDK，运行需要 loader、兼容驱动、支持现有 surface 的设备以及 validation layer。验收通过后独立提交并推送到既有仓库 BlackJoke76/ZLRenderer_v2 的 main，推送前核实 remote/upstream。
