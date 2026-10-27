# record-render-groups-in-parallel 实施验收

下方实施验收保留 2026-10-27 apply 时记录；2026-10-28 的独立 openspec-verify-change 结果见本文末节。基线为 `0f09df95f591821970cceb8b59b6f35f505b9226`。仅本 change 的代码、测试、规划与文档纳入交付；前置 change 和主规格未同步/归档。

## 实现与设计核对

- `RhiConfig::recordingWorkerCount` 默认 0；实际 worker 为请求值和组数的较小值。Presenter 生命周期持有现有 TaskSystem，零/单/多模式共用 `recordRenderGroup`。
- 每帧槽每 worker 独占池，按组预分配 primary。零 worker 有一个内联 lane。资源数量明确为 `2 × G × max(1,min(W,G))` 个 primary；双组请求 4 时实际 2 worker、4 pool、8 primary；五组请求 4 时为 8 pool、40 primary。
- 每组只读准备快照、写自己的结果和局部 trace。批次接受后立即 wait，无可抛出工作夹在接受与等待之间；所有回调和捕获结束后才汇总异常及校验结果。
- `collectRecordedGroupCommands` 校验数量、完整性、非空/唯一 primary、组索引及 frameSlot/generation/imageIndex/planVersion，再按组序返回数组。任一失败不 reset fence、不提交/呈现、不推进状态；非标准异常也定位到组。
- 统一提交继续使用一个 VkSubmitInfo、一次 vkQueueSubmit、原 acquire wait stage、图像索引归属的 present semaphore 和槽 fence。WAW barrier 与最终转换不因 CPU 乱序录制改变。
- 正常帧等待本槽 GPU 完成后复位全部 lane 池。重建在 CPU 无使用者且 GPU 完成后复位所有槽所有池，才释放旧附件。shutdown 排空 join → GPU 等待 → pool → 附件，初始化部分创建失败沿同一所有权清理。
- 测试门控位于真实 primary begin/end 之间；独立 enter/exit 事件、最大在途数、实际 worker/thread/pool/primary 与提交数组分开检查，trace 按组序汇总不被当作时间顺序。逐帧完整诊断限制为测试前 12 帧及新代/计划首次成功提交。

## 已执行检查

| 检查 | 结果与证据 |
|---|---|
| Debug 全部 CTest | 137/137；build-parallel/ctest-Debug-verified.log，79.24 秒 |
| Release 全部 CTest | 137/137；build-parallel/ctest-Release-verified.log，78.77 秒 |
| CPU-only Debug / Release | 各 26/26；build-parallel/cpu-test-Debug.log、cpu-test-Release.log；禁用 Vulkan 查找、无效 GLFW 路径及无 _deps 条件下配置构建通过 |
| /analyze | build-parallel-analysis Release 全部目标构建通过、无警告；build-parallel/analysis-build.log |
| 产品/正常库隔离 | Debug/Release zlRenderer.exe 与 taskSystem.lib 不含 ZL_RECORDING 控制、启动/门控/兄弟失败标识；build-parallel/audit_binaries.py |
| 所有权额外核验 | 62 份 Debug/Release 日志通过实际 command 与初始化 (slot,worker,group) 映射、join 在池释放前、每次重建复位全部池检查；build-parallel/ownership-audit.json |
| ASan Release 全部 CTest | 137/137；build-parallel/ctest-ASan-verified.log，351.45 秒 |
| 最终有界日志/后组控制补测 | Debug/Release/ASan 各 12/12；ctest-*-final-controls.log，11.51 / 11.48 / 44.65 秒；全量后仅补充测试诊断限流和后组失败等待前组完成，相关路径已重新构建补测 |
| 真实窗口矩阵 | 17/17 流程通过；build-parallel/window-matrix-driver.log、window-logs-verified 下各流程的 stdout/stderr、9 张 PNG 与 verification.txt；window-audit.json 另检查像素证据与记录有界 |

