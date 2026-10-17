# handle-swapchain-resize 实施验收

日期：2026-10-14。范围为现有单窗口清屏路径、oldSwapchain 替换、暂停/恢复与相关验证；不包含主规格同步或归档。

## 实现与契约

- `src/vulkanPresenter.h:33` 的不可复制 SwapchainResources 只包含交换链一代的对象，unique_ptr 分别管理当前代和候选代；两个帧槽独立。候选异常由析构调用共享释放函数，不默认 move/copy 原始句柄。
- `src/vulkanPresenter.cpp:156` 的共享创建函数用于初建和替换，创建信息明确传入 oldSwapchainHandle。初建为空，替换为当前未退役句柄；调用前将旧代标为不可再次获取。失败控制测试不能证明真实驱动失败/退役，报告与日志明确注明。
- `src/vulkanPresenter.cpp:399` 等待旧 GPU 工作、复位旧命令引用、处理事件后重新读取尺寸/关闭并查询 surface；零 extent 延迟，不退役旧链。完整候选资源创建后交换 unique_ptr 并清理旧代；仅成功发布后更新有效代及清除重建请求。
- `src/vulkanPresenter.cpp:280` 保留实际 acquire/submit/present 同步。成功获取的 SUBOPTIMAL 消费 acquire 信号并完成当前帧；OUT_OF_DATE 无图像时不复位 submission fence。后续帧禁止使用退役旧链。
- `src/main.cpp:62` 让后端先观察暂停状态并记录计数，然后 main 按实际 iconified/零 framebuffer 等待事件；关闭优先，公共 Rhi 接口不变。surface 单独零 extent 使用一次有界事件等待后返回。

## 25 个场景逐项核对

代码缩写：P=`src/vulkanPresenter.cpp`；S=`src/swapchainSelection.cpp`；M=`src/main.cpp`；W=`scripts/verifyWindow.ps1`。CTest 的 presentation 前缀在表内省略；原始日志在 `build-resize/test-logs/<Debug|Release>/<case>.stdout.log` 与 stderr.log。

