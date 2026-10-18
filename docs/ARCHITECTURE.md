# 当前架构

## RenderGraph 核心

`renderGraph.h/.cpp` 提供不依赖 Vulkan/GLFW 的外部颜色图像声明、有序 raster pass、呈现出口和不可变编译计划。清屏是附件的 Clear load 行为，Load 表达保留内容的颜色附件读写，Store/Discard 决定结果是否可继续读取。图内 typed handle 不能跨图混用，不保存实际图像或 framebuffer；当前整图范围为单 mip/layer/sample，每个 pass 一个颜色附件，可声明 fragment 采样输入，反馈采样明确拒绝。

编译按声明顺序分析资源的最近写入者和其后的全部读取者，保留带生产者/消费者的 RAW/WAR/WAW 与布局变化关系，验证内容有效性。Clear+Store 建立内容，Discard 后不能 Load/采样/呈现；没有自动重排、裁剪或临时 GPU 图像分配。物理执行组当前一对一对应 raster pass，出口不额外成组，组前后转换引用逻辑图像；兼容读读不新增 barrier，后续覆盖仍等待所有读取者。单纯 WAR 表达执行依赖，布局变化另包含转换要求；逻辑依赖不是自动重排指令。CPU CTest 核对声明、错误拒绝、依赖和确定分组。

`vulkanRenderGraph.h/.cpp` 统一映射用途和验证局部绑定。颜色附件写/读写对应 COLOR_ATTACHMENT_OUTPUT 与颜色 WRITE/READ，fragment 采样对应 FRAGMENT_SHADER/SHADER_READ；采样只完成 CPU/参数验证，当前录制入口明确拒绝需要采样绘制的计划。组边界通过显式 image barrier 转换，render pass 的 initial/subpass/finalLayout 都保持 COLOR_ATTACHMENT_OPTIMAL；同一转换只有一处执行。Render pass 按实际使用的 Clear/Load 与 Store/Discard 组合缓存（最多四种），同格式 framebuffer 复用兼容 render pass，不逐帧创建。当前没有自动合并算法，未来合并必须保留组内内容语义并重新降低同步。

VulkanBackend 持有图和不可变计划，图身份与每次编译版本分开；计划只有逻辑引用。Presenter acquire 后构造当前代局部图像绑定，检查缺失、过期、格式/extent 不符及多个逻辑图像绑定同一实际图像；不支持别名。全部执行组录制进同一帧槽 primary buffer，一次 submit。测试目标的双清屏仅增加一条附件声明，与生产共用编译、映射和录制。

正常日志每个已执行代/计划版本首次提交输出 Graph plan/resource/dependency，以及从实际 barrier/begin/submit 输入采集的有界 Graph submitted/group/barrier 记录，present 返回后单独输出 request result。记录在提交成功后才发布；稳定帧不重复完整输出。测试目标额外采集前 12 帧。共享 checkRenderGraph.cmake 从实际参数核对顺序、绑定、WAW 和唯一呈现转换，颜色与同步 validation 作为另外两份证据，不能互相替代。

## 窗口与 Vulkan 生命周期

`main.cpp` 在主线程拥有 GLFW/window，使用 `Rhi` 的初始化、清屏帧和显式关闭接口。`rhi.h` 仅暴露颜色配置和生命周期，不暴露 Vulkan 句柄。具体 VulkanBackend 依次拥有逻辑图/计划、VulkanContext 与 VulkanPresenter，presenter 在其借用的计划和 context 之前析构，窗口晚于两者释放。Presenter 用不可复制的私有 SwapchainResources 集合拥有一代交换链、render pass 变体、extent 和实际图像附件；unique_ptr 管理当前代及局部候选代，候选异常由同一释放函数清理。固定两个帧槽独立于代际资源。图内 handle 不成为全局资源注册表，没有虚拟多后端接口或渲染线程。

初始化：GLFW → NO_API window → Rhi 配置检查与图编译 → context（instance、messenger、surface、physical/logical device、queues）→ 后端执行能力检查 → 两个帧槽 → swapchain/render pass/图像资源 → 主循环。零尺寸或最小化时延迟创建交换链；Debug/Release 均要求 validation。

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

默认线性清屏色为 `(0.05,0.10,0.20,1)`，通过 main 中 RhiConfig 改动；共用 clearColor.h 的检查拒绝非有限或 [0,1] 外分量。生产图声明单个颜色附件 CLEAR/STORE；显式获取边界 UNDEFINED→COLOR_ATTACHMENT_OPTIMAL，render pass 内保持该布局，最后显式转为 PRESENT_SRC_KHR。两次清屏之间的 WAW barrier 保持颜色附件布局，不重新退回 UNDEFINED。begin/end 覆盖整个 extent；没有 shader、pipeline 或 draw。