新构建目录通过显式 FETCHCONTENT_SOURCE_DIR_GLFW 复用已固定的现有 GLFW 源码缓存，不重新下载依赖；这不是本次网络从零下载验收。编译器为 MSVC 19.38.33145、SDK 10.0.26100.0；普通构建保持 /W4 /WX。ASan 配置使用 /EHsc /Zi /fsanitize=address 和链接 /DEBUG；运行 PATH 仅在验收进程加入 ASan DLL 目录。未发现可用 Clang 工具链，TSan/UBSan 未运行，不能把 ASan 或压力重复作为无数据竞争证明。GLFW C 与外部驱动/layers 不在本次 C++ 插桩范围。

## 真实窗口明细

所有流程至少持续 10 秒、重复缩放、三轮最小化/恢复并正常关闭；每次完整画面以 25 个内部及边缘点采样。普通配置双清屏为 A/B 两种颜色，像素匹配最终 B，并独立检查第一组完整录制、WAW 和唯一出口。两种配置各有一个多 worker 最小化关闭流程；另含默认产品、替代色和 ASan 多 worker 双清屏。

| 证据目录（相对 window-logs-verified） | submitted / presented | 代数 | 组数 | 有界图记录 |
|---|---:|---:|---:|---:|
| ASan/multi-double | 2407 / 2407 | 9 | 2 | 19 |
| Debug/0-fifo-window | 2243 / 2243 | 9 | 1 | 19 |
| Debug/0-graph-double-window | 2108 / 2108 | 9 | 2 | 19 |
| Debug/1-fifo-window | 2331 / 2331 | 9 | 1 | 19 |
| Debug/1-graph-double-window | 2193 / 2193 | 9 | 2 | 19 |
| Debug/4-fifo-window | 2333 / 2333 | 9 | 1 | 19 |
| Debug/4-graph-double-window | 2271 / 2271 | 9 | 2 | 19 |
| Debug/minimized-multi | 2412 / 2412 | 9 | 2 | 19 |
| Debug/zlRenderer- | 2477 / 2477 | 9 | 1 | 9 |
| Release/0-fifo-window | 2405 / 2405 | 9 | 1 | 19 |
| Release/0-graph-double-window | 2327 / 2327 | 9 | 2 | 19 |
| Release/1-fifo-window | 2410 / 2410 | 9 | 1 | 19 |
| Release/1-graph-double-window | 2372 / 2372 | 9 | 2 | 19 |
| Release/4-fifo-window | 2427 / 2427 | 9 | 1 | 19 |
| Release/4-graph-double-window | 2420 / 2420 | 9 | 2 | 19 |
| Release/minimized-multi | 2457 / 2457 | 9 | 2 | 19 |
| Release/vulkanLifecycleTests-alternate-window | 2426 / 2426 | 9 | 1 | 19 |

每个测试窗口仅保留前 12 帧及新代首次提交的完整图记录（本次各 19 条），默认产品为每代一次（9 条），没有随约两千帧增长的完整逐帧 dump。两种配置的同图单/双清屏画面一致；颜色意图、load/store、barrier 和实际提交语义由 recording-compare-modes 比较，句柄等身份只在跨运行比较时归一化，单次运行内仍核对真实归属。

## 要求与场景映射

### Record complete physical groups into independent primaries

recording-0/1/4 × 1/2/5；recordRenderGroup 公共路径与实际 begin/end/完整组输入。

- Record two complete clear groups
- Keep a single group complete
- Select inline single and multiple workers

### Reuse group primaries with frame slot lifetime

recording-*-*、recording-double-rebuild、pool/primary 中途失败；资源映射及 reset 时序检查。

- Revisit a frame slot
- Rebuild with several primaries per slot
- Skip without command or fence mutation
- Use distinct pools concurrently

### Validate compiler and clear execution at distinct coverage levels

graph-*、recording-compare-modes、两配置窗口矩阵与 validation/sync 检查。

- Verify compiler cases without a GPU
- Verify two clear passes on the GPU
- Preserve the presentation regression baseline
- Compare serial and parallel rendering

### Complete the recording batch before deciding frame submission

recording-reverse、acceptance、sibling、nonstandard、multiple、group-begin/body/end、submit-later；graph-recording 的十种结果损坏反例。

