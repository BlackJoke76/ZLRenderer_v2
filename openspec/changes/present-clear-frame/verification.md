# present-clear-frame 实施验收记录

日期：2026-10-12（Asia/Shanghai）；schema：spec-driven。实施和运行验收通过，24/24任务完成，已提交推送main，仍为活动 change，未同步 delta 或归档。实现提交为4dfcc6512689c4c2818e2c3c03fa042c38b55aaa，远程main已核对一致；交付记录提交见STATUS和Git历史。

## 构建与运行证据

全新 `build-present-clean` 按 README 配置 VS2022 x64、C++20；Debug/Release /W4 /WX 构建成功，各34/34 CTest通过。日志在 `test-logs/<配置>/`，CTest完整输出在 `Testing/Temporary/LastTest.log`（最近配置）。

真实 AMD Radeon RX7900XT，graphics/present均族0；FIFO、format50/B8G8R8A8_SRGB、colorSpace0、3图像、EXCLUSIVE，2帧槽。当前surface无MAILBOX，MAILBOX优先是合成策略测试，不能宣称硬件实测。仅测试的fifo用例从真实支持列表移除MAILBOX，再选择支持的FIFO并运行帧。normal日志的imageIndex按0/1/2循环，frameIndex按0/1循环；CTest核对每槽command/acquire/fence身份及每代图像的present semaphore。

测试目标通过可用的VK_EXT_layer_settings在instance创建显式配置validate_sync=true，成功后输出启用记录。产品窗口验收使用官方SDK进程变量VK_LAYER_VALIDATE_SYNC=1；不改全局layer配置、不使用旧VK_LAYER_ENABLES。生产Release二进制不包含ZL_VULKAN_TEST_CASE、故障注入或受控诊断标识。

| 实际窗口场景 | submitted/presented | generations | 证据目录 |
|---|---|---|---|
| Debug默认色、缩放/最小化恢复/关闭 | 2036/2036 | 5 | build-present-clean/window-logs/Debug |
| Debug最小化期间关闭 | 1998/1998 | 5 | build-present-clean/minimized-close-logs/Debug |
| Release默认色、缩放/最小化恢复/关闭 | 2044/2044 | 5 | build-present-clean/window-logs/Release |
| Release最小化期间关闭（独立重验） | 2000/2000 | 5 | build-present-clean/minimized-close-logs/Release |
| Release测试目标替代色 | 2064/2064 | 5 | build-present-clean/alternate-color-logs/Release |
| ASan Release完整窗口流程 | 2068/2068 | 5 | build-present-asan/window-logs/Release |

每次无输入至少10秒，再反复调整1100×700、720×480、900×600外框，检查响应和暂停/恢复或最小化关闭，退出0。每目录保存stdout/stderr、verification.txt和客户区PNG，每阶段9点颜色采样。默认线性RGBA=(0.05,0.1,0.2,1)显示RGB=63,89,124；替代=(0.2,0.05,0.1,1)显示RGB=124,63,89，符合sRGB编码。客户区截图已检查为均匀颜色。

`build-present-asan`：/EHsc /Zi /fsanitize=address，链接/DEBUG；34/34 CTest及完整窗口通过，无主机内存错误，运行时只给验证进程加入MSVC ASan DLL路径。`build-present-analysis`：/EHsc /analyze，四个C++目标通过，无分析警告。未安装Clang工具链，MSVC不提供UBSan，未运行或宣称UBSan通过。

## 34个规格场景对应

12条要求（8新增、3context变更、1文档变更）均有代码和验收依据；下表覆盖全部场景，不以任务勾选或文件存在作为证明。

