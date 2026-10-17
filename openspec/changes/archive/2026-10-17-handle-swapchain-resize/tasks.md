# Tasks

只有本项实现及对应验证完成后才勾选。复用现有功能与测试；发现已满足的项目时，以当前验证证据完成，不为了勾选而重写代码。

## 1. 基于 oldSwapchain 的整代替换

- [x] 1.1 在 VulkanPresenter 中分组每代交换链资源，复用有明确目标集合的创建/释放步骤，加入候选代作用域清理与唯一所有权转移；验证 Debug 构建及既有初建、资源回滚、normal/fifo 测试通过，固定帧资源未迁入代际集合。
- [x] 1.2 调整重建为等待旧 GPU 工作并解除命令引用 → 传未退役 oldSwapchain 创建候选 → 完成附件 → 发布新代 → 清理旧代；验证真实重建测试记录初建旧句柄为空、替换旧句柄等于前代、创建前旧链未销毁、新代完整后才提交、每代旧对象只清理一次。
- [x] 1.3 补充旧链退役后的失败退出，覆盖新交换链创建失败控制及候选 swapchain/render pass/image view/framebuffer/present semaphore 各阶段异常；验证对应 CTest 非零退出、无 retired acquire/再次传入已退役旧句柄、两代已创建对象清理平衡且无未知 validation 诊断，报告返回值注入与真实创建的区别。
- [x] 1.4 扩展既有 hooks/verifyPresentation.cmake 的代际及帧身份检查，并仅注册直接相关 CTest；验证连续两次以上重建的 old/new 链、实际图像级资源、跨代两个帧槽 command/acquire/fence 身份复用、图像数/格式配置变化的合成选择与附件创建代码核查通过。
- [x] 1.5 更新 docs/ARCHITECTURE.md 的局部所有权、替换次序及失败处理，保留基础 WSI 限制；验证说明逐步对应实际创建/发布/释放函数，明确 oldSwapchain 不构成失败回滚或呈现完成保证。

## 2. 尺寸、暂停、恢复及关闭边界

- [x] 2.1 在初始化、帧入口及重建准备结束处一致判定窗口关闭、iconified、framebuffer 像素尺寸，重建等待后处理事件并复查；验证测试目标可控地产生等待后再次缩放、最小化及关闭，新尺寸生效、延迟不创建/提交、关闭正常退出。
- [x] 2.2 对所选 surface extent 为零进行可恢复延迟，重建返回延迟时不得访问候选图像，surface 单独不可呈现时使用有界事件等待；验证选择测试覆盖固定/可变零 extent 与单维为零，控制测试无零尺寸创建、无热循环，恢复后可创建并呈现。
- [x] 2.3 保留延迟期间的 rebuildRequested、有效代与帧槽状态，按完整发布清除请求；验证“请求重建 → 暂停 → 同尺寸/异尺寸恢复”、重复暂停以及初始最小化/零 framebuffer 后恢复和恢复前关闭，跳过阶段不 reset submission fence、不推进帧计数。
- [x] 2.4 将上述边界加入现有 CTest/受控状态验证，保持 acquire/present OUT_OF_DATE、SUBOPTIMAL、timeout、record/submit 失败回归；验证成功获取的 SUBOPTIMAL 信号实际消费、无图像 OUT_OF_DATE 后仍可推进，无不可恢复错误的无限重试，并记录各场景覆盖方式。
- [x] 2.5 在 README 与架构文档说明实际尺寸来源、同尺寸恢复、延迟和关闭行为；验证文档与上述控制测试一致，公共 Rhi 不泄露 Vulkan 资源，稳定帧未加入全设备等待或逐帧创建。

## 3. 可重复的实际窗口验收

- [x] 3.1 添加低频代际/状态诊断与提交计数快照，扩展 verifyWindow.ps1 按当前客户区像素尺寸等待对应 framebuffer/extent 日志后截图采样；验证每个稳定尺寸均有匹配证据，原有日志解析仍可用，生产程序无逐帧句柄日志或测试环境开关。
- [x] 3.2 扩展同一脚本的快速缩放、至少三次最小化/恢复（含同尺寸恢复）及独立最小化关闭流程；验证暂停观察后无新增 submit，恢复后计数增长，尺寸稳定且无重建结果时代际不增长，可见/最小化关闭均在正常设备条件下 5 秒内退出 0，截图与原始日志留存。
- [x] 3.3 更新 README 的脚本命令、证据位置与环境要求；验证命令能按原文运行，明确 MAILBOX/FIFO 实际模式、同步 validation 启用方式、截图遮挡/DPI 和非支持颜色编码限制，不扩大 knownEnvironmentDiagnostics 过滤范围。

## 4. 整体验收与交付

- [x] 4.1 用独立构建目录执行 cmake 配置、Debug/Release 构建及全部 CTest；验证两种配置全部通过、无编译警告，新旧测试结果与完整 stdout/stderr 有保存路径。
- [x] 4.2 顺序对 Debug/Release 产品运行完整窗口流程及独立最小化关闭；验证至少 10 秒持续清屏、逐尺寸/代际与恢复证据、资源平衡、退出 0，基础 validation 和可用同步 validation 无应用 WARNING/ERROR，并据实列明真实与合成覆盖。
- [x] 4.3 对此次所有权与失败清理变更运行 MSVC ASan 的相关 CTest/窗口流程及静态分析；验证无主机内存问题/分析警告，可用时运行 UBSan，无可用工具时明确记录限制而不宣称通过。
- [x] 4.4 写入本 change 的 verification.md，对全部 delta 场景逐项关联代码、测试和日志；验证 oldSwapchain 参数链、失败退役、暂停/恢复与基础 WSI 限制均有明确结论，执行 openspec validate handle-swapchain-resize --strict 与 git diff --check 通过。
- [x] 4.5 根据实际验收更新 docs/STATUS.md、ROADMAP 与任务勾选；验证交接记录能还原本 change 的完成项、验证命令、证据路径、实际模式和未覆盖条件，无未经验证的完成声明。
- [x] 4.6 核对当前 remote、branch 和 upstream，仅暂存本 change 相关成果，使用含 handle-swapchain-resize 的独立提交并推送；验证暂存差异与验收一致、提交 SHA 及远程同步结果，报告目标分支和可核实的 GitHub 提交链接，保留其他工作区改动。

## Workflow follow-up

- 本 change 已由用户的新实施请求进入 apply；主规格同步与归档仍等待另行请求。
- 若需提升扩展/API 基线或更改已有基础 WSI 兼容范围，暂停并复议范围；不得用放宽 validation 过滤完成验收。
- 用户另行请求时，再同步主规格并归档，验证归档记录与主规格；这不是当前任务清单的完成前提。