- Finish in reverse order and submit in plan order
- Wait for a sibling after recording fails
- Reject incomplete recording results
- Reject scheduling before acceptance
- Fail submission after full recording

### Verify actual recording overlap with bounded evidence

recording-overlap 的在途事件及不同线程/池；recording-reject-evidence 的七种损坏反例和 graph-primary-evidence 的七种反例。

- Overlap inside actual recording
- Check evidence without relying on log order
- Report coverage and performance limits

## 覆盖限制与失败记录

真实设备为 AMD Radeon RX 7900 XT，graphics/present 同族 0、FIFO、format 50、3 图像/2 帧槽。MAILBOX 优先/FIFO 回退及分族继续按生产选择函数的合成/代码覆盖报告，不宣称 MAILBOX 或分族硬件实测。五组测试提供真实提交/同步输入证据，像素验收仍针对单/双清屏；采样绘制未提供。基础 WSI 的 WaitIdle 不等于呈现引擎完成保证。本 change 不宣称 GPU 并行或性能提升；门控延长真实录制区间，只证明 CPU 并发能力，不证明两个 Vulkan API 同时执行。

最初受限执行中新旧基线均在 vkCreateSwapchainKHR 返回 -4；正常桌面权限下通过，未修改驱动、注册表或全局 layer 配置。首次新构建引用了不存在的 GLFW 缓存后已修正为已存在缓存。两轮事件检查先于相应测试二进制重建而失败，日志 ctest-Debug-final.log/ctest-Release.log 保留；之后匹配版本的全量 verified 日志与补测分开保存，不覆盖为“从未失败”。首个持续窗口截图边缘出现黑色窄条，失败 PNG/日志保存在 build-parallel/window-logs/Debug/0-fifo-window；每次采样前重新置顶验收自身窗口后，完整流程 2243/2243 帧、9 代、25 点及三轮最小化恢复通过，原采样范围/阈值未变，后续证据单独保存在 window-logs-verified。仅按既有具体 AMD loader 消息归因，无放宽应用诊断。

## OpenSpec 与交付

`openspec validate record-render-groups-in-parallel --strict` 通过；CLI 提示 MODIFIED 的前置要求尚不在主规格中。它们已存在于完成但未同步的 prepare-render-group-recording delta，后续必须先同步前置 change，再同步本 change；本次没有绕过或代为执行同步/归档。

技术验收核对：5/5 要求、19/19 场景、6/6 设计决策已对应实现及上述分层证据。无实现范围缩减，16/16 任务完成；独立 verify、主规格同步及归档未执行。

交付目标已只读核实：origin=https://github.com/BlackJoke76/ZLRenderer_v2.git，main / origin/main；开始交付前远端与基线 SHA 一致。实现提交 [51b95eb820186c4ddaeef42399dbd12755cfcd0c](https://github.com/BlackJoke76/ZLRenderer_v2/commit/51b95eb820186c4ddaeef42399dbd12755cfcd0c) 已推送至 origin/main，git ls-remote 确认远端 SHA 与该提交一致。任务完成勾选、STATUS 和本交付回执随独立 docs(record-render-groups-in-parallel) 提交保存，最终文档提交 SHA 见 Git 历史及本轮交付输出。既有未跟踪 .agents/ 与两个 .gitkeep 均保留。

## 独立复核：2026-10-28

复核对象为已推送的 `9a32dc9618eeebaec921373c2d15ed09298736af`（实现 `51b95eb820186c4ddaeef42399dbd12755cfcd0c`），两者之间 src/tests/scripts/CMakeLists.txt 无差异。本轮未修改产品、测试、规划、任务或主规格，仅追加本报告与 STATUS。任务勾选和 apply all_done 仅作为输入，以下结论来自重新阅读规划/源码/检查器、全新构建复跑及原始证据核验。

### Summary scorecard

| 维度 | 检查及结论 |
|---|---|
| Completeness | Task Completion 16/16；Spec Coverage 5/5（3 MODIFIED、2 ADDED；无 REMOVED/RENAMED） |
| Correctness | Requirement Implementation Mapping 5/5；Scenario Coverage 19/19 |
| Coherence | Design Adherence 6/6 决策；Code Pattern Consistency 通过 |