正常帧：处理事件 → 有界等待 frame fence → 有界 acquire 得到 imageIndex → 验证图绑定 → reset该槽pool，按计划发出组前 barrier、录制对应 framebuffer 并发出组后 barrier → 提交前reset fence → 单次 submit 等待该槽acquire信号（阶段来自图的首次实际使用，当前为 COLOR_ATTACHMENT_OUTPUT）、signal该图像present信号并关联fence → present等待该图像信号。显式边界同步覆盖颜色写入和布局转换。只在submit成功后推进frameIndex/提交计数并发布图执行记录。提交fence不能证明present等待已完成；3张图像和2个帧槽不能混用索引。

TIMEOUT/NOT_READY 跳帧；acquire OUT_OF_DATE 不reset fence，下次重建。成功获取的 SUBOPTIMAL 正常消费 acquire 信号、提交/呈现后再重建。present OUT_OF_DATE/SUBOPTIMAL 触发重建。录制/submit 失败终止正常循环；不会等待未提交的 fence。surface/device 丢失及其他不可恢复错误明确非零退出，不无限重试。

窗口活跃时 poll events 并持续 renderFrame；Presenter 在初始化、帧入口和重建等待后共用窗口状态读取，关闭优先于呈现。framebuffer 为零或 Windows 窗口 iconified 时后端不提交，main 随后 wait events；恢复后继续呈现。surface 的有效 extent 单独为零时，后端一次 glfwWaitEventsTimeout(0.1) 后返回再复查。尺寸依据 framebuffer 像素而非窗口外框；同尺寸恢复仍保留待处理重建请求，没有请求则复用有效交换链。

缩放或异常状态触发低频重建：停止新提交 → device idle → reset旧命令引用 → 处理已到达事件并重读尺寸/关闭 → 查询当前 surface → 以未退役旧句柄作为 oldSwapchain 创建候选 → 创建全部图像附件 → 发布完整新代 → 清理旧代。初建 oldSwapchain 为 null。最小化或零 extent 延迟时不调用创建、不退役旧链，也不推进帧/代计数；完整发布才清除 rebuildRequested。稳定帧不调用 device/queue WaitIdle，不创建/销毁命令及同步对象。

传非空 oldSwapchain 的创建调用使旧链退役，即使新链创建失败也不能再从旧链 acquire 或再次把它作为未退役前代传入。本实现创建或候选附件失败即停止正常循环，候选 RAII 清理其已创建对象，关闭路径清理旧代；不回滚到旧链继续渲染。最多短暂拥有旧代与一个候选代，render pass 逐代重建，全部图像资源按新链实际图像数量配置。

关闭：完成真实已提交 GPU 工作 → 释放 command pools → framebuffer → views → render pass → present semaphores → swapchain → acquire semaphores/fences → device → surface → messenger → instance → window → GLFW。正常显式 shutdown 检查等待结果；失败也释放已拥有对象并报告非零，析构 noexcept 用同一清理路径兜底。资源句柄成功创建后才保存，释放后清空。

**基础 WSI 限制：** 当前 Vulkan 1.0 路径在退出和简化重建时采用常用 WaitIdle 清理；它证明 GPU queue submissions 完成，不能严格证明 presentation engine 已结束使用 semaphore/swapchain。validation 无错误也不能证明此缺口不存在。[Khronos 说明](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。未启用 maintenance1/present fence；规范级呈现完成保证需另行规划扩展及兼容要求。

生产路径不开启故障环境开关。仅测试编译目标保留原初始化 checkpoint，新增候选各资源阶段、逐代对象/old-new 句柄及窗口状态控制。替换创建失败注入发生于实际 API 调用前，只验证 host 退役控制与清理，不宣称驱动真实失败/退役；附件失败在真实成功创建后注入。窗口状态/surface 零 extent 控制仅影响测试采样，不将伪造配置提交给驱动。测试目标可用时通过 VK_EXT_layer_settings 在 instance 创建明确启用 validate_sync；实际产品窗口验收进程使用 SDK 的 VK_LAYER_VALIDATE_SYNC=1。不更改全局配置。

CTest 直接使用生产选择函数，逐次比较帧槽 command/acquire/fence 身份及按图像的 present semaphore，检查创建/释放数量、device 子资源先释放、故障阶段及退出结果。窗口脚本保存物理客户区截图、25点内部/边缘颜色采样、至少10秒连续运行、重复缩放、三轮最小化恢复/关闭、计数快照和原始诊断。截图几何与 GDI 采集在同一 per-monitor v2 线程上下文中完成，避免 PowerShell 线程切换及 DPI 虚拟化污染坐标，采集前留出桌面动画稳定时间。当前像素自动验证器支持 R8/B8 四种 UNORM/SRGB 格式，其他格式须截图人工核验。仅按两条具体既有 loader 消息归因，新应用诊断仍失败。
