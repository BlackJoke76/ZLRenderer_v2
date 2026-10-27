# Proposal

## Why

`prepare-render-group-recording` 已把准备、完整执行组录制和统一提交拆开，但当前仍由调用线程顺序录制，工程内尚无可复用的 CPU 批次执行与完成等待设施。先独立建立并验证最小任务执行器，可以把线程、任务生命周期和失败汇总与 Vulkan 资源同步分开验收。

## What Changes

- 新增纯 CPU 任务执行器：按显式 worker 数量创建固定线程，接受独立任务批次，返回批次完成句柄，等待全部任务终结后取得按任务索引关联的失败汇总。
- 零 worker 使用调用线程按提交顺序执行同批任务；单 worker 使用一条工作线程；多 worker 允许独立任务并发，提供执行器内稳定的 worker 索引。不同模式采用相同完成和失败契约。
- 每个已接受任务执行一次；任务抛异常被收集，批次其余任务继续执行，worker 继续服务后续批次。允许空批次、多个在途批次、重复等待以及丢弃句柄后任务继续完成。
- 显式关闭和析构停止接收批次，排空全部已接受工作并回收线程；覆盖空闲、队列尚有任务、回调运行中、失败后及重复关闭。
- 新增不查找或链接 Vulkan/GLFW、不下载图形依赖的 CPU-only 配置和测试路径；默认图形构建保持现有入口。
- 调用边界：一个协调线程串行提交和关闭；非回调线程可以等待批次；任务回调不得重入同一执行器的提交、等待、关闭或销毁操作。
- 不包含 RenderGraph/presenter 接入、Vulkan 命令池管理、GPU 提交、任务依赖图、取消、任务优先级、工作窃取、协程或性能提升承诺。

## Capabilities

### New Capabilities

- `cpu-task-execution`：拥有任务批次的 CPU 调度、worker 身份、完成可见性、任务失败汇总及排空关闭，并支持无图形依赖的独立验收。

### Modified Capabilities

无。现有 `render-graph` 和 `frame-presentation` 执行契约不变；文档补充 CPU-only 使用方式不改变默认 Windows 图形入门要求。

## Impact

- 新增 `src/taskSystem.h/.cpp` 与 `tests/taskSystemTests.cpp`，仅依赖 C++20 标准库和平台线程库；API 命名遵循 lowerCamelCase。
- `CMakeLists.txt`：复用 `configureCppTarget`，新增 taskSystem/测试目标和线程依赖；通过默认开启的 renderer 选项隔离 GLFW/Vulkan 查找与图形目标。
- `README.md`、`docs/ARCHITECTURE.md`、`docs/ROADMAP.md`、`docs/STATUS.md`：实施时补充调用契约、纯 CPU 命令、验证范围和仍未接入的并行录制阶段。
- 不修改现有 renderer 产品调用链，不以本 change 的 CPU 测试宣称 Vulkan 并行录制正确。既有已完成但未归档的 `prepare-render-group-recording` 保持原状。