六项适用检查均已完成，没有 Not verified、部分验证或跳过检查。环境不提供的 TSan/UBSan 和本轮未重复的 ASan/静态分析分别列入下方证据使用范围，未当作新运行或无数据竞争证明。

### 新证据与已有证据复查

| 检查 | 本轮结果与证据 |
|---|---|
| 新目录配置与构建 | build-parallel-verify Debug/Release 均无警告，保持 /W4 /WX；configure.log、build-Debug.log、build-Release.log。使用固定现有 GLFW 源码缓存，不宣称重新联网下载验收 |
| 全部 CTest | Debug 137/137（89.51 秒）、Release 137/137（82.01 秒）；ctest-Debug.log、ctest-Release.log。包括真实录制门控、逆序完成、故障汇总、重建、跨模式比较、结果损坏和证据反例 |
| CPU-only 新目录 | build-parallel-verify-cpu 两配置各 26/26（10.42/10.07 秒），关闭 renderer、禁用 Vulkan 查找并指定无效 GLFW 路径；无 _deps、无图形目标，正常库保持图形依赖隔离 |
| 新真实窗口 | 6/6，明细见下表；每个至少持续 10 秒、9 代、三轮最小化恢复、25 点采样、正常条件 5 秒内退出，创建/释放平衡，无应用 validation/sync WARNING/ERROR |
| 独立像素再核验 | audit_evidence.py 直接从 stdout 颜色及格式推导显示 RGB，再用 Pillow 重采样每张 PNG 的 25 点；原 17 流程加新 6 流程共 23 流程、207 张 PNG，全部通过，不只读取 verification.txt 的通过文字 |
| 分配、录制与提交再核验 | 同脚本审查本轮 158 份包含命令资源的 stdout：原始分配键 (slot,worker,group) 对应真实录制池/primary；同 worker 线程身份稳定、不同 worker 线程互异，实际 submit index 对应组序；join 先于池释放，重建复位所有池再释放旧附件 |
| 二进制隔离 | 新 Debug/Release 产品与正常 taskSystem.lib 均不含录制启动、门控、兄弟失败及 ZL_RECORDING 控制标识；测试目标使用单独编译的 hook 版本，普通库源码与合同未改动 |
| 既有 ASan 证据 | 已读取 build-parallel/ctest-ASan-verified.log（137/137）、ctest-ASan-final-controls.log（12/12）、ASan 最终构建和窗口证据；核对 /fsanitize=address、/Zi、/DEBUG 配置。原全量后仅测试日志限流/后组控制调整，12项补测及最终 ASan 窗口覆盖这些调整。本轮未重跑 ASan |
| 既有静态分析与工具限制 | 已核对 /analyze 配置及 analysis-build-final.log 无编译器警告/错误；本轮未重跑静态分析。MSVC 环境未发现可用 TSan/UBSan 工具链，未将其记为通过 |

新证据根目录为 `build-parallel-verify/`；`run-verification.ps1` 保存主要复跑命令，窗口各目录包含 driver.log、stdout/stderr、PNG 和 verification.txt。`independent-audit.json` 保存上述独立审查结果；`verification-manifest.json` 记录复核 HEAD、源文件及新/复用证据 SHA256。诊断白名单重新核对，仍仅允许既有具体 AMD loader 与重复 EOS layer 消息，未扩大应用错误过滤。

### 本轮窗口明细

| 目录（相对 build-parallel-verify/window-logs） | 请求 worker | submitted / presented | 代数 |
|---|---:|---:|---:|
| Debug/alternate-single | 1 | 2431 / 2431 | 9 |
| Debug/multi-double | 4 | 2448 / 2448 | 9 |
| Debug/production | 0 | 2499 / 2499 | 9 |
| Release/multi-double | 4 | 2338 / 2338 | 9 |
| Release/multi-minimized | 4 | 2390 / 2390 | 9 |
| Release/production | 0 | 2484 / 2484 | 9 |

