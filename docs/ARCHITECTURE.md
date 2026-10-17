# 当前架构

`main.cpp` 在主线程拥有 GLFW/window，使用 `Rhi` 的初始化、清屏帧和显式关闭接口。`rhi.h` 仅暴露颜色配置和生命周期，不暴露 Vulkan 句柄。具体 VulkanBackend 依次拥有 VulkanContext 与 VulkanPresenter，presenter 先析构，窗口晚于两者释放。Presenter 用不可复制的私有 SwapchainResources 集合拥有一代交换链、render pass、extent 和实际图像附件；unique_ptr 管理当前代及局部候选代，候选异常由同一释放函数清理。固定两个帧槽独立于代际资源。没有通用资源 ID、虚拟多后端接口或渲染线程。

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

窗口活跃时 poll events 并持续 renderFrame；Presenter 在初始化、帧入口和重建等待后共用窗口状态读取，关闭优先于呈现。framebuffer 为零或 Windows 窗口 iconified 时后端不提交，main 随后 wait events；恢复后继续呈现。surface 的有效 extent 单独为零时，后端一次 glfwWaitEventsTimeout(0.1) 后返回再复查。尺寸依据 framebuffer 像素而非窗口外框；同尺寸恢复仍保留待处理重建请求，没有请求则复用有效交换链。

缩放或异常状态触发低频重建：停止新提交 → device idle → reset旧命令引用 → 处理已到达事件并重读尺寸/关闭 → 查询当前 surface → 以未退役旧句柄作为 oldSwapchain 创建候选 → 创建全部图像附件 → 发布完整新代 → 清理旧代。初建 oldSwapchain 为 null。最小化或零 extent 延迟时不调用创建、不退役旧链，也不推进帧/代计数；完整发布才清除 rebuildRequested。稳定帧不调用 device/queue WaitIdle，不创建/销毁命令及同步对象。

传非空 oldSwapchain 的创建调用使旧链退役，即使新链创建失败也不能再从旧链 acquire 或再次把它作为未退役前代传入。本实现创建或候选附件失败即停止正常循环，候选 RAII 清理其已创建对象，关闭路径清理旧代；不回滚到旧链继续渲染。最多短暂拥有旧代与一个候选代，render pass 逐代重建，全部图像资源按新链实际图像数量配置。

关闭：完成真实已提交 GPU 工作 → 释放 command pools → framebuffer → views → render pass → present semaphores → swapchain → acquire semaphores/fences → device → surface → messenger → instance → window → GLFW。正常显式 shutdown 检查等待结果；失败也释放已拥有对象并报告非零，析构 noexcept 用同一清理路径兜底。资源句柄成功创建后才保存，释放后清空。

**基础 WSI 限制：** 当前 Vulkan 1.0 路径在退出和简化重建时采用常用 WaitIdle 清理；它证明 GPU queue submissions 完成，不能严格证明 presentation engine 已结束使用 semaphore/swapchain。validation 无错误也不能证明此缺口不存在。[Khronos 说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。未启用 maintenance1/present fence；规范级呈现完成保证需另行规划扩展及兼容要求。

生产路径不开启故障环境开关。仅测试编译目标保留原初始化 checkpoint，新增候选各资源阶段、逐代对象/old-new 句柄及窗口状态控制。替换创建失败注入发生于实际 API 调用前，只验证 host 退役控制与清理，不宣称驱动真实失败/退役；附件失败在真实成功创建后注入。窗口状态/surface 零 extent 控制仅影响测试采样，不将伪造配置提交给驱动。测试目标可用时通过 VK_EXT_layer_settings 在 instance 创建明确启用 validate_sync；实际产品窗口验收进程使用 SDK 的 VK_LAYER_VALIDATE_SYNC=1。不更改全局配置。

CTest 直接使用生产选择函数，逐次比较帧槽 command/acquire/fence 身份及按图像的 present semaphore，检查创建/释放数量、device 子资源先释放、故障阶段及退出结果。窗口脚本保存物理客户区截图、25点内部/边缘颜色采样、至少10秒连续运行、重复缩放、三轮最小化恢复/关闭、计数快照和原始诊断。截图几何与 GDI 采集在同一 per-monitor v2 线程上下文中完成，避免 PowerShell 线程切换及 DPI 虚拟化污染坐标，采集前留出桌面动画稳定时间。当前像素自动验证器支持 R8/B8 四种 UNORM/SRGB 格式，其他格式须截图人工核验。仅按两条具体既有 loader 消息归因，新应用诊断仍失败。

## 固定Slang三角形

RhiConfig显式选择内容，默认clearOnly，main选择triangle。共享recordFrame先按loadOp CLEAR清背景，三角形模式再设置当前extent的动态viewport/scissor、绑定graphics pipeline并vkCmdDraw(3,1,0,0)。固定顶点由Slang SV_VulkanVertexID生成，无buffer、descriptor、push constant、depth或额外submit/barrier；颜色附件和present依赖保持原路径。

每代SwapchainResources持有render pass、graphics pipeline和空pipeline layout；候选代全部创建成功才发布。两个局部shader module只活到pipeline创建完成（含异常退出），不在热路径加载。重建继续使用oldSwapchain和原等待/命令引用解除流程，pipeline/layout在对应render pass前释放；每次必要重建创建一个pipeline，稳定帧无pipeline创建或额外WaitIdle。格式变化通过新代render pass和pipeline兼容，extent通过动态状态更新。基础WSI呈现完成限制保持原约定。

shaderFile按exe位置定位shaders目录，以uint32_t存储读取的SPIR-V并核对长度、magic、1.0版本、bound和schema；这只是加载结构检查，不能替代spirv-val和驱动校验。构建产物经Slang GLSL中间后端生成，入口main与VkPipelineShaderStageCreateInfo::pName一致。仅清屏模式完全不加载这些资源。
