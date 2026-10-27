# Proposal

## Why

现有后端已经把帧准备、完整组 primary 录制和单次有序提交分开，TaskSystem 也已独立验收，但组录制仍逐组串行执行。现在把两者接通，使不同执行组可由多个 worker 同时录制，并以可检查的重叠、调用输入和画面证据保证正确性。

## What Changes

- 复用现有 TaskSystem，将本帧每个 PreparedRenderGroup 作为独立任务；零 worker 串行、单 worker 和多 worker 共用 recordRenderGroup。
- 按帧槽和稳定 worker 索引持有 command pool，预分配可复用 primary；完成等待后按计划组序收集结果，不依赖任务完成顺序。
- 保持一次 VkSubmitInfo、一次 vkQueueSubmit、原有 acquire/present 同步及组边界 barrier。整批完成且全部组有效才允许 reset fence 和提交。
- 失败时先收敛全部已接受任务再清理；不提交半帧、不推进未提交状态、不释放仍被任务或 GPU 使用的对象。
- 验证真实录制任务重叠、串并行参数/像素一致、跨帧及重建复用、失败不提交，并维护有界诊断。
- 不引入 secondary、额外 GPU queue、组重排/合并、任务依赖图、运行时计划替换、取消机制或新的绘制功能；不把清屏任务的并发能力等同于性能提升。

## Capabilities

### New Capabilities

无。并行组录制归入现有 render-graph；CPU 执行器本身的契约保持不变。

### Modified Capabilities

- `render-graph`: 支持共享录制函数的零/单/多 worker 调度、帧槽与 worker 命令资源所有权、有序完整提交、并发失败收敛和分层验收。

## Impact

主要涉及 src/vulkanPresenter.h/.cpp 的命令资源、录制协调及清理，src/rhi.h/.cpp 的 worker 配置传递，CMakeLists.txt 的 taskSystem 链接，以及 tests/vulkanTestHooks.*、checkRenderGraph.cmake、verifyPresentation.cmake 和 scripts/verifyWindow.ps1 的证据及验收。复用 src/taskSystem.* 和 src/vulkanRenderGraph.*，保持 CPU-only 构建独立；实现后更新 docs/ARCHITECTURE.md、docs/STATUS.md 与相关使用说明。

前置依赖：prepare-render-group-recording 已实施但尚未同步/归档；add-recording-task-system 已同步/归档。新 delta 基于前者的完成后契约，后续同步必须先合入前者再合入本 change；本轮不修改或归档前置 change，不直接修改主规格。
