# Design

## Context

动机见 proposal.md。当前 `main.cpp` 拥有 GLFW/window，先创建空 `VulkanContext` 再初始化，主循环只有 `glfwWaitEvents`。Context 拥有 instance、messenger、surface 和 device，借用 physical device/queues；句柄均私有，没有设备访问接口，也没有 swapchain 扩展或 GPU 工作。

`queueSelection.cpp` 已提供同族优先、分族可用和 queue family 去重，继续复用。`vulkanContext.cpp` 的 `checkResult`/`enumerateProperties` 是内部 helper，后者处理 `VK_INCOMPLETE`；如呈现路径需要，提取到小型内部公共文件，而不是复制。CMake 已统一 C++20 和严格告警；既有 lifecycle 子进程测试及窗口脚本检查精确销毁序列，新增呈现资源后须同步调整，不能直接沿用旧的 device-first 断言。

现有主规格中 vulkan-context 的退出顺序未包含 device 子对象，project-onboarding 将交换链描述为未来阶段；本 change 的两份 MODIFIED delta 明确更新这两点。desktop-window 的可见、响应和退出契约保持有效，不新增近似窗口能力。

## Goals / Non-Goals

**Goals:**
- 主线程上建立单一、明确的 acquire/record/submit/present 时间线，支持两个可复用帧槽及实际数量的交换链图像。
- 用清晰所有权隔离应用和 Vulkan，保留已有初始化与失败回滚路径。
- 在 Vulkan 1.0 + KHR swapchain 基线上完成完整颜色附件清屏，支持同族与分族 queues。
- 能证明模式选择、连续提交、资源复用与窗口变化控制路径；通过 validation 及可用的 synchronization validation 验证。

**Non-Goals:**
- 不定义虚拟多后端接口、通用 buffer/texture/pipeline API 或资源 ID 管理器；只引入当前清屏生命周期需要的边界。
- 不要求 dynamic rendering、synchronization2、timeline semaphore、swapchain maintenance/present-wait 扩展，不提高核心 Vulkan 版本。
- 不增加渲染线程、shader、graphics pipeline、绘制、深度、MSAA、GPU 时间查询或指定帧率。

## Decisions

### 1. 最小 RHI 是具体门面，Vulkan 后端拥有 context 与呈现资源

新增 `rhi.h/.cpp`：对外是不可复制的具体 `Rhi`，提供 `initialize(windowHandle, config)`、`renderFrame()`、`shutdown()`；析构为 noexcept 回滚兜底。配置只包含有明确当前用途的 `ClearColor`，默认线性 RGBA `(0.05, 0.10, 0.20, 1.0)`，初始化检查有限性与 [0,1]。本次颜色在初始化时配置，不增加热更新、命令行或 UI 控件。

后端实现细节保持私有（可通过前置声明的 Vulkan 后端实现对象隔离头文件）；应用不包含 Vulkan 头，不操作 Vk 句柄。GLFW/window 仍由 main 所有，必须晚于 RHI 销毁。Vulkan 后端先初始化 context，再初始化 `VulkanPresenter`；presenter 借用 context 提供的只读 device/surface/physical device/queue/family 视图，不接管这些句柄，也不将其暴露给应用。

构造顺序保证 presenter 析构早于 context。显式 shutdown 能报告等待错误；异常展开用相同的幂等清理职责，析构只报告错误不抛异常。main 的错误文案扩展为可覆盖初始化与运行阶段的失败，失败返回非零。

替代方案：把帧循环塞进 main 会泄漏同步职责；将 context 变成全功能渲染器会混合基础设备与交换链生命周期；抽象基类和多后端工厂当前没有第二个实现需求。

### 2. 设备适用性与交换链配置共用真实查询及纯选择函数

在现有候选设备遍历中增加 device extension、surface formats/present modes/capabilities 查询。具备队列但缺少 `VK_KHR_swapchain`、颜色附件 usage 或可用清屏格式/模式的候选跳过，保留拒绝原因；逻辑设备明确启用此扩展。保持同族优先、分族可用和去重。

