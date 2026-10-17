# Design

## Context

动机与范围见 `proposal.md`，行为契约见 `specs/frame-presentation/spec.md`。本 change 需要设计文件，因为 `oldSwapchain` 重建涉及新旧两代同时存在、退役和异常清理，以及等待期间的窗口状态变化。

当前实际路径为 `main` → `Rhi::renderFrame` → `VulkanPresenter::renderFrame`。主循环已经对 framebuffer 为零及 `GLFW_ICONIFIED` 使用 `glfwWaitEvents`，恢复后继续调用后端；后端逐帧比较 framebuffer 与 `lastFramebufferExtent`，并处理 acquire/present 的 OUT_OF_DATE、SUBOPTIMAL。

`rebuildSwapchain(framebufferExtent)` 当前执行 GPU idle → reset 两个帧槽 command pool → `releaseSwapchain` → `createSwapchain`，所以创建信息的 `oldSwapchain` 始终为空。swapchain/render pass/extent 与 image resources 是 presenter 的独立成员，现有创建函数会直接覆盖这些成员，不能同时拥有旧代和候选代。初次创建与重建已共享函数，应保持复用。

测试已有选择函数、34 项 CTest、部分创建回滚和实际窗口脚本。脚本有三个尺寸变化与一次最小化恢复，尚未逐代记录 old/new 句柄及完整资源切换；presenter 的测试帧日志只记录最初 12 次提交，不能直接证明长窗口流程的暂停与恢复。

## Goals / Non-Goals

**Goals:**

- 在现有单线程调用链中完成 `oldSwapchain` 替换，使旧代、候选代和可用代的所有权及退役状态清楚。
- 重建的必要等待结束后复查事件/像素尺寸；不能呈现时返回或等待事件，不在后端循环等待恢复。
- 新代完整就绪后才恢复帧推进；故障时停止呈现并清理两代，固定帧槽保持原身份。
- 通过少量阶段/代际诊断与既有测试设施证明状态转换和资源生命周期。

**Non-Goals:**

- 不使用后台重建、多个待回收世代队列或无停顿重建，不保留旧图像继续呈现；重建仍可低频等待 GPU。
- 不利用 oldSwapchain 保留旧画面内容；本项目每帧仍 CLEAR/STORE。传入旧句柄允许驱动复用内部资源，不保证它必然复用。
- 不将此次局部资源分组扩展为通用资源管理或多后端接口；Rhi 无需暴露 Vulkan 句柄。
- 基础 WSI 清理限制沿用上一 change 的约定，严格呈现完成扩展仍另行规划。

## Decisions

### 1. 用私有的整代资源集合支持旧代与候选代

将当前 swapchain handle、render pass、extent、实际图像资源及代际标识集中到 presenter 私有的 `SwapchainResources`（或同等局部类型）中。frame command pool/buffer、acquire semaphore、submission fence 不属于该集合，保留两个帧槽。

共享创建函数接收明确的目标资源集合、所选配置及 `oldSwapchainHandle`；初次创建传空旧句柄，重建传当前未退役句柄。共享释放函数只清理指定集合，逐对象清空已释放句柄，借用的 swapchain image 不独立销毁。所有权禁止复制，转移后源集合必须为空；用局部 RAII/作用域清理保护候选代，避免原始句柄的默认 move 导致重复拥有。

render pass 每代重建，与当前实现一致，可正确适配格式变化。先不增加格式缓存，因为这里只有清屏、没有 pipeline 需要保留。资源分组是支持两代同时存在的实际需求；另一种方案是复制一组 old/new 成员和独立销毁函数，会重复创建/释放逻辑并难以保证失败清理。

### 2. 替换流程显式处理 oldSwapchain 的退役

一次有效替换按下列步骤执行：

