# 路线图

1. `bootstrap-project`：Windows CMake/C++20/GLFW 工程、窗口生命周期、文档和 GitHub 交付。实际验收状态见 STATUS.md。
2. 后续待规划：Vulkan 实例、设备、surface、swapchain 与首帧呈现。遵守 AGENTS.md：支持 MAILBOX 时优先选择，否则回退 FIFO；验证模式选择、回退和窗口调整大小。
3. 后续待规划：明确帧资源生命周期与同步，再逐步加入基础渲染。

后续阶段均未实现；每个阶段需先形成独立 OpenSpec change 与验收标准，不因路线图而提前扩展当前工程。