| 要求 / 场景 | 代码与验证证据 | 结论 / 覆盖性质 |
|---|---|---|
| Recover / Resize or out of date | P:280、399；acquire-out-of-date、present-out-of-date；四次真实窗口流程 | 真实创建及窗口缩放通过；WSI 结果控制注入 |
| Recover / Suboptimal acquisition or presentation | P:315–386；acquire-suboptimal、present-suboptimal | 实际 acquire/present 成功后改变 host 分支，真实同步仍被消费 |
| Recover / Minimize and restore | M:62；P:280；四次窗口流程的 pause/resume 快照 | 每流程三次真实最小化恢复，计数冻结/继续推进 |
| Recover / Close while minimized | M:62；Debug/Release minimized-close-logs | 最小化关闭退出 0，5 秒等待上限内完成 |
| Recover / Window changes during reconstruction preparation | P:413；prepare-resize、prepare-zero、prepare-iconified、prepare-close | 等待后的真实尺寸设置与受控窗口状态，旧快照不再决定新配置 |
| Recover / Iconified window retains a nonzero framebuffer | P:60、280；initial-iconified、prepare-iconified；真实窗口脚本 | 非零 framebuffer 的 iconified 控制状态仍暂停 |
| Recover / Pending reconstruction survives repeated pause and resume | P:280、399；prepare-zero/prepare-iconified 的三次暂停采样、恢复代 2；窗口三轮恢复 | 同尺寸待重建恢复通过；异尺寸恢复沿用当前尺寸比较/等待后复查，结合 prepare-resize 实际尺寸证据核查；不声称所有组合均硬件实测 |
| Recover / Presentation starts in a paused state | P:44；initial-zero、initial-iconified、initial-zero-close | 受控初始状态，恢复后建代 1，恢复前关闭 0 提交/0 代 |
| Recover / Surface selects a zero extent | S:61、P:427；swapchain-selection、surface-zero、surface-variable-zero、surface-width-zero | 合成/控制固定、可变及单维零 extent，不将无效能力交给真实创建 |
| Recover / Stable size after a resize burst | W:172–181；四个窗口流程 verification.txt | 最终客户区/extent 匹配，稳定观察期间代计数不增加 |
| Failures / Acquisition requires reconstruction | P:322、399；acquire-out-of-date | 12 帧正常推进，未提交帧不破坏 fence |
| Failures / Recording or submission fails | P:258、341；record、submit | 退出 1、提交计数 0，无等待不存在的提交 |
| Failures / Unrecoverable presentation or device failure | P:368、392、510；surface-lost、present-failure、wait-failure | 受控结果/host 分支停止推进，不宣称真实设备丢失 |
| Failures / Reconstruction fails after partial resource creation | P:156、450；rebuild-failure、rebuild-render-pass、rebuild-image-view、rebuild-framebuffer、rebuild-present-semaphore | 候选各阶段真实成功后 checkpoint，所有已创建对象按代只释放一次，退出 1 |
| Failures / Replacement creation fails after retiring the old swapchain | P:179；rebuild-create；retired 检查代码核查 | 实际调用前注入失败，仅证明 host 控制及清理；旧链不被重新 acquire 或传入 |
| Validate / Validated runtime sequence | 全量 CTest、四次 Debug/Release 窗口流程 | 原始日志、截图、配置、退出与资源证据均保存 |
| Validate / Policy and exceptional path coverage | swapchain-selection、queue-selection、fifo；原有异常测试 | MAILBOX/FIFO、分族为策略覆盖；本机仅 FIFO/同族硬件实测 |
| Validate / Repeated window transitions have measurable evidence | W:67、184；四个 verification.txt | 逐尺寸匹配、三轮暂停冻结、恢复推进、可见/最小化关闭通过 |
| Validate / Boundary coverage supplements actual windows | tests/vulkanTestHooks.cpp；合成配置及本表 | 覆盖方式区分，未伪造真实 Vulkan 对象或宣称受控状态为硬件实测 |
| Generation / Initial creation has no predecessor | P:156；old-chain 的 create generation=1 old=0 | 真实初建空旧句柄 |
| Generation / Replacement refers to the live predecessor | P:434；old-chain 的连续 1→2→3 链，verifyPresentation.cmake:144 | 真实创建参数匹配前代；发布后才销毁旧代，帧使用当前代 |
| Generation / Every image has current attachments | P:210–247；verifyPresentation.cmake:123 的实时附件所有权断言 | 帧 framebuffer/render pass 属于存活且已发布的代；实际图像数量管理通过 |
| Generation / Configuration changes between generations | S:61；swapchainSelectionTests.cpp；P:210 的附件配置核查 | 格式/图像数/FIFO 配置变化为合成输入；实际硬件每代查询并匹配，不宣称驱动真实改变格式/图像数 |
| Generation / Frame resources survive reconstruction | P:399；old-chain 12 帧的 slot command/acquire/fence 身份比较 | 两帧槽跨三代复用，每图像 present 信号量按所属代/图像验证 |
| Generation / Generation cleanup and completion evidence | P:450；逐代创建/释放与调用顺序断言、四个窗口摘要 | 自有对象释放一次，借用图像不独立 destroy；基础 WSI 完成限制保持明示 |

## 构建和实际运行

独立目录 `build-resize`，Visual Studio 2022 / MSVC 19.38，/W4 /WX。配置使用现有固定 GLFW 3.4 源码目录 `build-present-clean/_deps/glfw-src`，未重新下载；编译产物及 CMake 配置独立。

```powershell
cmake -S . -B build-resize -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_GLFW=build-present-clean/_deps/glfw-src
cmake --build build-resize --config Debug --parallel 2
ctest --test-dir build-resize -C Debug --output-on-failure
cmake --build build-resize --config Release --parallel 2
ctest --test-dir build-resize -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-resize/Debug/zlRenderer.exe -logDirectory build-resize/final-window-logs/Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-resize/Debug/zlRenderer.exe -closeWhileMinimized -logDirectory build-resize/final-minimized-close-logs/Debug
```

Release 窗口命令仅将 Debug 路径替换为 Release。上述 cmake/ctest 由 VS 自带绝对路径执行，必要时按 README 在当前终端加入工具 PATH。最终 Debug/Release 各 **50/50 CTest 通过**，构建无编译警告；完整汇总 `build-resize/build-<配置>.log`、`ctest-<配置>.log`。测试目标通过 VK_EXT_layer_settings 明确输出同步 validation 启用。

