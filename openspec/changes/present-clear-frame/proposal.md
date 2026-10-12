# Proposal

## Why

现有程序只初始化 Vulkan 并等待窗口事件，尚不能向窗口提交画面。需要一个可持续清屏、可验证同步和资源复用的最小呈现路径，作为后续绘制功能的基础。

## What Changes

- 建立最小 RHI 边界：应用只调用初始化、清屏帧和关闭操作；Vulkan 后端复用现有 context，负责交换链、命令和同步对象的生命周期。
- 扩展设备适用性检查，启用 `VK_KHR_swapchain`，查询 surface 能力、格式和模式；优先 MAILBOX，不支持时使用 FIFO。
- 创建交换链及每图像资源，运行获取图像 → 录制清屏 → 提交 → 呈现循环。默认清屏颜色为代码配置的线性 RGBA `(0.05, 0.10, 0.20, 1.0)`，可在初始化配置中更改。
- 区分帧槽和交换链图像索引，复用命令与同步对象，处理缩放、最小化、OUT_OF_DATE 和 SUBOPTIMAL，正常关闭与失败路径均等待已有 GPU 提交并按依赖清理。
- 增加选择策略、同步状态及失败路径验证，更新 README 和工程交接文档。
- 本次只呈现清屏颜色；三角形、shader/pipeline、通用资源句柄体系、多后端、渲染图、异步线程和性能目标留给后续 change。

## Capabilities

### New Capabilities

- `frame-presentation`: 最小 RHI 的持续清屏契约、交换链配置、帧资源复用、呈现同步与交换链重建。

### Modified Capabilities

- `vulkan-context`: 物理设备必须满足交换链扩展和当前 surface 的清屏呈现条件；显式启用交换链扩展；退出时先完成已有提交并释放 device 子资源。
- `project-onboarding`: 将呈现模式约定的文档状态从未来阶段更新为已实施后的可核验行为。

## Impact

- 预计涉及 `src/main.cpp`、`vulkanContext.h/.cpp`、新 RHI/Vulkan 呈现模块与选择策略 helper、`CMakeLists.txt`，以及已有 lifecycle 测试和 `scripts/verifyWindow.ps1`。
- 保持 Windows、C++20、GLFW、Vulkan 1.0 与 Debug/Release validation 基线；新增必需设备扩展 `VK_KHR_swapchain`，不新增第三方库。仅有 graphics/present 队列而无法创建清屏交换链的设备将明确拒绝。
- README 增加颜色配置、从头构建和持续呈现验收步骤；ARCHITECTURE 记录所有权、同步时间线与销毁顺序；STATUS 记录实测模式、场景、诊断和硬件覆盖；ROADMAP 更新本阶段。
- 规划材料不代表实现已完成；实施验收后按项目约定单独提交并推送。基础 WSI 的呈现完成与清理限制、可选扩展的后续方向明确记录于 design。