纯选择 helper 供实际创建与合成测试共用：
- 模式：MAILBOX 优先，否则 FIFO；不擅自改为 IMMEDIATE。
- 格式：优先 `B8G8R8A8_SRGB + SRGB_NONLINEAR`，再选择支持颜色附件的已公布格式；覆盖 surface 以单个 `VK_FORMAT_UNDEFINED` 表示应用可选的情况。最终格式须满足所需图像视图/颜色附件能力；颜色按所选格式编码，不手工重复 gamma 转换。
- extent：若 `currentExtent` 固定则使用它，否则取 GLFW framebuffer 像素尺寸并 clamp 到 min/max；零 framebuffer 先暂停，不通过 clamp 强行创建非零交换链。
- image count：请求 `minImageCount + 1` 并按非零 `maxImageCount` 截断；后续所有图像资源以 `vkGetSwapchainImagesKHR` 实际结果为准，不能假定恰好三张。
- usage 为 `COLOR_ATTACHMENT`，一个 array layer、clipped=true、当前支持的 transform；composite alpha 优先 OPAQUE，否则选公布的可用值。
- 同族用 EXCLUSIVE；分族用 CONCURRENT，family 列表复用去重结果。当前没有证据需要额外的所有权转移优化。

只在创建和重建时输出模式、格式、extent、实际图像数和重建原因，不每帧刷屏。真实查询失败报告 API/阶段/result，具有 VkResult 的枚举继续处理 `VK_INCOMPLETE`。

### 3. Render pass 的 CLEAR/STORE 表达整幅清屏

交换链只借用 VkImage；每图像拥有 image view、framebuffer 及 `renderFinishedSemaphore`。按当前格式创建单颜色附件 render pass，loadOp=CLEAR、storeOp=STORE，initialLayout=UNDEFINED（旧内容全部丢弃），subpass layout=COLOR_ATTACHMENT_OPTIMAL，finalLayout=PRESENT_SRC_KHR。录制 begin/end render pass，renderArea 覆盖整个 extent，clearValue 来自配置；无需 pipeline 或 draw 命令。

获取等待使用 COLOR_ATTACHMENT_OUTPUT；外部到 subpass 的依赖覆盖颜色阶段及颜色写入，最终布局转换纳入 render pass 完成并先于提交信号。不要使用 ALL_COMMANDS/GENERAL 代替精确依赖。保持获取、颜色写入和呈现之间的同步关系可审查，并用同步 validation 实测。

替代方案：transfer clear 需要额外 transfer usage 和显式布局转换，surface 不一定支持；dynamic rendering 会增加当前版本/扩展要求。传统 render pass 保持既有 API 1.0，且能为后续简单绘制沿用颜色附件配置。

### 4. 帧槽与交换链图像是两种不同的归属

固定两个 `FrameResources`，各拥有图形 family 的 command pool、一个 primary command buffer、一个 `imageAvailableSemaphore` 和初始 signaled 的 `submissionFence`。pool 不开单命令复位 flag，等待该槽完成后整体 reset。稳定运行不分配/释放这些对象。

每交换链图像的 `renderFinishedSemaphore` 用于 present 等待，按获取的 `imageIndex` 选取；不能按 `frameIndex` 选取。此前该图像的 present 使用经再次获取并在本次 submit 等待 acquire 信号后结束，再由本次 submit 重新 signal。帧 submission fence 只证明该帧命令及 acquire 等待已完成，不能证明 present 的等待已完成。无需再添加冗余的每图像 graphics fence：frame fence 保护帧命令，acquire 保护图像可用性。