实际设备仍为 RX 7900 XT、graphics/present 同族 0、FIFO、format 50、三图像/两帧槽。多 worker 双组请求 4、实际 2；五组请求 4、实际 4 的调用/复用由 CTest 另验。MAILBOX/分族仍是合成策略/代码覆盖；采样绘制、GPU 并行、呈现引擎完成和净性能提升没有被本复核宣称。门控在真实 primary begin/end 区间产生受控 CPU 重叠，不等于同时执行两个 Vulkan API。

### 要求映射

| 要求 | 主要实现定位 | 验证 |
|---|---|---|
| Record complete physical groups into independent primaries | src/rhi.h:9、src/rhi.cpp:45；src/vulkanPresenter.cpp:286、363；src/taskSystem.cpp:47、75 | 0/1/4 worker × 1/2/5组、新单/双清屏窗口及完整 primary/组输入 |
| Reuse group primaries with frame slot lifetime | src/vulkanPresenter.cpp:94、315、498、632、726、753 | 分配归属审计、多槽12帧、double-rebuild、窗口9代、pool/primary中途失败 |
| Validate compiler and clear execution at distinct coverage levels | src/renderGraph.cpp；src/vulkanRenderGraph.cpp:144；src/vulkanPresenter.cpp:363、444 | 图编译/后端映射、跨模式参数比较、原17与新6窗口、同步validation，明确各层限制 |
| Complete the recording batch before deciding frame submission | src/vulkanPresenter.cpp:326、338、349、444、548；src/vulkanRenderGraph.cpp:5；src/taskSystem.cpp:23、75 | 全批等待后校验与单次提交；标准/非标准/多异常、接受前失败、submit-later及十种无效结果 |
| Verify actual recording overlap with bounded evidence | src/vulkanPresenter.cpp:391、438；tests/recordingTestHooks.cpp:77、103、127、134；tests/checkParallelEvidence.cmake | 真实录制 enter/exit事件、不同 worker/thread/pool、逆序完成、有界日志及损坏证据拒绝 |

### 19 个场景逐项核对

| 场景 | 对应代码/测试证据 | 结论 |
|---|---|---|
| Record two complete clear groups | recordRenderGroup；presentation-graph-double、recording-4-2及新多worker双清屏；实际两primary、WAW与唯一出口 | 通过 |
| Keep a single group complete | recordRenderGroup；recording-0/1/4-1、新两配置产品窗口，完整入口/渲染/出口 | 通过 |
| Select inline single and multiple workers | RhiConfig→initialize→TaskSystem::submitBatch；recording-0/1/4×1/2/5；纯CPU批次/线程归属测试 | 通过 |
| Revisit a frame slot | renderFrame fence等待→recordGraphFrame reset全部本槽池；12帧循环与分配/提交归属审计 | 通过 |
| Rebuild with several primaries per slot | rebuildSwapchain 全池reset；recording-double-rebuild及新双清屏9代窗口 | 通过 |
| Skip without command or fence mutation | renderFrame在准备/录制前处理跳帧；超时、NOT_READY/OUT_OF_DATE、SUBOPTIMAL、零extent、最小化及关闭回归 | 通过 |
| Use distinct pools concurrently | WorkerCommandResources[workerIndex].primary[groupIndex]；overlap实际不同池/线程，五组复用独立句柄 | 通过 |
| Verify compiler cases without a GPU | renderGraphTests的RAW/WAR/WAW/读取者/内容/引用/组序；host-only后端映射测试 | 通过 |
| Verify two clear passes on the GPU | graph-double A/B完整render pass输入；新Debug/Release双清屏25点B及同步诊断 | 通过 |
| Preserve the presentation regression baseline | 原17与新6窗口包括两配置产品、替代色、持续10秒、9代、三轮恢复及最小化关闭 | 通过 |
| Compare serial and parallel rendering | recording-compare-modes九种组合的实际barrier/附件/submit语义；原窗口完整0/1/多模式像素矩阵重新采样 | 通过 |
| Finish in reverse order and submit in plan order | beforeRecordingTestEnd门控，recording-reverse确认order=1,0而真实数组仍0,1 | 通过 |
| Wait for a sibling after recording fails | TaskBatchHandle::wait全批；recording-sibling/nonstandard/multiple事件收敛、摘要3帧及fence计数 | 通过 |
| Reject incomplete recording results | collectRecordedGroupCommands；graph-recording十种损坏结果反例，调用在reset fence/submit之前 | 通过 |
| Reject scheduling before acceptance | TaskSystem::submitBatch全批接受点；recording-acceptance在已有3帧后拒绝且无第4帧推进 | 通过 |
| Fail submission after full recording | submitGraphFrame checkResult与hasSubmittedWork成功边界；recording-submit-later的3帧/4次reset及非零清理 | 通过 |
| Overlap inside actual recording | 真实vkBegin之后/vkEnd之前RecordingTestScope，overlap最大在途2、gate-timeout有界失败并排空 | 通过 |
| Check evidence without relying on log order | 独立事件序列与线程/池身份；recording-reject-evidence七种反例、graph-primary-evidence七种反例 | 通过 |
| Report coverage and performance limits | 本报告与STATUS/ARCHITECTURE区分CPU、真实GPU、合成策略、控制注入及未测工具，不声称GPU并行/加速 | 通过 |

