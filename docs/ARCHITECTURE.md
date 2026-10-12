# 当前架构

`main.cpp` 在主线程拥有 GLFW/window，使用 `Rhi` 的初始化、清屏帧和显式关闭接口。`rhi.h` 仅暴露颜色配置和生命周期，不暴露 Vulkan 句柄。具体 VulkanBackend 依次拥有 VulkanContext 与 VulkanPresenter，presenter 先析构，窗口晚于两者释放。没有通用资源 ID、虚拟多后端接口或渲染线程。

初始化：GLFW → NO_API window → Rhi 配置检查 → context（instance、messenger、surface、physical/logical device、queues）→ 两个帧槽 → swapchain/render pass/图像资源 → 主循环。零尺寸或最小化时延迟创建交换链；Debug/Release 均要求 validation。

设备选择复用 `queueSelection.cpp` 的同族优先、分族可用与去重。候选必须具有 `VK_KHR_swapchain`、当前 surface 的颜色附件 usage、兼容格式及 MAILBOX/FIFO。device 为每个族请求一个 index 0 队列，优先级 1；明确启用交换链扩展。`vulkanUtilities.h` 共用结果检查与 `VK_INCOMPLETE` 枚举重试，`swapchainSelection.cpp` 共用真实查询和纯配置选择。

交换链优先 MAILBOX，否则 FIFO；优先 B8G8R8A8_SRGB/SRGB_NONLINEAR，回退至受支持的颜色附件格式。单个 UNDEFINED 格式先扩展可选格式并查询实际颜色附件能力。extent 使用 framebuffer 像素尺寸及 surface 限制；图像数量请求 min+1，非零 max 才截断，并按实际返回图像数量分配。使用支持的 alpha/current transform；同族 EXCLUSIVE，分族 CONCURRENT。

| 资源 | 所有者及归属 | 完成与销毁条件 |
|---|---|---|
| command pool / primary buffer / acquire semaphore / submission fence | presenter，固定两个帧槽，按 frameIndex | 等待该槽 submission fence 后复位 pool 和重录；buffer 随 pool 释放 |
| framebuffer / image view / present semaphore | presenter，按 imageIndex | 命令完成后清理 view/framebuffer；present semaphore 按再次获取图像的 acquire 同步复用 |
| swapchain / render pass | presenter，每代交换链 | 重建、关闭停止提交并完成 GPU 工作后释放；格式变化重建 render pass |
| swapchain image | swapchain 所有，presenter 借用 | 不调用 vkDestroyImage |
| device / surface / messenger / instance | context | presenter 子对象先释放，之后按基础依赖顺序释放 |
| physical device / queues | instance/device 所有，内部借用 | 不独立 destroy |
| window / GLFW | main | Rhi 后依次释放 |

默认线性清屏色为 `(0.05,0.10,0.20,1)`，通过 main 中 RhiConfig 改动；初始化拒绝非有限或 [0,1] 外分量。render pass 的单个颜色附件 CLEAR/STORE，initialLayout=UNDEFINED（丢弃旧内容），subpass=COLOR_ATTACHMENT_OPTIMAL，finalLayout=PRESENT_SRC_KHR。begin/end render pass 覆盖整个 extent；没有 shader、pipeline 或 draw。

正常帧：处理事件 → 有界等待 frame fence → 有界 acquire 得到 imageIndex → reset该槽pool并录制对应framebuffer → 提交前reset fence → submit等待该槽acquire信号（COLOR_ATTACHMENT_OUTPUT）、signal该图像present信号并关联fence → present等待该图像信号。render pass外部依赖覆盖颜色输出和写入。只在submit成功后推进frameIndex/提交计数。提交fence不能证明present等待已完成；3张图像和2个帧槽不能混用索引。

TIMEOUT/NOT_READY 跳帧；acquire OUT_OF_DATE 不reset fence，下次重建。成功获取的 SUBOPTIMAL 正常消费 acquire 信号、提交/呈现后再重建。present OUT_OF_DATE/SUBOPTIMAL 触发重建。录制/submit 失败终止正常循环；不会等待未提交的 fence。surface/device 丢失及其他不可恢复错误明确非零退出，不无限重试。

窗口活跃时 poll events 并持续 renderFrame；framebuffer 为零或 Windows 窗口 iconified 时 wait events，醒来先检查关闭。恢复后继续呈现。缩放或异常状态触发低频重建：停止新提交 → device idle → reset旧命令引用 → 释放旧图像资源/交换链 → 重新查询并创建完整资源集合；失败只清理已创建对象并退出。稳定帧不调用 device/queue WaitIdle，不创建/销毁命令及同步对象。

关闭：完成真实已提交 GPU 工作 → 释放 command pools → framebuffer → views → render pass → present semaphores → swapchain → acquire semaphores/fences → device → surface → messenger → instance → window → GLFW。正常显式 shutdown 检查等待结果；失败也释放已拥有对象并报告非零，析构 noexcept 用同一清理路径兜底。资源句柄成功创建后才保存，释放后清空。

**基础 WSI 限制：** 当前 Vulkan 1.0 路径在退出和简化重建时采用常用 WaitIdle 清理；它证明 GPU queue submissions 完成，不能严格证明 presentation engine 已结束使用 semaphore/swapchain。validation 无错误也不能证明此缺口不存在。[Khronos 说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。未启用 maintenance1/present fence；规范级呈现完成保证需另行规划扩展及兼容要求。

生产路径不开启故障环境开关。仅测试编译目标保留原初始化 checkpoint，新增资源阶段及帧结果控制；注入未执行的 acquire/submit，或在实际成功 acquire/present/idle 后仅改变后续控制状态，避免伪装未提交工作。测试目标可用时通过 VK_EXT_layer_settings 在 instance 创建明确启用 validate_sync；实际产品窗口验收进程使用 SDK 的 VK_LAYER_VALIDATE_SYNC=1。不更改全局配置。

CTest 直接使用生产选择函数，逐次比较帧槽 command/acquire/fence 身份及按图像的 present semaphore，检查创建/释放数量、device 子资源先释放、故障阶段及退出结果。窗口脚本保存客户区截图、9点颜色采样、至少10秒连续运行、重复缩放、最小化恢复/关闭、帧摘要和原始诊断。当前像素自动验证器支持 R8/B8 四种 UNORM/SRGB 格式，其他格式须截图人工核验。仅按两条具体既有 loader 消息归因，新应用诊断仍失败。