正常时间线：
1. main 处理窗口事件，确认未关闭和非零 framebuffer。
2. 等待当前槽上一次成功提交的 fence；使用有界等待，超时时返回事件循环，保持槽状态不变。
3. 有界 `vkAcquireNextImageKHR` 获取 imageIndex。TIMEOUT/NOT_READY 无提交直接返回；OUT_OF_DATE 请求重建，保留 fence 的 signaled 状态。
4. SUCCESS/SUBOPTIMAL 已取得图像；reset command pool，录制当前图像完整清屏。SUBOPTIMAL 标记在结束本次获取生命周期后重建。
5. 录制完成后、即将实际 submit 时才 reset fence。submit 等待该槽 acquire semaphore（COLOR_ATTACHMENT_OUTPUT），signal 该图像 renderFinished semaphore，关联该槽 fence。
6. submit 成功才记录该槽 pending/帧计数并推进 frameIndex；present queue 等待该图像 renderFinished semaphore，呈现相同 imageIndex。
7. present 的 OUT_OF_DATE/SUBOPTIMAL 请求重建；其他错误终止正常循环。帧计数与测试记录区分 submitted/presented/跳过，不能将 submit 成功直接算成所有 present 成功。

录制/reset/submit 错误立即终止；特别是 reset fence 后 submit 失败，不在清理中等待该未提交的 fence。已经 acquire 后失败也不继续复用该槽的 acquire semaphore。统一退出只等待真实已提交工作，错误信息保留 VkResult。设备或 surface 丢失本次明确失败，不建立恢复重试体系。

替代方案：每帧 WaitIdle 会串行化设备并掩盖资源归属错误；按帧配置 present 信号量不能从 graphics fence 推导安全复用；逐帧创建同步对象没有必要。

### 5. 交换链重建复用创建路径，允许低频整体等待

main 活跃时使用 `glfwPollEvents` 并推进清屏；framebuffer 为零时使用 `glfwWaitEvents`，每次醒来先检查关闭，恢复非零后请求重建。缩放依据 framebuffer 像素尺寸，不能只用逻辑窗口大小。

第一次创建与后续重建共用查询/选择/创建步骤。重建暂停新提交，`vkDeviceWaitIdle` 完成已有 GPU 提交，清理旧 framebuffer、image view、present semaphore、render pass 和 swapchain，再用最新尺寸创建整套图像资源；本次选择简单的停顿重建（oldSwapchain=VK_NULL_HANDLE），不维护多个 retired generations。帧 command pools 先 reset 丢弃旧引用，帧槽同步状态回到已完成状态；不能用新图像数量去遍历旧资源。格式变化重建匹配的 render pass。重建初始化途中失败按已成功创建的资源回滚，退出而不继续使用残缺交换链。

这种低频等待用于重建和退出，稳定每帧路径禁止设备/队列 WaitIdle。基础 WSI 的呈现对象清理限制见下一节，它也适用于此简化重建。

### 6. 销毁顺序与基础 WSI 限制明确区分

正常显式 shutdown 先停止提交，检查 device idle 等待结果，然后释放呈现/帧资源，最后由 context 和 main 依次清理基础对象。概括顺序：完成 GPU 提交 → 复位/释放命令池（command buffer 随 pool 释放）→ framebuffer → image view → render pass → 同步对象 → swapchain → device → surface → messenger → instance → window → GLFW。各资源可能独立拥有，但其依赖必须保持；交换链 image 不调用 vkDestroyImage。部分初始化只清理已创建对象；等待失败仍报告并尽力清理，不将 device lost 宣称为成功退出。

本次保留 Vulkan 1.0 基线，采用基础 WSI 应用常用的 WaitIdle 清理路径。它保证已有 queue submissions 完成，但**不能从规范严格证明 presentation engine 已结束使用 present semaphore/swapchain**；validation 无报错也不能证明这个缺口不存在。本 change 的资源复用在连续呈现时有 acquire 同步依据，退出与简化重建的呈现清理则保留这一明确限制，须写入 ARCHITECTURE/STATUS，不能宣称完成了 presentation fence 保证。

可替代的严格呈现完成方案需要启用并验证 `VK_EXT_swapchain_maintenance1`（或对应 KHR 版本）的 feature/dependencies 和 present fence，或实现复杂的延迟回收；这会改变当前的兼容条件或资源管理范围，留给独立 change。若目标环境必须要求规范级 present 清理保证，应先通过 update 调整提案，而不是实施中静默提高最低要求。

