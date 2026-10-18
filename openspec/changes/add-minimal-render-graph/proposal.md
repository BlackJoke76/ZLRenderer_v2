# Proposal

## Why

当前清屏的资源使用、录制和同步都集中在 VulkanPresenter，缺少能根据资源前后用途生成执行与同步计划的基础。本变更将现有清屏接入最小单队列 RenderGraph，保持画面与呈现生命周期正确，并使多 pass 的依赖、执行分组和实际同步可以核验。

## What Changes

- 建立资源用途声明、逻辑 pass 和呈现出口。支持多个外部颜色图像与多个逻辑 pass，按声明顺序执行；清屏是颜色附件的 load 行为，不将整个图限定为一种清屏操作。
- 编译资源读写关系，自动推导 RAW、WAR、WAW 和用途变化所需的同步；处理多个读取者与内容有效性。当前范围为单 graphics queue、整张单 mip/单 layer/单采样颜色图像。
- 分离逻辑 pass 与物理执行组，本次一对一分组。自动 pass 合并算法留待后续，接口不固定每个逻辑 pass 的 command buffer 或 submit 数量。
- Vulkan 后端统一通过组边界的显式 image barrier 落实图同步；传统 render pass 在渲染范围内保持颜色附件布局，不重复承担同一次布局转换。
- 图与编译计划由渲染流程持有，实际图像在 acquire 后绑定；Presenter 继续拥有交换链、帧资源及 WSI 控制，维持一次 graphics submit、两个帧槽及整代重建。
- 生产画面仍为配置颜色的持续清屏；测试目标以同一交换链图像的两次清屏，真实验证多 pass 和写后写同步。采样读取等其他用途本轮验证编译及映射，不宣称已提供或实测采样绘制。
- 输出逻辑顺序、执行组、资源用途、同步原因与实际绑定，关联真实录制、成功提交和呈现请求结果。

不实现自动重排、通用 DAG 调度、pass 裁剪、资源别名、临时 GPU 图像、MRT/深度/MSAA、子资源拆分、shader/三角形/后处理绘制、动态渲染、多队列调度或多线程；不扩展基础 WSI 的呈现完成保证。

## Capabilities

### New Capabilities

- `render-graph`：声明外部颜色图像的有序使用，校验内容和引用，自动规划单队列依赖与状态转换，以独立执行组驱动后端并提供可核验的诊断。

### Modified Capabilities

无。`frame-presentation` 已有画面、选择、同步、复用、失败和重建契约继续作为回归基线；其外部行为要求不变。

## Impact

- 预计新增 `src/renderGraph.h/.cpp` 与纯 CPU 测试；在现有 Vulkan 后端中增加生产实际消费的计划映射与执行，必要时仅将这部分提取为小型文件，不建立虚拟多后端框架。
- 图构建及不可变计划可先由现有 VulkanBackend 持有；主要接入点为 Presenter 的 render pass 创建、帧录制和提交。SwapchainResources 只拥有当前代实际后端对象，不拥有逻辑图。
- 改用显式边界转换，原 render pass 的初始/最终布局及外部依赖需同步核对。后端继续使用 Vulkan 1.0 API；无新增第三方依赖。
- 复用 CMake helper、测试专用 hooks、呈现核查与窗口脚本，补齐 CPU hazard、实际 barrier、双清屏及跨代绑定证据；保留 MAILBOX 优先/FIFO 回退验证。
- 实施验收后更新 README、ARCHITECTURE、ROADMAP、STATUS 与本 change 的验证报告，准确列明真实 GPU、纯 CPU、参数映射和状态控制的覆盖范围。
