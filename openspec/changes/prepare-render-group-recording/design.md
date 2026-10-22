# Design

## Context

动机见 proposal.md。当前已核对的代码路径：

- `RenderGraph::compile` 为每个 raster pass 生成一个 `PhysicalPassGroup`，入口转换在 `beforeTransitions`，最终 present 转换放在最后一组 `afterTransitions`。本轮保留该编译策略。
- `VulkanPresenter::createFrameResources` 创建两个帧槽，各含一个 pool、一个 primary、acquire semaphore 和初始 signaled fence。
- `renderFrame` 负责窗口/重建检查、等待和 acquire、局部 `GraphImageBinding` 校验、录制、reset fence、submit、计数推进和 present。`recordGraphFrame` reset pool 后在单个 primary 中循环全部组。
- `mapGraphTransition`、`makeGraphAttachment`、`getGraphAcquireWaitStage` 与 `validateGraphBindings` 已是生产参数的公共来源，应复用。Render pass initial/final layout 固定为 COLOR_ATTACHMENT_OPTIMAL，不重复边界转换；load/store 变体在交换链代内缓存。
- `rebuildSwapchain` 等待既有提交并 reset 所有 pool 后替换交换链；清理先释放命令池再释放附件。基础 WSI 的 WaitIdle 呈现完成限制仍存在。
- `checkRenderGraph.cmake` 检查实际 barrier、组顺序和绑定，`verifyPresentation.cmake` 仍解析单个 command 身份，需要一起升级。`vulkanRenderGraphTests` 是 CPU 参数/校验覆盖，不是 GPU 执行证据。
- 主规格的 `Verify two clear passes on the GPU` 写明同一 command buffer，本 delta 完整替换该要求块中的此场景；其余呈现契约继续适用，不另建近似能力。

## Goals / Non-Goals

**Goals:**
- 使准备阶段输出当前帧可独立消费的录制输入，组录制不再读取可变交换链状态或承担提交职责。
- 用一套串行路径执行完整物理组，明确命令、附件、同步的所有权和提交成功边界。
- 以实际调用输入和像素分别证明录制结构及输出正确性。

**Non-Goals:**
- 不引入线程池、worker 编号、任务依赖、锁、secondary、逐组提交、额外收尾 primary。
- 不支持合并后的多逻辑 pass 渲染语义、计划运行时替换、额外外部图像分配或采样绘制。不预留通用调度框架。

## Decisions

### 1. 准备结果是本帧的值与借用句柄快照

建议使用 `PreparedGraphFrame` 与 `PreparedRenderGroup` 表达输入，放入现有 Vulkan 后端文件的最小合理范围。外层保留 slot、generation、acquiredImageIndex、planVersion、acquire wait stage；组项保留 groupIndex、目标 primary、解析后的 render pass/framebuffer/extent、clear/load/store 意图及已映射的 before/after barriers。对象只借用 Vulkan 句柄，不销毁图像或附件。

`prepareGraphFrame` 在成功 acquire 后完成全部绑定/支持校验和映射，为全部组生成准备记录。继续复用已有公共映射函数，避免诊断和执行各自重新解释计划。render pass 的代内缓存仍由 presenter 持有；准备阶段把准确句柄解析进记录。录制函数不得反查 `swapchainResources`，也不得调用 acquire/rebuild/reset fence/submit 或推进 presenter 状态。

组记录持有值或索引，避免保存指向可移动 vector 元素的指针；原生结构的 `pClearValues`、submit 数组等指针只在实际调用附近构造，保证其存储稳定且覆盖调用。准备结果只活到当帧提交/失败，重建前不得保留旧快照。计划是初始化时固定的，不增加跨帧布局 tracker 或热替换机制。

替代方案：只抽函数但继续隐式读 presenter 当前代，会保留生命周期耦合；复制整套资源所有权则产生双重销毁风险。因此只传本帧所需的借用句柄与值。

### 2. 一个帧槽命令池容纳全部组 primary

将 `FrameResources::commandBufferHandle` 改为 `groupCommandBufferHandles`，初始化时按固定计划组数一次分配 PRIMARY 数组。两个槽各有自己的 pool，串行录制无需每组 pool。组数非零及窄化到 Vulkan count 类型的有效性在分配前检查。

等待本槽 fence 成功且成功 acquire、完成准备后，统一 reset 本槽 pool 一次；逐组 `recordRenderGroup` 仅 begin、入口 barriers、完整 begin/end render pass、出口 barriers、end。组录制本身不 reset pool，避免后一组擦掉前组。完成状态由 begin/end 的成功返回证明，失败直接沿现有异常停止路径退出；不引入面向未来线程的结果容器或 worker 状态。

当前编译器严格一 pass 一组，录制支持校验应明确这个边界，不能将未来多 pass 组仅按首项清屏并把其余项日志当作已执行。将来合并需要单独设计组内语义。本轮 primary 归属物理组而不是逻辑 pass，单组不可跨 primary 拆分。

替代方案：每组 pool 增加对象且当前无并行需求；每帧 allocate/free 违反稳定资源复用；secondary 与本轮完整组 primary 目标不同。

### 3. 所有组成功后统一提交

在串行循环结束且全部 `vkEndCommandBuffer` 成功之后，统一提交函数构造一个 `VkSubmitInfo`。`commandBufferCount` 为组数，`pCommandBuffers` 直接引用按组序保存的稳定数组；一次 `vkQueueSubmit(..., 1, ..., submissionFence)`，不为组间增加 semaphore、submit 或 host wait。

