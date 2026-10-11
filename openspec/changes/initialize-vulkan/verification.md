# Verification Report: initialize-vulkan

2026-10-11，schema：`spec-driven`。复核实现提交 `ffa2f73065111b18d346934f11ee7772bc4639df` 及交接提交 `59743ecb055da38d07e0f7d901a60553725fad93`，复核开始时本地与 `origin/main` 一致。未改实现、规划任务或主规格，未归档。

## Summary

| Dimension | Status |
|---|---|
| Completeness | Task Completion：14/14；Spec Coverage：6/6 ADDED requirements；没有不可读的 tracking/artifact 文件 |
| Correctness | Requirement Implementation Mapping：6/6；Scenario Coverage：13/13 均有实现及验证依据，分族使用合成测试与代码核对 |
| Coherence | Design Adherence：6 项核心决策一致；Code Pattern Consistency：命名、复用、目录及范围约束一致 |

没有 REMOVED/RENAMED requirements，没有跳过上述适用检查。

## Requirement implementation mapping

| 要求 | 实现证据 | 验证依据 |
|---|---|---|
| Initialize Vulkan for the existing window | `src/main.cpp:45`，`src/vulkanContext.cpp:83`；全部创建及 queue 检查后才记录成功并进入事件循环 | Debug/Release 真实窗口验收 |
| Select a suitable physical device | `src/vulkanContext.cpp:209` 查询当前 surface 支持；`src/queueSelection.cpp:3` 排除空族，同族优先，否则分族；返回第一个合适设备 | 公共选择函数测试、真实族 0、`vulkan-no-device` |
| Create and retrieve the selected queues | `src/vulkanContext.cpp:252` 用公共去重列表构造 queue infos；`src/vulkanContext.cpp:108` 获取两个角色的 index 0 | 合成同族/分族去重测试、真实非空队列日志 |
| Enable validation and observable diagnostics | `src/vulkanContext.cpp:124` 核查依赖；`src/vulkanContext.cpp:181` pNext 回调和显式 layer；`src/vulkanContext.cpp:52` 无状态 noexcept 回调；`src/vulkanContext.cpp:193` persistent messenger | 两配置正常日志、layer/extension 缺项、受控诊断、实际 DLL 装载记录 |
| Destroy objects before their dependencies | `src/vulkanContext.cpp:284` 统一 cleanup；`src/main.cpp:35`、`:38`、`:45` 构造顺序使 context 先于窗口/GLFW 析构 | 两配置真实关闭记录，顺序及次数精确比较，无应用 validation WARNING/ERROR |
| Roll back partial initialization | 空 RAII context 先构造，创建成功后接管句柄；失败共用 cleanup；`src/main.cpp:51` 捕获错误返回 1 | 各阶段故障子进程验证返回 1、无成功日志及已创建对象的精确清理序列 |

队列获取的请求族、index 0 和 flags=0 满足 [vkGetDeviceQueue 的约束](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetDeviceQueue.html)。清理代码在 instance 前释放显式子对象，满足 [vkDestroyInstance 的约束](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyInstance.html)。messenger 在主线程创建，回调配置与实例依赖符合 [vkCreateDebugUtilsMessengerEXT](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateDebugUtilsMessengerEXT.html)。

## Scenario coverage