1. 停止正常帧推进，等待真实已提交 GPU 工作完成；复位旧 command pool，解除旧 framebuffer/render pass 的命令引用，不重建帧槽。
2. 在主线程处理已到达事件，再读取关闭、iconified、framebuffer 像素尺寸并重新查询 surface。关闭立即返回主循环退出；窗口或有效 extent 为零时延迟，不调用创建，不退役旧链。
3. 旧资源集合保持拥有，使用 `VkSwapchainCreateInfoKHR::oldSwapchain = oldSwapchainHandle` 调用 `vkCreateSwapchainKHR`；调用后即将旧链视为退役。
4. 创建成功的句柄立即纳入候选代的清理范围，枚举实际图像，并创建候选 render pass、image view、framebuffer 和每图像 present semaphore。
5. 候选代全部成功后才发布为当前可用代，更新 framebuffer 快照及有效代计数，清除 `rebuildRequested`。按既有基础 WSI 清理策略释放旧代；后续 acquire/录制/submit/present 只引用新代。
6. 创建调用或后续资源创建失败，立即终止正常帧循环，清理候选代已成功创建对象及旧代，报告阶段并非零退出。不能回到旧链继续 acquire，也不能再次把已退役旧链作为未退役 oldSwapchain 传入。

这不是可回滚的资源事务：Vulkan 规定，传非空 oldSwapchain 的创建调用即使失败，也会使旧链退役。选择明确失败退出，符合既有不可恢复创建失败契约；不增加从无交换链自动重试的第二套流程。初次创建和候选代初始化仍复用同一公共步骤。

官方依据：[VkSwapchainCreateInfoKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkSwapchainCreateInfoKHR.html) 的 oldSwapchain/Description/有效使用约束。oldSwapchain 不替代旧 GPU 工作等待，也不证明呈现引擎已结束使用旧同步对象。

### 3. 保持尺寸轮询，收敛延迟重建和关闭路径