wait 是本帧槽 imageAvailableSemaphore，阶段继续由图首次使用推导；signal 是 acquiredImageIndex 对应的 renderFinishedSemaphore。两组清屏保持第一组入口 undefined→color、第二组入口 color→color WAW、第二组出口 color→present。命令数组的顺序并不替代内存依赖；第二 primary 开头的现有显式 barrier 仍负责前组颜色写入到本组颜色写入的同步。只有出口所属组写入最后转换，不创建第三个 primary。

reset fence 紧邻真实提交且位于所有录制完成后。只有提交返回成功后设置 hasSubmittedWork、推进 submittedFrames/frameIndex、发布成功图记录并更新 reportedGeneration/reportedPlanVersion，然后按原规则 present。保持提交成功但 present 失败时已经推进的事实。

替代方案：逐组提交增加 CPU/队列开销并改变同步协议；以数组顺序代替 barrier 会丢失 WAW 义务；额外收尾 primary 则无必要且模糊组出口。

### 4. 失败与重建沿原生命周期收敛

| 阶段 | 本帧结果与清理规则 |
|---|---|
| fence/acquire 超时、NOT_READY/OUT_OF_DATE、暂停或关闭 | 不开始组录制、reset fence 或提交；依现有规则等待/重建 |
| 已成功 acquire 的 SUBOPTIMAL | 消费真实获取信号，执行全部组并统一提交/呈现，再重建 |
| 准备、pool reset、任意组 begin/组内/end 失败 | 无部分提交；不 reset 提交 fence；不推进计数/槽；报告阶段和组，非零退出，不重用该获取信号 |
| reset fence 或 submit 失败 | 停止正常循环，不等待此次未提交工作的 fence；清理仍等待此前真实已提交工作 |
| submit 成功后 present 失败 | 保留已提交状态，按现有可重建/不可恢复分类处理 |
| 初始化多 primary 分配或候选代创建失败 | 由已有所有权清理已创建对象，不回退已退役旧链 |

重建等待已提交 GPU 工作，再 reset 所有帧槽 pool，解除每个 primary 的旧附件引用，然后按现有 oldSwapchain 发布/清理规则替换。固定计划不改变组数，重建不重新分配 primary 或提交同步对象。销毁 pool 会一并释放全部 primary，清空宿主数组，保持先 pool 后 framebuffer/view/render pass 的释放顺序。

基础 WSI 不增加 present fence 或 maintenance1；WaitIdle 只作既有 GPU 工作完成证据，不补足呈现引擎完成保证。

### 5. 验收采集实际调用输入

保持每代/计划首次成功记录和测试前若干帧的有界策略。每组 begin/end/barrier/render pass 记录携带 groupIndex 和实际 commandBufferHandle；提交记录从真正 `VkSubmitInfo` 采集 count、有序句柄、wait/signal/fence，关联 slot、image、generation、version。失败日志携带阶段/组索引，与成功记录区分。

升级共享检查器核对：primary 数量=组数；组间句柄不同；数组顺序等于组序；每组 begin/end 成对；barrier 和完整渲染范围属于该组；最终转换唯一；数组在同槽跨帧/跨重建身份稳定。对检查器提供错序、重复/缺失句柄、数量不符、缺失出口等反例，防止只匹配固定 `submits=1` 摘要。

GPU 层运行单清屏和双清屏 A/B，以 25 点像素匹配 B 验证输出，validation/sync 证明范围内同步无应用诊断；原始参数证据独立验证完整组和单次实际调用。测试专用故障检查点覆盖前组已结束、后组 begin/组内/end 与 submit 失败；注入只证明对应 host 分支，明确不宣称驱动自然故障。测试目标的双组模式需可与后组失败及重建控制组合；不向产品增加控制开关。

## Risks / Trade-offs

- [多 primary 增加 begin/end 和驱动录制开销] → 保持完整组粒度及一次提交，不声称本轮提升性能；不设性能门槛或引入基准框架。
- [第二组清屏掩盖第一组漏执行] → 像素证据之外核对实际调用的完整第一组、独立句柄与 barrier，不靠最终颜色单独验收。
- [准备结构中悬空 span/原生指针] → 值语义、固定数组存储及调用时构造指针，禁止跨帧保留。
- [旧解析器默认单 primary 导致漏查] → 同步修改 graph 与 presentation/window 共用检查器，并用损坏记录验证拒绝能力。
- [故障路径等待 unsignaled fence 或提前销毁旧附件] → 后组失败/submit 失败检查计数及等待记录，重建检查全部 primary reset 在附件释放前。
- [真实设备不能覆盖 MAILBOX/分族/配置变化] → 保持纯选择/合成覆盖并明确与硬件记录分开，保留原始诊断过滤边界。

## Migration Plan

1. apply 时先建立准备结构和每槽 primary 数组，再切换单组录制与统一提交；共用现有映射/清理路径。
2. 升级调用证据与测试，执行 Debug/Release 全部回归，以及单/双清屏窗口、三轮最小化恢复、可见/最小化关闭和失败控制。按环境运行 ASan/静态分析，记录不可用工具，不把未运行项写成通过。
3. 验收后更新 ARCHITECTURE/STATUS，按项目规则独立提交并核实目标 remote/branch/upstream 后推送。主规格同步或归档仍由后续请求触发。
4. 若需回退，将本 change 的实现与相关测试一起回退，恢复单 primary 基线；不撤销既有 RenderGraph 或重建能力，不修改无关工作区内容。