| 场景 | 代码/测试证据 |
|---|---|
| Successful initialization | 两配置真实窗口，观察 success 后移动、调整大小、事件响应与关闭 |
| Shared queue family | `queueSelectionTests.cpp:17`；真实 graphicsFamily=0 / presentFamily=0 |
| Separate queue families | `queueSelectionTests.cpp:25`，公共选择函数返回 {1,0}；生产选择路径复用该函数 |
| No suitable device | `vulkan-no-device`：非零退出，无成功日志，surface/messenger/instance 回滚 |
| One family serves both roles | `queueSelectionTests.cpp:22`；真实请求族数 1，两个角色获得同一非空队列 |
| Two families serve the roles | `queueSelectionTests.cpp:29`；去重输入返回 {1,0}，生产路径对该列表逐族建立 infos，并按两个角色的族获取 queue index 0；没有真实分族硬件运行证据 |
| Normal validated lifecycle | 两配置 enabled-layer 与清理日志；实际 layer DLL 装载记录；无应用 WARNING/ERROR |
| Validation dependency missing | `vulkan-missing-layer` 与 `vulkan-missing-extension`，指出具体缺项、返回 1，只清理窗口与 GLFW |
| Diagnostic message is delivered | `vulkan-diagnostic`，真实 debug-utils 提交受控 WARNING；严重级别、标识及正文可见；回调 noexcept 且无抛异常操作 |
| Orderly shutdown | 两配置 SC_CLOSE、5 秒内退出 0、device → surface → messenger → instance → window → GLFW，各一次 |
| Failure after instance creation | `vulkan-instance` 与 `vulkan-messenger`，只清理当时已经创建的对象 |
| Failure after surface creation | `vulkan-surface`、`vulkan-no-device`、`vulkan-create-device`，诊断阶段/原因及正确回滚 |
| Failure after device creation | `vulkan-device`，device 最先释放，之后完整回滚 |

## Coherence

- CMake 通过 `Vulkan::Vulkan` 查找 SDK，没有硬编码本机 SDK 路径；应用请求 API 1.0。
- Debug/Release 都强制 layer/debug-utils；无静默 fallback。早期 pNext 与持续 messenger 共用同一个无状态 C 回调，回调没有会提前释放的状态。
- 真实 surface 查询和队列选择、族去重、优先级 1.0、flags=0、index 0 与设计一致；physical device 与 queues 均借用。
- 空、不可复制/移动的 context 在初始化前已完成构造；保存成功句柄；统一析构清理，不增加通用 handle/backend 框架。
- 销毁依赖及失败路径一致；本 change 没有 GPU 提交，也没有增加 device/queue idle 等待。
- `configureCppTarget` 复用 C++ 构建设置；生产目标不定义测试宏。Release 二进制检查未发现故障注入环境变量、故障文本及受控消息标识。没有 swapchain/MAILBOX 呈现代码。
- README、ARCHITECTURE、ROADMAP、STATUS 与实现一致；既有窗口和入门主规格、AGENTS 约定仍满足。bootstrap-project 的已知 GLFW tag SHA 描述警告按本次设计 Non-Goals 保留，不计为本次新增问题。

## Validation performed

在已有、此前从头配置的 `build-vulkan-clean` 中复跑：

```powershell
cmake --build build-vulkan-clean --config Debug --parallel 2
cmake --build build-vulkan-clean --config Release --parallel 2
ctest --test-dir build-vulkan-clean -C Debug --output-on-failure
ctest --test-dir build-vulkan-clean -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-vulkan-clean/Debug/zlRenderer.exe -logDirectory build-vulkan-clean/verify-logs/Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-vulkan-clean/Release/zlRenderer.exe -logDirectory build-vulkan-clean/verify-logs/Release
openspec validate initialize-vulkan --strict
git diff --check
```

编译成功，两个配置各 10/10 CTest 通过；两个可见窗口的标题、移动到 (100,100)、调整外框为 900×600、事件响应、SC_CLOSE 和退出 0 均通过。设备 AMD Radeon RX 7900 XT，两种角色族 0；所有清理顺序/次数吻合。

窗口 stdout/stderr 在 `build-vulkan-clean/verify-logs/<配置>/`，故障与受控诊断日志在 `build-vulkan-clean/test-logs/<配置>/`。同样复现旧 AMD ICD JSON ERROR 与 EOS Overlay 重复 WARNING；原始文本保留，仅匹配两条已核实的环境诊断，其他 WARNING/ERROR 仍使测试失败。不是“系统全部无诊断”。

本次另核对了 SDK 头文件/import library/validation DLL、先前实际 layer 装载日志、ASan 最后一次成功测试日志与无报告记录、MSVC /analyze 无警告日志。没有重新配置全新目录、复跑 ASan 或静态分析；复核期间实现代码无修改，已有从头构建及附加验证证据适用。MSVC 不支持 UBSan，此项仍未运行，不将其报告为通过。

## Issues and final assessment

- CRITICAL：0。
- WARNING：0。
- SUGGESTION：0。

适用检查全部通过，可进入归档流程。验证本身不执行同步主规格或归档。