保持当前逐帧读取 framebuffer 的方案，不另设窗口事件分发器。必要的状态读取提取成用途明确的小步骤，避免初始化、帧入口与重建后复查对可呈现状态产生不同解释。framebuffer 是像素尺寸，不能用窗口外框或逻辑尺寸替代；见 [GLFW framebuffer 文档](https://www.glfw.org/docs/latest/window_guide.html#window_fbsize)。

`rebuildRequested` 只在完整新代发布后清除；延迟不复位 submission fence，不推进 frameIndex、提交/呈现计数或代计数。不因恢复到同样尺寸就清除先前 OUT_OF_DATE/SUBOPTIMAL 请求；若没有请求且现有配置仍有效，同尺寸恢复可以直接呈现。

私有重建步骤明确返回“完成或延迟”，`renderFrame` 必须检查结果后再访问图像。framebuffer 为零/iconified 的等待继续由 main 的事件循环负责；surface 限定零 extent 而 framebuffer 仍非零时，后端在主线程执行一次有界事件等待（例如 `glfwWaitEventsTimeout(0.1)`）后返回，下次重新处理状态，避免无限等待事件或热循环。关闭标志始终优先；不得在创建函数中使用 `while (size == 0)` 阻塞恢复。公共 Rhi 接口可以保持不变。

尺寸在查询和创建之间仍可能改变。下一帧的尺寸比较、真实 acquire/present 结果负责再次收敛，不要求捕捉每个中间尺寸，也不引入去抖计时器。创建 API 的实际失败按步骤 6 退出；不能把任意创建失败臆判为可重试 OUT_OF_DATE。

### 4. 保留现有 acquire/submit/present 同步，整代切换发生在帧外

无图像的 acquire OUT_OF_DATE 保留 fence 状态、请求重建并返回。成功 acquire 的 SUBOPTIMAL 必须消费真实 acquire 信号，完成当前 submit/present 后才进入下一次替换。已成功 submit 后的 present 重建结果也在下一帧处理；不在有未消费 acquire 状态的半帧中发布候选代。

稳定帧保持图像级 renderFinishedSemaphore、帧槽级 acquire/fence、COLOR_ATTACHMENT_OUTPUT 等待及 CLEAR/STORE。每图像信号量的正常复用依赖重新 acquire 的同步，不用 frameIndex 替代 imageIndex。重建不加每帧 idle，也不为 oldSwapchain 改成 graphics fence 证明 present 完成。

### 5. 证据以代际和状态转换为单位

生产日志只在创建、重建、暂停/恢复和退出阶段记录，不输出每帧句柄。记录代标识、输入 framebuffer、所选 extent/格式/模式/实际图像数、重建原因及提交/呈现计数快照。测试目标追加 old/new handle、退役、完整发布、逐对象创建/释放和帧所属代；保持原有资源日志可解析，必要时同步更新解析器。

CTest 扩展既有 `vulkanLifecycleTests`、hooks 和 `verifyPresentation.cmake`，验证初建空旧句柄、连续 old/new 链、旧链直到创建调用仍存活、无 retired acquire、跨代固定帧资源复用、旧对象只清理一次，以及候选 swapchain/render pass/view/framebuffer/present semaphore 各阶段失败。创建调用前的返回值注入只能证明失败控制与清理，不声称真实驱动执行了失败创建或退役；后续阶段用真实成功创建后的 checkpoint，保持真实句柄可销毁。不伪装真实成功提交为未提交。

窗口脚本复用 `MoveWindow`/客户区像素采样/系统关闭设施：增加快速尺寸序列、至少三次最小化恢复（含相同尺寸恢复）、独立最小化关闭；每个稳定尺寸等待相应代际日志与实际客户区匹配，再采样颜色，避免固定延时把旧尺寸截图当作新代成功。窗口不可见时不截图；暂停计数快照在暂停被应用观察到之后采集，允许此前在途工作完成，要求无新的 submit。恢复后计数继续增加；尺寸稳定且无 WSI 请求时代际计数不变。

逐代对象归属和清理不能只用全程 create/release 总数证明。测试结合代标识、句柄、调用顺序与帧记录，验证新代完整发布后才使用、旧代直到新链创建后才销毁。地址可能在已释放后复用，不用跨不同时期地址不同作为寿命证明。

## Risks / Trade-offs

- [传入旧链后不能回滚] → 保留新旧所有权直到清理完成；创建与附件失败均终止循环，专门验证不再 acquire 退役旧链。
- [两代短暂同时存在增加资源峰值] → 一次只保留旧代和一个候选代，同步切换后立即按既有策略清理，不维护历史代队列。
- [WaitIdle 不能严格证明呈现引擎完成] → 继续明示基础 WSI 限制；oldSwapchain 本身也不补足保证，不把 validation 无错误当作证明。依据见 [Khronos guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)。需要扩展支持才能解决的错误触发范围复议。
- [窗口在重建准备期间再次变化] → 等待后重新处理事件/读取尺寸，零 extent 延迟，关闭优先；后续实际 WSI 结果处理再次变化。
- [同族/FIFO 硬件无法验证所有配置] → 真实窗口记录实际模式；MAILBOX 优先/回退、图像数量及格式配置变化由选择函数测试补充，不向真实驱动提交伪造能力。
- [窗口截图遮挡或 DPI 不匹配] → 保留脚本已有 DPI 修正，先核对 framebuffer/客户区再采样；顺序运行验收，失败截图保留并记录原因。
- [GPU/驱动故障让退出等待不可控] → 5 秒关闭验收只针对正常设备条件；故障实际结果如实报告，不声称主线程可取消 `vkDeviceWaitIdle`。

## Migration Plan

先准备局部整代所有权与共享创建/释放，再实现 oldSwapchain 替换及延迟边界，随后扩展注入/窗口验证。只有验证通过的任务才勾选；满足现有行为的部分复用，不作无关重写。

使用独立构建目录完成 Debug/Release 构建和全部 CTest；两种产品配置顺序运行窗口流程及最小化关闭，并启用可用同步 validation。资源所有权变更还运行 MSVC ASan 和静态分析；若可用再运行 UBSan，无法运行时明确工具限制。保留完整日志和逐场景验证报告。

验收后更新真实架构、README、STATUS 和路线图，确认远程/分支/upstream，仅提交本 change 成果并按已授权规则推送。归档等待用户单独请求。回滚仅撤销此 change 的实施与文档，不影响原有归档或其他工作区文件；本次规划阶段只创建 change artifacts。
