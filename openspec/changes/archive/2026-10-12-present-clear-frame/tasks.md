# Tasks

## 1. 交换链适用性与选择策略

- [x] 1.1 复用现有枚举/结果检查和队列选择职责，增加扩展与 surface 支持查询，跳过无法清屏呈现的候选并启用 VK_KHR_swapchain；以原队列/lifecycle 测试及仅测试目标中的缺扩展、缺 surface 能力场景验证诊断、非零退出和回滚。
- [x] 1.2 实现生产共用的交换链选择 helper，覆盖 MAILBOX/FIFO、格式与 UNDEFINED、framebuffer extent、图像数、alpha/transform 和同族/分族 sharing；用合成输入测试验证无 MAILBOX、上限=0、固定/可变 extent、缺配置及图像数边界。
- [x] 1.3 更新 README 的呈现设备条件与 ARCHITECTURE 的选择职责；核对文档与实际扩展启用/查询逻辑一致，保留尚未实施部分的真实状态。

## 2. 最小 RHI 与呈现资源生命周期

- [x] 2.1 建立具体 Rhi 和初始化清屏配置，后端复用 VulkanContext，只在内部借用设备视图，确保应用不操作 Vk 句柄；通过 CMake 严格编译、有效替代颜色配置和非有限/范围外颜色拒绝测试验证边界。
- [x] 2.2 创建 swapchain、实际图像列表、每图像 view/framebuffer/renderFinished semaphore 和匹配格式的单附件 CLEAR/STORE render pass；通过实际初始化日志及 validation 验证参数、数量、依赖和无独立销毁 swapchain image。
- [x] 2.3 创建两个可复用帧槽，各有 command pool、primary buffer、acquire semaphore 和初始 signaled fence，实现幂等资源回滚；用仅测试编译的阶段故障点验证图像资源/帧槽部分初始化的退出、资源计数及精确销毁顺序。
- [x] 2.4 将新模块加入应用和必要测试目标，更新 ARCHITECTURE 的 RHI/context/presenter 所有权与销毁时间线；严格构建并核对 presenter 先于 context、窗口最后释放，文档不包含未实现的通用 RHI 功能。

## 3. 持续清屏与帧资源复用

- [x] 3.1 实现按成功获取的 imageIndex 录制完整 framebuffer 清屏与最终呈现布局，使用颜色附件阶段同步；通过真实默认色和另一有效配置色的画面证据、validation 与可用的同步 validation 验证，无需 pipeline/shader。
- [x] 3.2 接入有界 fence/acquire 等待、record、submit 和 present，成功 submit 后才推进帧槽，按图像选择 present semaphore；运行跨越两个帧槽的多轮帧测试，记录实际 frameIndex/imageIndex 和对象复用，验证稳定路径不逐帧创建对象或调用 WaitIdle。
- [x] 3.3 main 改为活跃时处理事件并持续 renderFrame，输出准确的成功提交/呈现摘要；真实窗口无输入持续至少 10 秒，结合帧计数及客户区颜色确认持续运行，并验证移动及关闭响应。
- [x] 3.4 扩展测试目标验证 fence/acquire timeout 跳帧、图像索引与帧索引不同及录制/submit 失败控制；确保注入未执行的调用或保持真实成功调用的同步状态，检查错误退出不会等待未提交 fence 或继续复用已获取同步对象。
- [x] 3.5 更新 README 的颜色配置、模式日志与连续清屏运行步骤，ARCHITECTURE 记录 acquire→颜色写入→present 的同步时间线；实际按文档运行并核对日志与颜色配置位置。

## 4. 交换链重建与窗口状态

- [x] 4.1 首次创建和重建复用查询/选择/创建逻辑，实现 framebuffer 变化与 acquire/present OUT_OF_DATE/SUBOPTIMAL 分支；通过实际多次缩放及受控结果测试验证恢复清屏、未提交帧不 reset fence、成功 acquire 的 SUBOPTIMAL 信号被正确消费。
- [x] 4.2 实现零 framebuffer 暂停与事件等待，醒来先检查关闭；通过最小化→恢复后持续显示，以及单独在最小化期间关闭的实际窗口测试验证无忙循环、无死锁和正常退出。
- [x] 4.3 重建只在停止新提交并完成已有 GPU 工作后复位旧命令引用及释放旧资源，按最新图像数量/格式创建新集合；通过格式/图像数选择测试、重建中途故障注入及多轮缩放日志验证没有旧尺寸引用或半初始化资源的继续使用。
- [x] 4.4 扩展窗口验证脚本覆盖持续画面、重复缩放、最小化恢复及最小化关闭，保留原始诊断与颜色证据；运行脚本验证退出码、帧摘要和完整清理记录，并更新 README 的可重复验收命令及 ARCHITECTURE 的重建停顿策略。

## 5. 退出与异常处理

- [x] 5.1 完成显式 shutdown 与 noexcept 清理兜底：等待实际 GPU 提交，先清理呈现/帧子对象再 context/window，等待失败导致非零退出；用正常有提交关闭、阶段回滚与受控等待失败测试核对对象仅清理一次，未提交 fence 不进入完成等待。
- [x] 5.2 完成不可恢复的呈现、surface/device 丢失错误路径，保留阶段和 VkResult 并终止正常帧循环；用不会破坏真实同步状态的控制测试及代码审查验证无无限重试，记录无法真实注入设备丢失的覆盖界限。
- [x] 5.3 调整既有 lifecycle/diagnostic 子进程的入口与清理断言，确保产品不编译或读取故障开关；运行全部原有和新增 CTest，确认 early initialization 失败与诊断回调契约未回退。
- [x] 5.4 在 ARCHITECTURE 和 STATUS 明确基础 WSI WaitIdle 清理的规范限制与后续 maintenance 扩展方向；检查没有把 submission fence/validation 无错误写成 present 完成证明，不宣称已实施可选扩展。

## 6. 整体验收与交付

- [x] 6.1 在新的构建目录按 README 配置、编译 Debug/Release 并运行完整 CTest 和窗口脚本；保存持续至少 10 秒、缩放、最小化恢复/关闭及退出证据，核对正常退出为 0，未知应用 validation WARNING/ERROR 均无遗漏。
- [x] 6.2 在验收进程启用 SDK 支持的 synchronization validation，保存启用及运行日志；记录实际 MAILBOX/FIFO、图像数、帧槽复用、合成 FIFO/分族及故障路径覆盖，支持时增加仅测试目标的实际 FIFO 运行，明确无法覆盖的硬件/工具条件。
- [x] 6.3 核对 proposal、全部 delta、design 和验收证据，更新 STATUS/ROADMAP 及 change 的 verification.md；每个场景有实现与验证对应，任务仅在已验证后勾选，主规格在同步请求前保持原样。
- [x] 6.4 全部验收通过后核对差异、远程、main 和 upstream，只提交本 change 相关成果并推送已确认仓库；以提交 SHA、远程一致性和 GitHub 链接验证交付，保留其他工作区文件。

## Workflow follow-up

- 用户请求归档后，再同步 delta 到主规格并归档，复核主规格、完整归档记录及实际 GitHub 同步结果。