| 要求 | 场景 | 实现/证据 |
|---|---|---|
| Expose a minimal rendering interface | Render through the application boundary | main只调用Rhi，rhi.h无Vk类型；严格构建/真实窗口 |
| Choose a supported swapchain configuration | Mailbox is supported；Fifo fallback；Respect surface bounds；Configuration is unavailable | 生产query/selection；swapchain-selection覆盖模式、格式/UNDEFINED、extent、图像数上限0、alpha/transform和sharing；缺usage拒绝测试/真实FIFO |
| Continuously present the configured clear color | Sustained visible output；Alternative configured color；Invalid color configuration | 默认/替代实际截图及帧摘要；invalid-color/nonfinite-color无提交、非零退出 |
| Synchronize acquisition rendering and presentation | Successful frame；Separate queue families | CLEAR/STORE及最终布局，COLOR_ATTACHMENT_OUTPUT等待，按图像信号；同步validation，分族CONCURRENT合成参数和代码检查 |
| Reuse completed frame and image resources | Multiple frame slot cycles；Image count differs from frame slot count | 实际3图像/2帧槽，12帧对象身份检查及数千帧窗口；稳定路径无WaitIdle/对象创建，fence后reset pool |
| Recover from window and swapchain changes | Resize or out of date；Suboptimal acquisition or presentation；Minimize and restore；Close while minimized | 真实窗口两类场景；acquire/present四种重建用例恢复12帧、第2代，成功SUBOPTIMAL获取信号被提交消费 |
| Handle aborted frames and rendering failures | Acquisition requires reconstruction；Recording or submission fails；Unrecoverable presentation or device failure | 获取OUT_OF_DATE不reset fence；record/submit失败摘要0、不等待未提交fence；surface-lost/present-failure/wait-failure非零及资源平衡 |
| Validate presentation lifecycle | Validated runtime sequence；Policy and exceptional path coverage | 完整Debug/Release/ASan、同步validation及窗口；真实/合成/注入界限明示 |
| Select a suitable physical device | Shared queue family；Separate queue families；No suitable device；Queue support without swapchain support | 同族真实；分族公共选择测试；no-device/missing-swapchain/missing-surface-usage诊断与回滚 |
| Create and retrieve the selected queues | One family serves both roles；Two families serve the roles；Swapchain device extension enabled | 去重列表直接构造device infos，启用KHR swapchain并运行实际API；同族真实/分族合成 |
| Destroy objects before their dependencies | Orderly shutdown；Close with submitted frames；Failure during GPU completion wait；Partial presentation initialization | 实际有提交关闭、wait-failure；frame-resources/swapchain/image-view/framebuffer/rebuild-failure核对计数与device前释放 |
| Recorded presentation preference | Plan swapchain implementation；Read implemented presentation state | AGENTS保留模式约定；README从头构建/颜色/验收；STATUS实测/限制；ROADMAP区分完成和后续 |

## 设计核对与限制

Rhi通过私有VulkanBackend组合context和presenter；后者借用device view，先于context/window释放。固定两帧槽与按实际数量的图像集合；每帧一次submit/present，render pass整幅CLEAR/STORE，没有shader/pipeline、逐帧同步对象或WaitIdle。初次/重建复用查询和创建，重新建立全部图像集合，失败退出。正常shutdown与noexcept兜底共用清理；成功创建后保存句柄，释放后清空；不独立销毁physical device、queue或swapchain image。

- 基础WSI按提案保留WaitIdle退出/重建限制：只证明GPU提交完成，不严格证明呈现引擎已释放present semaphore/swapchain。稳定帧复用具有再次acquire依据；没有maintenance1/present fence，validation无错误不能补足规范保证。
- 未实测分族或MAILBOX。格式/图像数变化是生产helper合成输入与按代创建代码检查，真实缩放仍为format50/3图像。
- submit/surface/device/wait错误是受控控制流，未让真实驱动掉线。未执行acquire/submit前返回注入结果；或真实成功acquire/present/idle后仅改变host分支，保留真实同步状态。
- 原始日志包含既有具体AMD ICD loader ERROR，只按knownEnvironmentDiagnostics两条具体模式归因，未知应用WARNING/ERROR仍失败。一次旧enables变量探测有VALIDATION-SETTINGS提示，最终移除该变量。
- 初次截图DPI偏移已通过SetProcessDPIAware修正并重验；一次Release截图被并发ASan窗口遮挡，保留capture-interference.png和interference.*.log，顺序重跑通过。最终可见窗口验收顺序运行。
- 自动像素验证需要未遮挡桌面，支持四种B8/R8 UNORM/SRGB；其他格式须人工查看截图。不宣称帧率或性能优化保证。

Git交付任务6.4已完成；实现提交和任务完成记录分别保存并推送到已确认的origin/main，交接记录同步更新。主规格同步与归档等待用户后续请求。