| 窗口流程 | submitted/presented | 完整代数 | 证据目录 |
|---|---|---|---|
| Debug 可见关闭 | 2393/2393 | 9 | build-resize/final-window-logs/Debug |
| Debug 最小化关闭 | 2348/2348 | 9 | build-resize/final-minimized-close-logs/Debug |
| Release 可见关闭 | 2360/2360 | 9 | build-resize/final-window-logs/Release |
| Release 最小化关闭 | 2342/2342 | 9 | build-resize/final-minimized-close-logs/Release |

每流程无输入运行至少 10 秒，逐尺寸匹配实际 framebuffer/客户区/extent，25 点内部/边缘采样 RGB 63,89,124；三轮暂停/恢复提交及呈现计数相等，恢复后的后续阶段推进，稳定尺寸期间不重复重建。四次均退出 0，自有资源创建/释放平衡，无未知应用 validation WARNING/ERROR。脚本仅将自己启动的窗口置顶以保护截图；不操作其他窗口。

本机实际 AMD Radeon RX 7900 XT，graphics/present 均族 0，FIFO、B8G8R8A8_SRGB（50）、3 图像/2 帧槽。保留原始旧 AMD ICD JSON loader ERROR，仅按既有具体模式归因；未修改 knownEnvironmentDiagnostics、驱动、注册表或全局 layer 配置。生产窗口子进程设置 VK_LAYER_VALIDATE_SYNC=1。Release 产品二进制检查不含 ZL_VULKAN_TEST_CASE、候选故障与逐帧测试标识。

视觉复查发现早期脚本截图混入非客户区、只采样中间九点不能发现这一问题。修正为同一 C# 调用中的 per-monitor v2 DPI 几何/GDI 采集，保留桌面恢复动画稳定时间并增加25个内部/边缘采样点；Debug/Release 两种关闭及 ASan 窗口全部重新运行通过。最终证据以上表 final 目录为准，早期截图和两次增强采样暴露坐标/动画问题的失败记录保留在 `window-logs`、`edge-window-logs`、`pixel-window-logs`，不混作最终通过证据。

## 内存与工具验证

MSVC ASan 的 Release 构建使用 `/EHsc /Zi /fsanitize=address`，链接 `/DEBUG`；/analyze 构建使用 `/EHsc /analyze`，四个 C++ 目标无静态分析警告。最终编译日志分别在 `build-resize-asan/build-final.log` 和 `build-resize-analysis/build-final.log`，完整初建日志为各目录 build.log。

ASan Release **50/50 CTest 通过**，汇总在 `build-resize-asan/ctest-Release.log`，用时 158.07 秒；完整窗口流程通过，submitted/presented=2341/2341、9 代，三次最小化恢复后正常关闭，退出 0，无 ASan 报告或未知应用 validation 诊断。日志、截图和计数证据在 `build-resize-asan/final-window-logs/Release`。ASan DLL 所在 MSVC bin 路径只加入验证进程 PATH，未修改系统配置。

未发现可用 Clang/UBSan，MSVC 不提供 UBSan，本次不宣称 UBSan 验证通过。GLFW C 源码和外部驱动/layers 未进行此次 C++ ASan 插桩。

## 验收界限与交付

沿用规划明确的基础 WSI WaitIdle 清理策略：GPU 提交完成不等于呈现引擎完成，oldSwapchain 也不提供呈现完成信号，本 change 未启用 maintenance1/present fence。无 validation 错误不能补足规范级呈现完成保证；相关限制继续保留在 README/ARCHITECTURE。未支持的 MAILBOX、分族和真实跨代格式/图像数变化不得宣称硬件实测。

首次沙箱构建未产生程序时曾提前启动 CTest，8 项因缺少可执行文件失败；改为正常外部 MSVC 构建完成后再测试。该启动失败不计为产品功能证据；最终完整两配置结果以上述日志为准。

