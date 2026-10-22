# Proposal

## Why

当前 RenderGraph 已生成物理执行组及组边界转换，但 `recordGraphFrame` 将全部组录到一个 primary，帧准备、录制和提交仍集中在 presenter。需要以完整物理执行组作为独立录制单元，先用串行路径验证边界和生命周期，为后续工作提供清晰职责。

## What Changes

- 拆分帧准备、单组录制和统一提交；准备阶段解析当前已获取图像、附件、同步映射及提交上下文，录制阶段只消费准备结果。
- 每个完整物理执行组使用一个独立 primary，包含组前转换、完整 render pass 和组后转换；按计划顺序串行录制。
- 每帧槽预分配并复用与当前固定计划组数匹配的 primary 数组；所有组成功结束后，通过一次 `vkQueueSubmit`、一个 `VkSubmitInfo` 按序提交。
- 保留图像级呈现同步、帧槽级获取同步与提交 fence，保持跳帧、SUBOPTIMAL、重建、清理及失败停止规则。
- 更新实际调用诊断与测试，证明两个组的 primary 不同、组间 WAW barrier 正确、最终图像为第二次清屏颜色，且只有一次提交。
- 范围外：多线程/任务系统、secondary、每组提交、自动合并、采样绘制、多队列、计划热替换和新的 WSI 扩展。

## Capabilities

### New Capabilities

无。

### Modified Capabilities

- `render-graph`：明确准备/录制/提交边界、完整物理组对应独立 primary、按序一次提交的执行契约及分层验证；替换旧双清屏场景中的“同一 command buffer”约束。

## Impact

- `src/vulkanPresenter.h/.cpp`：帧槽命令数组、准备记录、单组录制、统一提交及重建清理引用。
- `src/vulkanRenderGraph.h/.cpp`：复用绑定校验和同步映射，按需承载准备结果；不改变逻辑图声明、依赖算法或一 pass 一组策略。
- `tests/vulkanRenderGraphTests.cpp`、`tests/vulkanTestHooks.*`、`tests/checkRenderGraph.cmake`、`tests/verifyPresentation.cmake`、窗口验收脚本与 `CMakeLists.txt`：验证实际数组输入、顺序、复用与中途失败。
- 实施验收后更新 `docs/ARCHITECTURE.md`、`docs/STATUS.md`；无新增第三方依赖，应用 RHI 接口不变。