### 六项设计决策与项目模式

1. 显式 worker 配置、默认零、按组数限制：rhi.h:9 → rhi.cpp:45 → VulkanPresenter::initialize；执行器由 presenter 持有，普通库/应用与测试注入隔离，符合设计。
2. 每槽每 worker 独占池，按组预分配 primary：createFrameResources 与 WorkerCommandResources，稳定 `(slot,worker,group)` 归属；复用用集合身份判断，未把组跨帧换 worker 误判为重新分配，符合设计。
3. 一条录制函数、隔离结果：recordGraphFrame 固定快照和结果存储后提交任务，每个回调只写自身元素；所有模式调用 recordRenderGroup。submitBatch 后紧接 wait，没有插入可抛出的协调工作；回调失败由 TaskSystem 捕获并等待全部捕获释放，符合设计。
4. 全批成功后一次有序提交：结果校验、trace拼接都在reset fence之前，orderedCommands按groupIndex构造；worker无queue/present/frame推进调用，原WAW和出口不变，符合设计。
5. 回收先CPU再GPU再解除引用：shutdown排空join后waitForSubmissions，再释放池和附件；rebuild为同步帧入口，无CPU批次跨帧在途，GPU等待后复位全部池，符合设计。
6. 分层证据：真实录制内门控、独立enter/exit事件、按组汇总trace、实际submit与像素分别检查；控制由协调线程一次解析，测试共享门控使用mutex/CV；证据反例、新复跑和像素再采样均通过，符合设计。

Code Pattern Consistency：实现范围与29文件交付范围核对，新增函数/变量使用lowerCamelCase，资源名称表达pool/primary/frame角色；原有准备/映射、TaskSystem和recordRenderGroup被复用，未复制串并行录制或提交路径。对象在初始化阶段预分配，稳定帧无新增Vulkan对象/WaitIdle，未引入secondary、额外队列、通用调度器、采样绘制或计划热替换。测试单独编译，代码所有权与项目既有presenter清理方式一致。无显著模式偏离。

### Issues by priority

- CRITICAL：0。
- WARNING：0。
- SUGGESTION：0。

### Final assessment 与后续边界

六项检查全部通过，实现独立复核通过，无未验证检查。可进入后续规格同步/归档流程，但必须沿已声明顺序先同步 prepare-render-group-recording，再同步本 change。CLI strict validate通过后输出的“MODIFIED header not found”提示，源于前置delta的两个要求尚未进入主规格；本轮已经定位前置delta第46、74行确认该依赖，不将它误判为缺失实现，也不绕过归档检查。未同步或归档任何 change。

复核前已确认 origin=https://github.com/BlackJoke76/ZLRenderer_v2.git，main / origin/main，远端与复核对象9a32dc9一致。复核结果仅更新本报告及交接记录，按已有项目授权保存为独立文档提交；最终提交SHA和远端同步结果见Git历史及本轮交付输出。既有未跟踪 .agents/ 与两个 .gitkeep 保持。
