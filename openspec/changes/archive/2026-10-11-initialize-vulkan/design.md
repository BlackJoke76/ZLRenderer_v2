# Design

## Context

参见 proposal.md 的动机和边界。现有 src/main.cpp 在主线程创建 GLFW_NO_API 窗口，以 unique_ptr 管理窗口，用独立 GlfwLifetime 终止 GLFW；空闲主循环使用 glfwWaitEvents。项目尚无 Vulkan 对象或设备选择逻辑，不能复用其他 checkout 的 renderer。

已有 desktop-window、project-onboarding 主规格继续适用。本机探测确认 SDK 1.4.341.1 的头文件、import library、validation layer 和 vulkaninfoSDK.exe 存在；loader 支持 1.4.341，枚举到 AMD Radeon RX 7900 XT。探测同时报告旧 AMD ICD JSON 路径、第三方 overlay 重复/版本诊断；这些是环境事实，不是本 change 的已完成验收。

## Goals / Non-Goals

**Goals:** 可阅读的初始化调用链；一个明确的 Vulkan 所有者；成功、失败共用清理；真实 validation 与测试证据覆盖生命周期。

**Non-Goals:** swapchain/MAILBOX、帧同步、command pools/buffers、资源分配器、GPU 提交、渲染、RHI 或 RenderGraph；不加入 GPU 打分框架，不改系统驱动/隐式 layer 设置，不顺带处理已归档 bootstrap-project 的 tag SHA 描述警告。

## Decisions

### 1. 构建与 API 基线

用 CMake find_package(Vulkan REQUIRED) 与 Vulkan::Vulkan 复用 SDK 的头文件和 Windows loader 链接，保留现有 MSVC x64 / C++20 目标。应用请求 Vulkan 1.0 核心 API 加本次必需的 surface 与 debug-utils 扩展；本次初始化不依赖更高版本核心能力，SDK 版本不等于 GPU 必须达到的版本。后续 feature 的版本需求由后续 change 显式升级。

相较自建动态 loader/引入 volk，本次链接 SDK 更直接，少一层依赖；loader 和驱动是 README 明确列出的运行前置条件。SDK 路径由环境与 CMake 查找，不硬编码本机路径。若 loader DLL 缺失，Windows 可能在进入 main 前拒绝启动，不能将此情形宣称为应用已执行的失败回滚。

### 2. 先检查，再创建 instance 和诊断对象

从 glfwGetRequiredInstanceExtensions 取得窗口 surface 所需 instance extensions，添加 VK_EXT_debug_utils，去重并核查支持。用 vkEnumerateInstanceLayerProperties 检查 VK_LAYER_KHRONOS_validation。Debug 和 Release 都启用 validation；本次没有静默 fallback 或关闭开关。

instance 的 pNext 链包含 debug messenger create info，捕获创建/销毁 instance 期间的诊断；成功后用 vkGetInstanceProcAddr 解析 debug-utils 创建/销毁函数并创建持续有效的 messenger。回调覆盖 GENERAL/VALIDATION/PERFORMANCE、WARNING/ERROR，记录严重级别、消息标识及文本，返回 VK_FALSE，且不向 C API 边界抛异常。回调所依赖的记录状态必须活到 instance 销毁完成，不能在清理 messenger 时先释放。

相较只在 instance 创建后注册回调，这能观察更早的错误。验证时用受控消息确认回调链路，并单独确认 layer 实际启用；合成消息不冒充真实 validation 检查。

### 3. 在真实 surface 上选择设备与队列

复用现有 GLFWwindow，由 glfwCreateWindowSurface 创建 VkSurfaceKHR。逐个枚举物理设备、非空队列族；检查 VK_QUEUE_GRAPHICS_BIT，用 vkGetPhysicalDeviceSurfaceSupportKHR 对当前 surface 查询呈现能力，检查查询 VkResult。

每个设备优先寻找同时满足 graphics/present 的同一队列族；找不到时接受分离的两个族。选择枚举顺序中的第一个合适设备，输出设备名称和两个索引；不额外偏好显卡品牌或要求独立显卡。队列选择的实际公共步骤提取为可用合成输入验证的小函数，无需模拟整个 Vulkan 驱动。

逻辑设备只为去重后的族各请求一个队列，优先级 1.0、flags=0；取得 queue index 0。此阶段不启用可选 GPU features、不要求或启用 VK_KHR_swapchain，不查询 formats/present modes。surface 呈现队列支持只是当前选择条件，后续 swapchain change 仍需检查自己的扩展与 surface 能力。

相较限定单一 graphics/present family，允许分离族避免无故拒绝有效设备；相较提前建设评分/多队列系统，满足当前真实需求即可。

### 4. Vulkan 生命周期单独拥有，窗口仍由 main 拥有

新增小型 VulkanContext 所有者（建议 src/vulkanContext.h/.cpp），拥有 instance、debug messenger、surface、logical device；physical device 与两种 queue 是借用句柄。禁用复制与移动，避免原始 Vulkan 句柄双重销毁。只为当前复用与选择测试提取必要 helper，不为每种 handle 建立通用框架。

main 先创建 GLFW/window，再构造空的、句柄均为 VK_NULL_HANDLE 的 RAII context，然后初始化；可用返回 unique_ptr 的工厂组织这一步。在持有者析构已有效的状态下创建各对象，阶段成功后才存入句柄；不能在仅含 raw handles 的构造函数里逐步初始化后抛异常并期待未完成对象的析构回滚。

