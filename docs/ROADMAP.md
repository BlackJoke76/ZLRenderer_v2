# 路线图

1. `bootstrap-project`：Windows CMake/C++20/GLFW 工程、窗口生命周期、文档和 GitHub 交付。实际验收状态见 STATUS.md。
2. `initialize-vulkan`：现有窗口中的 instance、validation messenger、surface、物理/逻辑设备与 queues，正常及部分初始化销毁顺序。实际验收状态见 STATUS.md。
3. 后续待规划：独立 swapchain 与首帧呈现 change。遵守 AGENTS.md：支持 MAILBOX 时优先选择，否则回退 FIFO；检查设备扩展、surface 能力，验证模式选择、回退和窗口调整大小。
4. 后续待规划：明确帧资源生命周期与 GPU 同步，再逐步加入基础渲染；加入提交后须扩展退出时的完成等待契约。

第 3、4 阶段尚未实现；每个阶段需先形成独立 OpenSpec change 与验收标准，不因路线图而提前扩展当前工程。当前不选择呈现模式，不分配帧资源，不提交或等待 GPU 工作。
