# 当前架构

`CMakeLists.txt` 获取固定版本的 GLFW，通过 `Vulkan::Vulkan` 链接 SDK，构建 C++20 应用 `zlRenderer`。`main.cpp` 在主线程拥有 GLFW 和窗口；`VulkanContext` 独占 Vulkan 对象，禁止复制/移动。队列选择的公共步骤在 `queueSelection.cpp`，由生产路径与合成输入测试共同使用。

初始化：GLFW → `GLFW_NO_API` 窗口 → 空 context → layer/extensions 检查 → instance → debug messenger → 窗口 surface → physical device / queue families → logical device → queue index 0 → 成功日志 → `glfwWaitEvents` 主循环。

选择枚举顺序中的第一个合适物理设备；非空队列族优先同族满足 graphics/present，否则接受分族。present 支持通过 `vkGetPhysicalDeviceSurfaceSupportKHR` 查询当前 surface。`getUniqueQueueFamilies` 的结果直接构造 device queue infos：每族一个队列、优先级 1.0、flags=0，不启用可选 features 或 swapchain 扩展。具有 VkResult 的枚举共用 helper 并重试 `VK_INCOMPLETE`，查询/创建失败包含阶段、API 与结果。

| 对象 | 所有者 | 销毁/有效依赖 |
|---|---|---|
| logical device | VulkanContext | `vkDestroyDevice`；instance 有效 |
| graphics/present queues | device，context 借用 | 随 device 释放，无独立 destroy |
| surface | VulkanContext | `vkDestroySurfaceKHR`；instance 与窗口有效 |
| debug messenger | VulkanContext | 扩展销毁入口；instance 有效 |
| physical device | instance，context 借用 | 无独立 destroy，随 instance 失效 |
| instance | VulkanContext | `vkDestroyInstance`；显式子对象先销毁 |
| window | main 的 unique_ptr | `glfwDestroyWindow`；GLFW 有效 |
| GLFW | main 的 GlfwLifetime | 窗口销毁后 `glfwTerminate` |

正常销毁：device → surface → debug messenger → instance → window → GLFW。先构造空 context，再逐步初始化；成功创建后才保存句柄。异常展开和正常退出共用 `cleanup()`，只释放非空句柄并清空，析构不抛异常；`main` 捕获失败并返回 1。GLFW 自身初始化失败时由 GLFW 清理；窗口创建失败仍由 GlfwLifetime 终止 GLFW。

Debug/Release 均要求 Khronos validation。instance 的 pNext 注册早期回调，持续 messenger 保留到 device/surface 清理完成。回调是 `noexcept` 的无状态函数，使用 C stdio 打印级别、消息标识和正文，返回 `VK_FALSE`；不依赖会提前析构的回调状态，函数和标准错误在 instance 销毁全过程有效。测试通过真实 `vkSubmitDebugUtilsMessageEXT` 证明消息传递，合成消息不代表真实 validation 错误。

测试目标复用 `main.cpp` 和 context，以仅测试编译宏启用故障点；产品目标不编译这些分支，也不读取测试环境变量。缺 layer/extension、instance/messenger/surface/device 后失败、无合适设备和模拟 device API 失败的子进程均验证退出码 1、无成功日志及精确清理序列。真实硬件只验证同族 0；分族由公共选择/去重函数的合成输入覆盖。

当前没有 swapchain、command buffers、GPU 提交或等待；无待完成 GPU 工作，因此清理不调用 `vkDeviceWaitIdle`。后续提交 GPU 工作必须补充完成与退出契约。MAILBOX 优先及 FIFO 回退留给后续 swapchain change。