初始化链：GLFW → window → 前置检查 → instance → messenger → surface → physical device / queue family selection → logical device → queues → 成功日志 → 现有事件循环。

错误包含失败阶段、API/缺失项及 VkResult；支持枚举 API 的 VK_INCOMPLETE 重试，不能把尚未完成的枚举当成有效设备清单。main 捕获错误并返回非零，context 析构不抛异常，已创建对象沿正常清理路径释放。

### 5. 明确正常与部分初始化销毁顺序

正常关闭：logical device → surface → debug messenger → instance → GLFWwindow → glfwTerminate。main 中 context 的生命周期在窗口所有者内结束；清理记录以具体对象角色区分每一步。device 与 surface 都依赖 instance，本项目约定 device 先清理，messenger 保留到 device/surface 清理结束。

| 对象 | 拥有者 | 释放动作 | 必须仍有效的依赖 |
|---|---|---|---|
| logical device | VulkanContext | vkDestroyDevice | instance；本次无提交中的 GPU 工作 |
| graphics/present queues | logical device，context 借用 | 随 device 释放，无独立 destroy | logical device |
| surface | VulkanContext | vkDestroySurfaceKHR | 创建它的 instance；本项目中窗口仍存在 |
| debug messenger | VulkanContext | vkDestroyDebugUtilsMessengerEXT | instance 和回调记录状态 |
| physical device | instance，context 借用 | 无独立 destroy | instance |
| instance | VulkanContext | vkDestroyInstance | 所有需显式释放的子对象已释放 |
| window | main 的 unique_ptr | glfwDestroyWindow | GLFW；VulkanContext 已销毁 |
| GLFW | main 的 GlfwLifetime | glfwTerminate | 窗口已销毁 |

失败时跳过未创建的对象，销毁后清空句柄，复用一个清理职责。对 instance、messenger、surface、logical device 成功后分别验证异常退出，无重复销毁和泄漏。本次无 GPU 提交，不添加 vkDeviceWaitIdle 或队列等待；将来提交 GPU 工作后必须显式扩展退出契约，不能直接复用无等待的假设。

### 6. 验证与文档

CTest 验证复用的选择函数：同族优先、分族接受、queueCount=0 排除、缺少 graphics/present 拒绝，以及实际用于 logical device 的族列表去重。集成验证覆盖 Debug/Release 的真实启动、移动/调整大小、系统关闭和退出码，日志应显示 validation 开启、设备/族索引及清理顺序。

只在测试目标或临时验证版本加入故障注入，覆盖缺少 layer/extension、无合适设备及每个已创建对象后的失败；不增加产品配置开关，不为测试引入通用 Vulkan backend。故障日志与清理记录须一起检查。正常流程的应用生命周期 WARNING/ERROR 必须解决；外部 loader/隐式 layer 环境诊断单独保留并说明，不能隐藏或直接称为应用错误。

README 增加 Vulkan SDK、loader、驱动和 validation 条件及失败诊断；ARCHITECTURE 写入上述调用链、所有权表和销毁顺序；ROADMAP 将初始化与未来 swapchain/首帧分开；STATUS 记录实际命令、设备、队列、日志、失败验证和 Git 同步证据。AGENTS.md 的 MAILBOX 约定继续保留，无呈现模式代码。

## Risks / Trade-offs

- [validation 强制开启导致缺 layer 的机器不能正常运行] → 当前是开发工程，README 明确要求；缺项精确报错，不谎报成功。
- [本机存在外部 loader/overlay 诊断] → apply 记录原始 baseline，归因具体消息；不自动修改注册表、驱动或全局 layer 环境，真实驱动阻塞需另行处理。
- [硬件只覆盖同族情形] → 用合成输入覆盖分族与无设备路径，真实硬件只报告实际走到的分支。
- [初始化异常遗留对象] → 先建立 RAII 持有者再初始化，逐阶段故障验证，messenger 活到其他子对象清理结束。
- [后续渲染误用无等待销毁假设] → 文档显式注明当前没有 GPU 提交，后续 GPU 工作需补同步与完成等待。

## Migration Plan

apply 中接入 Vulkan SDK 与 context 后按 README 从新构建目录验证 Debug/Release；通过规格、CTest 与真实 validation 验收后更新交接并独立提交/推送 main。本 change 不改 bootstrap-project 归档。若撤销，以新提交移除 context 调用与 Vulkan 链接，恢复已验证的空窗口流程，不重写远程历史。

## Sources

- [GLFW 3.4 Vulkan guide](https://www.glfw.org/docs/3.4/vulkan_guide.html)：所需 instance extensions、无图形 API 窗口、surface 创建及应用释放责任。
- [Vulkan validation overview](https://docs.vulkan.org/guide/latest/validation_overview.html)：validation layer 与诊断目的。
- [vkCreateDebugUtilsMessengerEXT](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateDebugUtilsMessengerEXT.html)：messenger 创建及回调配置。
- [vkGetPhysicalDeviceSurfaceSupportKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceSurfaceSupportKHR.html)：对指定 surface 的队列呈现支持查询。
- [vkGetDeviceQueue](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceQueue.html)：只获取创建设备时请求的队列。
- [vkDestroyDevice](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyDevice.html)、[vkDestroySurfaceKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroySurfaceKHR.html)、[vkDestroyInstance](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyInstance.html)：销毁接口与依赖约束。
