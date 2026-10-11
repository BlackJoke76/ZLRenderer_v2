# 当前架构

`CMakeLists.txt` 获取固定提交的 GLFW，构建单个 C++20 可执行目标 `zlRenderer`。
`src/main.cpp` 是唯一应用入口；窗口事件在主线程处理。

生命周期：错误回调 → GLFW 初始化 → 创建无图形 API 的窗口 → 等待事件、检查关闭标志 → 销毁窗口 → GLFW 终止。

窗口使用 `unique_ptr` 和 GLFW 销毁函数管理；先构造的 GLFW 生命周期对象在窗口之后释放，因此失败返回与正常返回共享清理路径。初始化失败不进入主循环，GLFW 自身处理失败初始化的清理；创建失败由生命周期对象终止 GLFW。两种失败均返回非零退出码。

目前没有 Renderer 类、GPU 资源或 swapchain。MAILBOX 优先约定留给后续 Vulkan 呈现实现。