交付目标已核实：HTTPS origin `https://github.com/BlackJoke76/ZLRenderer_v2.git`，当前分支 main，上游 origin/main。实施提交 [`5921f80e7577a896b5a66c62c6e98769b6d48de7`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/5921f80e7577a896b5a66c62c6e98769b6d48de7) 已推送，git ls-remote 确认远程 main 与提交一致。19/19任务及交付回执随后用独立 handle-swapchain-resize 交接提交保存；只有本 change 相关成果入库，既有未跟踪 `.agents/` 与两个 `.gitkeep` 保留，未同步主规格/归档。

交付复核将截图 C# 辅助方法统一为 getClientRectPixels/captureClient 小驼峰命名，Win32 对接函数保留外部 API 名称。这是只改内部方法名与调用的变更，使用与 README 相同的 Windows PowerShell 入口重新编译内嵌 C# 并检查脚本语法通过；原有五流程画面与状态验证对应的方法体保持不变。默认 pwsh/.NET Core 主机的试查因 System.Drawing 转发引用失败，未计作通过结果；此脚本的已验证入口为文档所列 powershell，不宣称 PowerShell 7 兼容。

## 2026-10-15 OpenSpec 复核

复核基线：`dc4ec5a10ea7038db78f9bda07ca9e3817421361`，schema 为 spec-driven。重新读取 CLI 返回的 proposal、design、delta spec、tasks，识别相对 `f7045de` 的实施范围，并对照当前代码和原始运行证据检查；没有实施待办、修改产品代码、同步主规格或归档。

### Summary scorecard

| Dimension | Status |
|---|---|
| Completeness | Task Completion：19/19；Spec Coverage：4/4（3 MODIFIED、1 ADDED；无 REMOVED/RENAMED） |
| Correctness | Requirement Implementation Mapping：4/4；Scenario Coverage：25/25，含实际窗口、真实 Vulkan 对象、受控结果、合成选择及明确的代码核查 |
| Coherence | Design Adherence：5/5 决策；Code Pattern Consistency：通过，符合现有所有权、命名、函数复用与局部修改模式 |

CRITICAL：0；WARNING：0；SUGGESTION：0。六项适用检查均完成，没有 Not verified 或部分检查项；没有以任务勾选代替代码/证据核查。

### 完整性与正确性复核

- 窗口恢复要求：核对 `src/vulkanPresenter.cpp:44`、`:60`、`:280`、`:399` 与 `src/main.cpp:59`。初始化、帧入口和 GPU 等待后重新判定状态；零 extent 延迟保持请求，关闭优先；成功 SUBOPTIMAL 获取先消费同步再替换。对应 prepare-resize/zero/iconified/close、initial-zero/iconified/zero-close、surface-zero/variable-zero/width-zero 及原有 WSI 结果测试重新通过。
- 中断/错误要求：核对 `src/vulkanPresenter.cpp:179`、`:294`、`:341`、`:391`、`:450`、`:510`。已退役旧链不会再次获取或作为未退役前代使用；候选 RAII 清理与旧代清理分开，失败不等待未提交 fence。除全量测试外，逐一核对两配置六个重建故障用例 stderr 的实际失败阶段，共12项，防止只凭非零退出接受错误阶段失败。
- 验证生命周期要求：核对 `scripts/verifyWindow.ps1:67`、`:91`、`:184`、`:207` 和诊断分类。四个新窗口流程各有九个截图阶段、每阶段25个内部/边缘采样点；暂停/恢复计数冻结、后续推进、稳定代数及5秒关闭上限均通过。确认诊断过滤两文件相对实施前未改动，新的应用诊断仍失败。
- 整代重建要求：核对 `src/vulkanPresenter.h:33`、`src/vulkanPresenter.cpp:156`、`:399`、`:450`。以活动旧句柄创建，候选全部附件成功后才发布，旧代随后销毁；固定帧槽不迁入代集合。old-chain 真实连续两次替换，帧附件存活/已发布代断言、槽身份与逐对象释放检查均通过；配置变化使用合成选择与附件构造代码核查，没有宣称未实测硬件路径。
- 本报告前述25场景矩阵逐行与 delta 场景标题、代码、测试/记录重新比对。异尺寸恢复的组合路径仍明确采用代码核查结合 prepare-resize 及实际窗口证据，不将其说成所有组合均已硬件实测。

### 设计与代码模式复核

