# Proposal

## Why

当前最小 RHI 只能清屏，尚未验证 shader 编译、graphics pipeline 与 draw 的完整链路。下一步用 Slang 彩色三角形建立最小可观察绘制结果，同时守住已验收的交换链生命周期。

## What Changes

- 构建期通过本机 Slang 编译顶点/片元入口为 SPIR-V，明确工具路径、版本、目标和入口，生成产物随可执行文件部署。
- 新增显式三角形内容选项：示例应用选择彩色三角形，RHI 默认及原清屏测试保留 clear-only 行为；背景仍使用现有 clearColor。
- 同一 render pass 内清背景后绘制一个三角形，缩放、最小化恢复与 oldSwapchain 整代替换后继续正确显示。
- 扩展构建、失败路径与实际窗口像素验收，保留现有呈现回归。

## Capabilities

### New Capabilities

- `triangle-rendering`: Slang 构建产物、配置选择、彩色三角形显示及绘制资源生命周期的最小契约。

### Modified Capabilities

无。`frame-presentation` 的清屏模式、同步、模式选择与重建契约继续适用；三角形为显式新增内容模式。

## Impact

预计涉及 CMakeLists.txt、shaders/、src/rhi.h、src/rhi.cpp、src/main.cpp、src/vulkanPresenter.*、测试与 scripts/verifyWindow.ps1，以及实施后的 README/ARCHITECTURE/STATUS/ROADMAP。开发构建新增 slangc 依赖，运行时不加载 Slang 编译器；不改变 Vulkan 设备功能基线。

## Scope and Acceptance

目标：Debug/Release 稳定显示带 RGB 插值的单三角形和配置背景，尺寸变化及至少三轮最小化恢复后仍正确，可见/最小化关闭均在正常设备条件下5秒内退出0，无应用 validation/sync WARNING/ERROR。允许新增固定绘制资源、构建规则和相关测试。

不做：顶点/索引缓冲、纹理、descriptor、uniform、深度、mesh、热重载、运行时编译、反射框架、通用PSO缓存、RenderGraph、多线程或maintenance1。以 vertex ID 生成固定顶点，允许非方形窗口按NDC比例拉伸。

暂停条件：编译目标需要提升当前设备功能基线、既有呈现契约无法保留、必须引入上述非目标或工具链不可用且无已授权配置办法。报告证据后调整规划，不静默扩展。
