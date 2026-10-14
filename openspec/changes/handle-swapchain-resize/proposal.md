# Proposal

## Why

`present-clear-frame` 已实现基本缩放、最小化恢复及关闭，本次是在现有路径上补齐边界与验收，并按用户要求通过 `oldSwapchain` 创建下一代交换链。当前路径先销毁旧交换链再创建，重建还使用等待 GPU 前采样的尺寸；现有验收尚不能逐次证明最新尺寸、暂停期间无提交及每代资源的完整生命周期。

## What Changes

- 明确尺寸变化、最小化/零 framebuffer、恢复与关闭的状态转换；重建等待后重新处理事件和读取尺寸，使用最新可呈现状态，暂不可呈现时延迟重建。
- 复用并调整现有交换链创建、重建与释放路径：初建的 `oldSwapchain` 为 null，重建时传入当前未退役的旧句柄；先创建完整新一代资源，再切换并清理旧资源，保留固定两个帧槽。
- 每代重新查询 surface 支持，完整更新 swapchain、图像视图、framebuffer、render pass 和图像级呈现信号量。新交换链创建调用之后旧交换链已退役，即使失败也不得回滚到旧链继续 acquire。
- 保留 acquire/present 的 OUT_OF_DATE、SUBOPTIMAL 处理，补齐重建与暂停交错、相同尺寸恢复、重建中途失败的控制与验证。
- 扩展既有 CTest 和窗口脚本，核对逐次 framebuffer/extent、稳定状态不重复重建、最小化期间无新提交、重复恢复及关闭；保存原始 validation/sync 日志与资源证据。
- 验收后更新 README、架构与 STATUS；按项目规则独立提交并推送。

允许范围限于现有单窗口、具体 RHI 和 VulkanPresenter，以及直接相关的选择函数、测试和文档。不加入绘制、RenderGraph、多线程、多窗口、通用事件/资源体系或设备/surface 丢失后的自动恢复；不改变颜色配置、MAILBOX 优先/FIFO 回退及 Vulkan 1.0 基线。沿用已记录的基础 WSI WaitIdle 清理限制，本次不引入 maintenance1/present fence。

验收结果应为：Debug/Release 中反复缩放及最小化恢复后完整显示配置颜色，暂停期间不提交/忙循环，正常及最小化关闭退出 0，每代资源可核对、无应用相关 validation WARNING/ERROR。已满足的代码保持复用，以补齐具体缺口为实施依据。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `frame-presentation`：细化窗口变化恢复与验收要求，增加基于 `oldSwapchain` 的整代资源重建契约，并明确旧链退役及重建中途失败的处理。

`desktop-window` 的事件响应和正常关闭要求保持适用，无需建立重复 capability 或修改其契约。

## Impact

- 预计涉及 `src/main.cpp`、`src/vulkanPresenter.{h,cpp}`、`src/swapchainSelection.{h,cpp}`；优先保持现有 `Rhi` 公共接口。
- 测试涉及 `tests/swapchainSelectionTests.cpp`、`tests/vulkanTestHooks.{h,cpp}`、`tests/verifyPresentation.cmake`、`scripts/verifyWindow.ps1` 及直接相关 CTest 注册。
- 实施后据实更新 `README.md`、`docs/ARCHITECTURE.md`、`docs/STATUS.md` 和路线图中的本 change 状态。
- 无新运行依赖。真实 MAILBOX、分族队列和配置变化覆盖依设备而定；必须区分硬件证据与合成/注入结果。
- 如果必须提升 Vulkan/扩展兼容基线、引入呈现完成扩展或重写 RHI 架构才能解决问题，暂停并重新讨论范围；未知应用 validation 错误不得通过扩大诊断过滤规避。