| 设计决策 | 当前实现与结论 |
|---|---|
| 私有整代资源集合 | 不可复制 SwapchainResources + 当前/候选 unique_ptr，候选析构调用共享释放；两个帧槽独立，无原始句柄默认 move 复制所有权 |
| oldSwapchain 退役及替换 | 初建空句柄；替换传有效前代；创建后旧链退役，候选不完整直接异常退出，不回滚 acquire；真实 old/new 调用顺序吻合 |
| 尺寸轮询与延迟 | 保留逐帧采样，等待后 poll/复查；窗口暂停由 main wait events，surface 单独零 extent 有界等待；仅完整发布清请求 |
| 帧外切换、保留同步 | 无图像 OUT_OF_DATE 不 reset fence；SUBOPTIMAL 消费真实 acquire；present 重建延至下一帧；没有逐帧 device/queue idle |
| 按代与转换提供证据 | 生产低频诊断，测试目标独有句柄/故障记录；代码核查及新 Release 二进制确认无测试环境变量和逐帧注入标识；25点采样含边缘 |

命名、目录和复用核对范围为已识别的 src、tests、CMake 与窗口脚本差异；外部 Win32/Vulkan API 名称保持要求的拼写，新增自有函数使用小驼峰。没有新增通用资源体系、多后端、渲染图或线程架构。

### 本轮重新执行的验证

新建独立目录 `build-resize-verify`，复用固定 GLFW 源码，保留原实施证据。VS/MSVC /W4 /WX 的 Debug/Release 构建均通过且无编译警告；各 **50/50 CTest** 通过，耗时分别37.50秒、39.30秒。配置、构建、CTest 汇总均在该目录，测试原始日志在 `test-logs/<配置>/`；测试目标明确启用 VK_EXT_layer_settings 同步 validation。

```powershell
cmake -S . -B build-resize-verify -G "Visual Studio 17 2022" -A x64 -DFETCHCONTENT_SOURCE_DIR_GLFW=build-present-clean/_deps/glfw-src
cmake --build build-resize-verify --config Debug --parallel 2
ctest --test-dir build-resize-verify -C Debug --output-on-failure
cmake --build build-resize-verify --config Release --parallel 2
ctest --test-dir build-resize-verify -C Release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-resize-verify/Debug/zlRenderer.exe -logDirectory build-resize-verify/window-logs/Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-resize-verify/Debug/zlRenderer.exe -closeWhileMinimized -logDirectory build-resize-verify/minimized-close-logs/Debug
```

Release 窗口命令同样替换配置名执行。实际命令使用 VS 自带 cmake/ctest 的绝对路径；工具 PATH 设置见 README。

| 本轮实际窗口流程 | submitted/presented | 代数 | 新证据目录 |
|---|---|---|---|
| Debug 三次恢复、可见关闭 | 2382/2382 | 9 | build-resize-verify/window-logs/Debug |
| Debug 三次恢复后最小化关闭 | 2389/2389 | 9 | build-resize-verify/minimized-close-logs/Debug |
| Release 三次恢复、可见关闭 | 2397/2397 | 9 | build-resize-verify/window-logs/Release |
| Release 三次恢复后最小化关闭 | 2381/2381 | 9 | build-resize-verify/minimized-close-logs/Release |

四次均退出0，自有资源创建/释放平衡，25点 RGB 63,89,124 采样通过，暂停/恢复计数一致，稳定状态不重复重建，无未知应用 validation/sync WARNING/ERROR。真实硬件仍是 RX 7900 XT、族0、FIFO、格式50、3图像/2帧槽。具体旧 AMD loader 诊断保留，不视为应用同步错误。

历史 ASan 50/50与完整窗口、/analyze 无警告记录及实际编译参数已读取核对，本轮没有重跑这些工具；它们作为同一 C++ 实现的既有任务证据，不冒充本轮新运行。UBSan 仍按原工具可用性限制记录，未宣称通过。MAILBOX/分族/真实跨代格式数量变化的覆盖性质、基础 WSI 呈现完成限制均保持，不扩张验收含义。

### Final assessment

全部适用检查通过，可进入归档流程。本轮只更新验证报告与交接记录，不同步主规格或归档；工作区既有 `.agents/` 与两个 `.gitkeep` 保留。复核记录按项目授权独立提交推送 main，最终提交 SHA 和远程同步结果以 Git 交付输出为准。