参考依据（规划时已核对）：
- [Khronos: Swapchain Semaphore Reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)：按图像复用，及基础 WSI shutdown 的 WaitIdle 限制。
- [Khronos: Swapchain Recreation sample](https://docs.vulkan.org/samples/latest/samples/api/swapchain_recreation/README.html)：旧呈现资源回收的复杂性与 maintenance1。
- [Khronos: Swap chain recreation tutorial](https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/04_Swap_chain_recreation.html)：acquire OUT_OF_DATE 时提前 reset fence 的死锁，以及最小化期间检查关闭。

### 7. 验证生产步骤，合成条件不伪装成硬件能力

- 纯选择测试覆盖 MAILBOX、FIFO、缺模式、格式与 UNDEFINED、extent clamp、image count 上限=0、composite alpha、同族/分族 sharing；测试直接调用生产 helper。
- 仅测试编译目标扩展现有 checkpoint/hook，覆盖交换链/视图/framebuffer/帧同步部分初始化失败、重建中途失败、获取 OUT_OF_DATE/TIMEOUT、成功获取标记 SUBOPTIMAL、present 重建分支、录制和 submit 失败后的控制流。模拟返回值必须维护真实句柄/信号状态：不能把真实成功 acquire 改成“未获取”并遗弃 semaphore，也不能把真实成功 submit 假装未提交。
- 将实际 API 调用与结果分支保持少量清晰公共步骤，让注入只影响确实未执行的调用或已成功调用后的有效控制状态；不为测试建立完整虚拟 Vulkan 框架。对无法无损模拟的设备丢失，用控制状态测试/代码审查注明局限。
- 正常窗口脚本至少运行 10 秒，证明两个帧槽多次复用，观察客户区颜色（截图/人工确认或可靠像素采样，按实际格式编码判断），反复缩放，最小化恢复，并单独验证最小化期间关闭。摘要帧计数由成功操作累计，在退出输出；测试日志可记录 frameIndex/imageIndex/对象身份，但产品不逐帧输出。
- 保留现有初始化/队列/诊断测试。实际 Debug/Release 开启 validation，通过 SDK layer settings 在验收进程启用同步 validation，确认启用记录，保留完整 stdout/stderr；仅分类既有两条具体环境诊断，不忽略新错误。
- MAILBOX 实际设备与 FIFO 合成回退分别记录；若可在测试目标用公开支持的 FIFO 强制选择，再实际运行 FIFO 帧路径，明确这是测试 override，产品策略始终 MAILBOX 优先。分族若无实际硬件，报告 helper/创建参数与控制流验证，不能报实测。

## Risks / Trade-offs

- [基础 WSI 没有直接的 present 完成信号] → 保持帧循环的图像级安全复用；退出/重建的 WaitIdle 限制明确记录，不用 graphics fence 证明 present 完成；严格保证需要后续扩展 change。
- [缩放发生在能力查询与创建之间] → 处理创建/获取/呈现的实际结果，重新查询最新尺寸；重建失败报告实际进度，不继续提交旧或半成品资源。
- [图像数、顺序或格式变化] → 每次重建重新建立完整图像资源集合，分离 frameIndex/imageIndex，配置测试覆盖差异。
- [同族硬件无法证明分族实际执行] → 选择/sharing 参数测试及代码审查记录覆盖界限，后续有硬件再补实测。
- [静态清屏无法单凭截图证明连续帧] → 用成功 submit/present 计数及帧资源复用记录补充视觉证据。
- [sRGB 显示与线性颜色数值不同] → 文档给出线性 RGBA 与所选格式，验收采样考虑编码和颜色管理，避免硬编码不适用的屏幕字节值。

## Migration Plan

先补共享查询与设备适用性，再构建具体 RHI/交换链，接入清屏录制和同步循环，最后加入重建、退出与验证。README 等真实状态文档在实施后按实测更新；本次规划不修改产品代码或现有主规格。

使用新的构建目录按 README 完成 Debug/Release 配置、编译与 CTest，运行真实窗口场景及同步 validation，记录未覆盖硬件/工具。只有验收通过才标记任务并更新 STATUS、提交推送；归档另按用户请求处理。回滚应撤销本 change 的实现提交及对应文档，不改动此前已归档规格。
