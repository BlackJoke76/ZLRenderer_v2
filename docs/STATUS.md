# 实际进度

2026-10-28，`openspec-verify-change` 独立复核 `record-render-groups-in-parallel` 通过：[完整报告](../openspec/changes/record-render-groups-in-parallel/verification.md#独立复核2026-10-28)。Task Completion 16/16、Spec Coverage/实现映射5/5、场景19/19、设计6/6及代码模式核对通过；六项适用检查全部执行，0 critical / warning / suggestion，无未验证或跳过检查。复核基线为已推送9a32dc9618eeebaec921373c2d15ed09298736af（实现51b95eb）；本轮未修改产品或测试代码。

全新build-parallel-verify Debug/Release无警告构建，各137/137 CTest（89.51/82.01秒）；新CPU-only目录禁用图形依赖，两配置各26/26（10.42/10.07秒）。新6个真实窗口流程均通过：两配置默认产品、Debug替代色单worker、两配置多worker双清屏、Release多worker最小化关闭；分别2499、2484、2431、2448、2338、2390帧，均9代、至少10秒、25点、三轮恢复和正常条件5秒内退出。独立脚本对原17加新6流程共207张PNG重新按实际颜色/格式计算显示值并采样，全部通过；158份本轮日志的原始分配→实际录制→实际submit归属、线程稳定性与清理时序另行核对。新日志/PNG、审查JSON和SHA256清单在build-parallel-verify。

本轮复查已有ASan 137/137与最终12/12补测、ASan窗口、/analyze无警告及其实际配置，未重复运行ASan/静态分析；MSVC未发现可用TSan/UBSan，不将这些未运行项当作通过或数据竞争证明。硬件仍RX7900XT、同族0、FIFO、format50、3图像/2帧槽；受控CPU重叠、真实GPU清屏、合成MAILBOX/分族、故障注入与基础WSI限制保持区分。strict校验通过；后续仍须先同步prepare-render-group-recording再同步本change。本轮只保存复核报告和交接，不同步/归档；文档提交与远端SHA以本轮交付结果为准，既有.agents/和两个.gitkeep保留。

2026-10-27，`record-render-groups-in-parallel` 实施技术验收通过，16/16 任务完成，已提交推送 main。[完整验收报告](../openspec/changes/record-render-groups-in-parallel/verification.md) 对应 5 条要求、19 个场景和 6 项设计决策。RhiConfig 显式配置 0/1/多 worker，默认零内联；复用 TaskSystem 和同一 recordRenderGroup。每帧槽每 worker 独占池及预分配 primary，整批结束后校验结果、按计划组序一次提交；任务异常、缺失/重复/未完成结果均阻止半帧提交，先收敛 CPU 使用者再清理。

新 build-parallel Debug/Release 各 137/137 CTest（79.24/78.77 秒），ASan Release 137/137（351.45 秒）；最终有界日志及后组控制补测三配置各 12/12。CPU-only Debug/Release 各 26/26，在禁用 Vulkan 查找、无效 GLFW 路径、无 _deps 条件下通过。/analyze 全部目标无警告；普通产品/任务库不包含测试控制。17/17 真实窗口流程通过：两配置 0/1/4 请求 worker × 单/双清屏、两配置多 worker 最小化关闭、默认产品、替代色与 ASan 多 worker 双清屏；均至少 10 秒、9 代、25 点、三轮最小化恢复、正常条件五秒内退出，资源平衡且无应用 validation/sync WARNING/ERROR。实际线程重叠用真实 primary begin/end 区间的门控、进入/退出事件及不同 worker/thread/pool 证明；逆序完成仍按计划提交，损坏证据拒绝，故障测试明确属于控制覆盖。

证据在忽略目录 build-parallel*，窗口日志/PNG 在 build-parallel/window-logs-verified。最初受限环境中新旧基线都在 swapchain 创建返回 -4，正常桌面权限下通过；一次持续截图边缘黑条的失败证据保留在 window-logs，逐次将验收自身窗口置顶后通过，未改变采样点或阈值。匹配事件检查器前的两次旧二进制失败日志亦保留，详见报告。真实 RX7900XT、同族0、FIFO、format50、3图像/2帧槽；MAILBOX/分族为合成或代码覆盖。基础WSI呈现引擎完成限制、无采样绘制、MSVC无可用TSan/UBSan均明示，不宣称 GPU 并行或性能提升。

本 change 未独立 verify、未同步主规格或归档；后续同步须先处理已完成但尚未同步的 prepare-render-group-recording，再处理本 change。已核实 origin=https://github.com/BlackJoke76/ZLRenderer_v2.git，main / origin/main，交付前远端与基线 0f09df95f591821970cceb8b59b6f35f505b9226 一致。仅提交本 change 成果，既有 .agents/ 与两个 .gitkeep 保留；实现提交 [51b95eb820186c4ddaeef42399dbd12755cfcd0c](https://github.com/BlackJoke76/ZLRenderer_v2/commit/51b95eb820186c4ddaeef42399dbd12755cfcd0c) 已推送至 origin/main，git ls-remote 核对一致；本交付回执及最终任务勾选随独立 docs 提交保存，最终文档 SHA 见 Git 历史及交付输出。

2026-10-27，`add-recording-task-system` 已同步主规格并归档（spec-driven）：目录为 `openspec/changes/archive/2026-10-27-add-recording-task-system/`，[完整验收与独立复核](../openspec/changes/archive/2026-10-27-add-recording-task-system/verification.md)。归档前4/4规划材料、15/15任务完成，2026-10-26独立复核0 critical/warning/suggestion。新增 `openspec/specs/cpu-task-execution/spec.md`，Purpose、6条要求和19个场景与归档delta逐项完全一致，仅转换标题与Requirements章节；其余5份主规格SHA256不变。

归档6个文件（含.openspec.yaml）移动前后SHA256逐一一致，校验清单保存在忽略目录 `build-task-system-verify/archive-manifest-20261027.json`。全部6份主规格和6个归档change任务记录严格校验通过；当前只保留另一个已完成但未归档的 `prepare-render-group-recording`，未修改其记录。本轮仅同步规格、归档和更新交接/路线图，未改产品代码或重新运行构建/GPU测试；此前复核数据和工具限制仍有效。

本归档以独立提交推送至已核实的 origin/main（upstream origin/main，https://github.com/BlackJoke76/ZLRenderer_v2.git），最终SHA及远端核对结果见本轮交付输出。既有未跟踪.agents/与两个.gitkeep保留。下方实施及复核段落保留当时状态，当前状态以本段为准。

2026-10-26，`openspec-verify-change add-recording-task-system` 独立复核通过：[完整复核](../openspec/changes/archive/2026-10-27-add-recording-task-system/verification.md)。完整性15/15任务与6/6新增要求；正确性6/6实现映射、19/19场景；一致性7/7设计决策及代码模式通过。六项适用检查均执行，0 critical / warning / suggestion，无跳过检查。本轮未修改产品/测试代码，未同步主规格或归档。

全新build-task-system-verify CPU-only目录在图形查找/下载禁用条件下Debug/Release各26/26（10.12/10.03秒），新ASan目录26/26（10.66秒），新/analyze与BUILD_TESTING=OFF仅库构建通过。默认图形配置Debug/Release各104/104（58.21/54.09秒）。每模式关键案例各100轮，目标/链接无图形依赖、无_deps及正常库无测试钩子均重新核对。新日志位于build-task-system-verify*，旧实施证据另保留。TSan/UBSan及像素窗口流程未运行的既定限制保持，不把CPU证据当作Vulkan并行录制或GPU完成证明。复核基线75dc398、实现6994f04；本次仅提交复核报告和交接，目标为已核实origin/main，最终提交与推送SHA见交付输出。

2026-10-26，`add-recording-task-system` 纯CPU任务执行器实施及技术验收完成，当前活动change未同步主规格或归档。[验收报告](../openspec/changes/archive/2026-10-27-add-recording-task-system/verification.md) 逐项关联19个场景、测试与证据。TaskSystem提供任务批次、零worker内联/固定worker执行、稳定worker索引、完成句柄、按任务索引保留异常、等待全批、排空关闭及析构；句柄丢弃不取消工作，保留句柄可跨执行器生命周期。一个协调线程提交/关闭，回调不重入同执行器控制接口，任务必须最终终结，不提供取消或任务依赖图。

独立新目录 `build-task-system` 在禁用Vulkan查找、图形下载断开、GLFW源无效的控制条件下配置成功；CPU目标与测试不引用/链接Vulkan/GLFW/RenderGraph，无_deps，关闭测试时仅构建任务库。CPU Debug/Release各26/26（9.94/9.86秒），ASan Release 26/26（10.83秒），库和两测试目标/analyze无警告；0/1/4 worker的关键等待/批次/丢句柄/异常/关闭各100轮。四worker门控证明4个真实线程同时在途及后续索引稳定；创建第0/1/3个worker前与批次接受前故障注入只证明host控制和已启动线程回收，不宣称OS/分配器自然失败。正常库二进制不包含测试钩子。

默认renderer=ON Debug/Release各104/104（50.63/46.65秒），即原78项回归加26项任务测试。所有既有renderer源文件及串行录制调用链无改动，任务库未接入应用；本轮未重跑像素窗口流程，也不宣称Vulkan并行录制或性能收益。当前MSVC环境无可用Clang工具链，TSan/UBSan未运行，不把ASan或压力重复当作数据竞争证明。原始日志及生成项目在忽略的build-task-system*目录，详细命令、案例、控制条件和限制见报告。

15/15任务完成。实施提交 [6994f0404dbfc249d4799e8968e734980fa58fd4](https://github.com/BlackJoke76/ZLRenderer_v2/commit/6994f0404dbfc249d4799e8968e734980fa58fd4) 已推送至origin/main，git ls-remote核实SHA一致；目标origin=https://github.com/BlackJoke76/ZLRenderer_v2.git，分支main/upstream origin/main。本最终任务勾选与回执随独立文档提交保存，最终回执SHA以Git交付输出为准。既有未跟踪.agents/和两个.gitkeep保持。`prepare-render-group-recording`仍保持原完成/未归档状态。

2026-10-22，`prepare-render-group-recording` 实施与技术验收通过，当前保持活动状态，未同步主规格或归档。[验收报告](../openspec/changes/prepare-render-group-recording/verification.md) 包含规格对应、原始证据位置和限制。帧准备、完整组录制、统一提交已分开；每帧槽预分配每物理组一个 primary，串行录制后按组序一个 VkSubmitInfo、一次 vkQueueSubmit。保留获取等待、组间 WAW、最终 present 转换、帧槽复用及全部命令引用解除后的旧代清理。后组失败不部分提交，成功提交后才推进状态。

Debug/Release 各78/78 CTest；ASan Release 78/78（211.67秒），/analyze 六个C++目标无警告。八个窗口流程全部通过：Debug 默认2274/2274、最小化关闭2318/2318、替代色2099/2099、双清屏1462/1462；Release 对应2227/2227、2300/2300、2110/2110、1586/1586，均9代，25点颜色、至少10秒连续呈现、三轮最小化恢复、暂停冻结/恢复推进、5秒内退出0与资源平衡通过。Debug/Release 故障测试包括后组begin/body/end、分配前、reset-fence前、已提交3帧后submit失败、双组重建部分失败及NOT_READY；五类实际记录反例均被拒绝。完整日志/PNG在忽略目录 build-render-graph-verify/group-window 与 test-logs，ASan/静态分析构建目录继续独立。

实际 RX7900XT、同族0、FIFO、format50、3图像/2帧槽；MAILBOX/分族仍分别为纯选择/合成或代码覆盖。基础WSI的WaitIdle呈现引擎完成限制、采样绘制未提供、MSVC未运行UBSan均保持。只按原有具体AMD loader消息归因，没有放宽应用诊断规则。使用Windows PowerShell运行既有窗口脚本以满足System.Drawing引用，未修改产品范围。

17/17任务完成。实施提交 [086647f7f612d5be1d23b9b9260ab79922e78cb8](https://github.com/BlackJoke76/ZLRenderer_v2/commit/086647f7f612d5be1d23b9b9260ab79922e78cb8) 已推送到 origin/main，git ls-remote核对远端SHA一致。目标为 origin=https://github.com/BlackJoke76/ZLRenderer_v2.git，main / origin/main。本交付回执及最终任务勾选随独立文档提交保存，最终回执SHA见Git交付输出。既有未跟踪 .agents/ 与两个 .gitkeep 保留。

2026-10-22，`add-minimal-render-graph` 已归档（spec-driven）：[完整报告](../openspec/changes/archive/2026-10-22-add-minimal-render-graph/verification.md)，归档目录为 `openspec/changes/archive/2026-10-22-add-minimal-render-graph/`。归档前20/20任务、全部规划材料完成，已核对2026-10-18独立复核结果；2026-10-19同步的 render-graph 主规格与delta的 Purpose、9条要求和30个场景完全一致。6个change文件（含 `.openspec.yaml`）移动前后SHA256一致，5份主规格及5个归档任务记录严格校验通过，当前活动change为空。哈希清单保存在忽略目录 `build-render-graph-verify/archive-manifest-2026-10-22.json`。

本次仅归档及更新交接、路线图，未改产品代码或主规格，未重新运行构建/GPU测试；此前真实清屏、CPU/参数覆盖与基础WSI限制保持。提交推送目标为已核实的origin、main / origin/main，最终SHA及远端核对结果见本轮交付输出；既有未跟踪 .agents/ 与两个 .gitkeep 保留。下方实施、复核和同步记录保留当时状态，当前状态以本段为准。

2026-10-19，`openspec-sync-specs add-minimal-render-graph` 完成：新增主规格 `openspec/specs/render-graph/spec.md`，9条新增要求、30个场景及 Purpose 与已复核 delta 逐项一致，仅转换标题和 Requirements 章节格式，无 TBD 或 delta 操作标题；其余4份主规格保持不变。全部5份主规格及本change严格校验通过。本次仅同步规格和交接文档，未修改产品代码或重新运行构建/GPU测试，2026-10-18独立复核结果及覆盖限制保留。change仍在活动目录，20/20任务完成，未归档。提交推送目标为已核实的origin、main / origin/main，最终SHA见本轮Git交付输出；既有未跟踪 .agents/ 与两个 .gitkeep 保留。下方记录保留各阶段当时状态。

2026-10-18，`openspec-verify-change add-minimal-render-graph` 独立复核通过：20/20任务、9/9要求、30/30场景、8/8设计决策及代码模式核查通过，0 critical / warning / suggestion，无适用检查未验证项。复核对象为已推送实现提交 `893c1b207d3ff23cc305d48e4a0df4b232c2141e`，未修改产品代码。新目录 `build-render-graph-verify` Debug/Release 无警告构建，各68/68 CTest（45.52/39.03秒）；四个新窗口流程分别2288/2288、1926/1926、2258/2258、1938/1938帧，均9代，覆盖生产可见/最小化关闭和两配置双清屏。25点颜色、持续呈现、缩放、三轮暂停冻结/恢复推进、5秒内退出0、资源平衡、实际图参数及应用validation/sync检查通过。新日志/截图在该目录 test-logs、window-logs。

已核对原八个普通窗口流程、ASan 68/68及两窗口、/analyze构建证据，本轮未重新运行替代色、ASan或静态分析。硬件仍为RX 7900 XT、同族0、FIFO、format50、3图像/2帧槽；CPU/参数覆盖及基础WSI限制保持明示。详细定位与结果见 change 的 verification.md 独立 verify 章节。本轮仅提交复核报告和交接记录，目标为已核实的origin、main / origin/main，最终提交与推送SHA见Git输出；主规格未同步、未归档。既有未跟踪 .agents/ 与两个 .gitkeep 保留。

2026-10-18，`add-minimal-render-graph` 实施验收通过，20/20任务完成：有序资源使用声明、RAW/WAR/WAW 与内容有效性分析、一对一物理执行组、显式图像边界同步、当前代绑定与真实参数诊断已接入生产清屏。图/不可变计划由 VulkanBackend 持有；Presenter 保持原 WSI 所有权。自动 pass 合并、采样绘制等仍未实现，当前 change 未同步主规格、未归档。完整逐项报告见 `openspec/changes/add-minimal-render-graph/verification.md`。

独立 `build-render-graph` Debug/Release 无警告构建，各 68/68 CTest（39.97/41.70秒）；内容负例补强后目标复测通过。`build-render-graph-asan` Release 68/68（87.37秒）与默认/双清屏窗口通过，无 ASan 报告；`build-render-graph-analysis` 六个 C++ 目标静态分析无警告。MSVC 未运行 UBSan。两普通配置各默认可见/最小化关闭、替代色、双清屏，共8个窗口流程，加ASan两个，共10个流程均9代，25点完整颜色、至少10秒连续呈现、三轮最小化恢复、暂停冻结/恢复推进、5秒内退出0、资源平衡与应用validation/sync诊断通过。原始日志/截图在上述目录的 test-logs、window-logs，具体计数见报告。

本轮实际仍为 RX 7900 XT、同族0、FIFO、format50、3图像/2帧槽；MAILBOX优先/分族为生产策略的合成覆盖。CPU/参数支持不代表采样绘制等已GPU实测，基础WSI的呈现引擎完成限制保持。Git目标已核实 origin、main / origin/main，实施前本地和远端同为 `2590e4c58d7c055cd0b0b647ba9345387b9bd579`；最终独立提交SHA及推送结果见Git历史和交付输出。既有未跟踪 .agents/ 与两个 .gitkeep 保留。

## 历史交接记录（以下保留当时状态）

2026-10-17，按用户要求回滚 `render-slang-triangle`。以新的逆向提交撤销该change的实现、交付、独立复核、规格同步和归档五个提交（f2ee745、11878bf、3ace840、c5f8abf、b213ab3），保留Git历史；除本回滚交接记录外，所有受跟踪文件恢复到 `d5adb950dee00495af40f3581d88733d6eb87b21`，此前 `handle-swapchain-resize` 的oldSwapchain整代重建保持。Slang构建依赖、三角形代码、triangle-rendering主规格和该change归档文件已撤回，当前无活动change。

回滚验证：重新配置 `build-resize-verify`，Debug/Release无警告构建，各50/50 CTest通过（35.35/39.62秒）；4份保留主规格严格校验通过。Debug真实窗口正常关闭2361/2361帧、Release最小化关闭2386/2386帧，均9代，重复缩放/三轮最小化恢复、25点完整清屏、暂停计数冻结、5秒内退出0及资源平衡通过，无未知应用validation/sync WARNING/ERROR。新原始日志为该目录 `rollback-configure.log`、`rollback-build-<配置>.log`、`rollback-ctest-<配置>.log`、`rollback-window-logs/<配置>/`；未重跑ASan/静态分析。既有FIFO同族硬件、合成覆盖及基础WSI限制不变。提交推送目标为已核实的origin、main / origin/main，最终SHA见Git输出；原始三角形成果仍可通过历史提交恢复。既有未跟踪 `.agents/` 和两个 `.gitkeep` 保留。

2026-10-17，`handle-swapchain-resize` 已同步主规格并归档至 `openspec/changes/archive/2026-10-17-handle-swapchain-resize/`（spec-driven）。`frame-presentation` 修改3条、新增1条需求，25个delta场景逐项一致；保留标题、Purpose及其余5条需求。4份主规格与归档前change严格校验通过，6个归档文件（含 `.openspec.yaml`）移动前后SHA256一致，19/19任务完成。此次仅同步规格、归档和更新交接文档，未重跑产品构建或窗口测试；2026-10-15复核结果及基础WSI限制继续有效。提交推送目标为已核实的 origin、main / origin/main，最终SHA以Git历史及交付输出为准。下方实施/复核段落保留当时状态。

2026-10-15 最新复核：`openspec-verify-change handle-swapchain-resize` 通过。完整性19/19任务、4/4 delta要求；正确性25/25场景已核查；一致性5/5设计决策及代码模式通过，0 critical / warning / suggestion，无跳过检查。新建 `build-resize-verify`，Debug/Release 构建无警告且各50/50 CTest；两配置可见关闭和最小化关闭四个窗口流程均通过（2382/2382、2389/2389、2397/2397、2381/2381帧，各9代）。25点客户区/边缘采样、三轮暂停冻结/恢复推进、5秒内退出0、资源平衡及应用validation/sync诊断均已核对。新原始证据在该目录 `test-logs`、`window-logs`、`minimized-close-logs`；详见 change 的 verification.md 新增复核章节。历史ASan/静态分析证据已核对，本轮未重跑。FIFO同族硬件、合成覆盖及基础WSI限制保持明示；未同步主规格或归档。此次仅提交本change的复核报告和交接记录，最终提交/推送SHA见Git交付输出。

最近完成 change：`handle-swapchain-resize`（已同步主规格并归档，当前无活动 change）。2026-10-14 实施、验收及 Git 交付通过，19/19 项完成；2026-10-17 已同步主规格并归档。已按用户要求通过 oldSwapchain 创建新链，完成新一代附件后切换并清理旧代，保留两个帧槽；等待后复查尺寸/关闭、零 extent 延迟和重复最小化恢复已验证。Debug/Release/ASan 各 50/50 CTest 通过，/analyze 无警告，五个实际窗口流程通过，详细报告见下方。

本机 AMD Radeon RX 7900 XT，graphics/present 族均为 0；当前 surface 模式选择实际为 FIFO 回退，3 张图像，格式 B8G8R8A8_SRGB（50）。MAILBOX 优先由纯选择测试覆盖；分族 sharing 和配置变化是合成输入/代码检查，未宣称真实分族或 MAILBOX 硬件实测。

基础 WSI 限制按提案保留：稳定帧的 present semaphore 按图像和再次 acquire 同步复用；退出/简化重建采用常用 WaitIdle 清理，它证明 GPU 提交完成，不能严格证明呈现引擎完成。未启用 maintenance1/present fence，validation 无错误不能作为补足此保证的证据。细节见 ARCHITECTURE。

## handle-swapchain-resize 验收与交接

报告：`openspec/changes/archive/2026-10-17-handle-swapchain-resize/verification.md`，4 条 delta 要求、25 个场景逐项关联实际/合成/控制及代码证据；严格校验与 git diff --check 通过。共享创建/释放用于初建与候选代，unique_ptr 保护候选异常清理；创建调用后的旧链已退役，失败停止正常循环并清理两代，不回滚 acquire 旧链。

独立 `build-resize` 的 Debug/Release 无警告构建与各 50/50 CTest 通过。使用现有固定 GLFW 3.4 源码目录配置独立产物，未重新下载；完整 build/ctest 汇总在该目录的 `build-<配置>.log` 与 `ctest-<配置>.log`，原始测试诊断在 `test-logs/<配置>/`。连续 1→2→3 真交换链参数链、附件存活与帧归属、两个帧槽身份、候选各阶段失败均核验；创建失败的调用前注入只验证 host 控制，不声称真实驱动失败/退役。

| 实际窗口流程 | submitted/presented | 代数 | 证据目录 |
|---|---|---|---|
| Debug 三次最小化恢复、可见关闭 | 2393/2393 | 9 | build-resize/final-window-logs/Debug |
| Debug 三次恢复后最小化关闭 | 2348/2348 | 9 | build-resize/final-minimized-close-logs/Debug |
| Release 三次恢复、可见关闭 | 2360/2360 | 9 | build-resize/final-window-logs/Release |
| Release 三次恢复后最小化关闭 | 2342/2342 | 9 | build-resize/final-minimized-close-logs/Release |
| ASan Release 三次恢复、可见关闭 | 2341/2341 | 9 | build-resize-asan/final-window-logs/Release |

每流程持续无输入至少 10 秒，逐尺寸匹配实际 framebuffer/客户区/extent，在同一 DPI 上下文查询并保存物理客户区 PNG 与25点内部/边缘颜色采样（RGB 63,89,124）；快速缩放最终收敛，稳定阶段不增加代计数；每轮暂停/恢复快照的提交/呈现计数相同，恢复后继续推进。可见与最小化关闭都在正常设备条件下 5 秒内退出 0，资源计数平衡，无未知应用 validation/sync WARNING/ERROR。脚本只将自己启动的窗口置顶，不操作其他窗口。

`build-resize-asan`：/EHsc /Zi /fsanitize=address、链接 /DEBUG，Release 50/50 CTest 与窗口通过；ASan DLL 路径只设于验证进程 PATH。`build-resize-analysis`：/EHsc /analyze，四个 C++ 目标无警告。未发现可用 Clang/UBSan，MSVC 不提供 UBSan，未宣称该项通过。测试目标显式 layer settings 启用 sync validation，生产窗口脚本给子进程 VK_LAYER_VALIDATE_SYNC=1；未修改驱动或全局配置。

实际硬件仍为 RX 7900 XT、同族 0、FIFO、格式 50、3 图像/2 帧槽。MAILBOX、分族及跨代格式/图像数量变化保持合成/代码核查性质。保留原始具体 AMD loader 诊断，没有扩大过滤。基础 WSI WaitIdle 限制保持明示，oldSwapchain 不补足呈现引擎完成保证；扩展路径仍另行规划。

交付目标已确认：`https://github.com/BlackJoke76/ZLRenderer_v2.git`，main / origin/main。实施提交 [`5921f80e7577a896b5a66c62c6e98769b6d48de7`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/5921f80e7577a896b5a66c62c6e98769b6d48de7) 已推送，git ls-remote 核对远程 SHA 一致。只提交本 change 的源码、测试、规划、报告及文档；既有 `.agents/`、两个 `.gitkeep` 保留。19/19任务与交付回执随本 change 的后续独立交接提交保存，最终回执 SHA 以 Git 历史及交付输出为准。

## present-clear-frame 验收与交接

完整报告：`openspec/changes/archive/2026-10-12-present-clear-frame/verification.md`，覆盖8条新增帧呈现要求、3条context变更要求、1条文档变更要求，共34个场景。全部24/24任务完成，实施、验证与Git交付均已核对。实施与复核阶段未同步主规格；随后同步归档的结果见下方。

全新 `build-present-clean` 从头配置和 Debug/Release 构建通过，/W4 /WX；各34项测试包含生产策略、真实帧对象身份复用、缺项/阶段回滚、重建和错误控制。测试目标用VK_EXT_layer_settings显式启用同步validation，生产窗口脚本使用SDK进程变量，不改全局配置。Release产品无测试环境变量/注入标识。

| 实际窗口验收 | submitted/presented | 证据目录（原始日志、截图、verification.txt） |
|---|---|---|
| Debug持续颜色、缩放、最小化恢复、关闭 | 2036/2036 | build-present-clean/window-logs/Debug |
| Debug最小化期间关闭 | 1998/1998 | build-present-clean/minimized-close-logs/Debug |
| Release持续颜色、缩放、最小化恢复、关闭 | 2044/2044 | build-present-clean/window-logs/Release |
| Release最小化期间关闭 | 2000/2000 | build-present-clean/minimized-close-logs/Release |
| Release替代颜色（测试目标） | 2064/2064 | build-present-clean/alternate-color-logs/Release |
| ASan Release完整窗口流程 | 2068/2068 | build-present-asan/window-logs/Release |

每次无输入至少10秒，重复缩放至5代，9点采样默认RGB63,89,124、替代RGB124,63,89；退出0、资源创建/释放平衡，只有既有具体AMD loader诊断，无未知应用validation/sync错误。截图DPI修正后重验；一次并发窗口遮挡失败截图/日志保留，最终顺序运行通过。

`build-present-asan`：/EHsc /Zi /fsanitize=address，链接/DEBUG，34/34回归及窗口通过，无主机内存问题。`build-present-analysis`：/EHsc /analyze，四个C++目标无警告。未安装Clang，MSVC无UBSan，本次未运行或宣称UBSan通过。未修改驱动或全局配置。

交付目标已确认：`https://github.com/BlackJoke76/ZLRenderer_v2.git`、`main`、upstream `origin/main`。只提交本change成果，未跟踪的 `.agents/` 与两个既有 `.gitkeep` 保留。实现提交 [`4dfcc6512689c4c2818e2c3c03fa042c38b55aaa`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/4dfcc6512689c4c2818e2c3c03fa042c38b55aaa) 已推送，git ls-remote确认main一致。任务完成与交付记录随单独docs(present-clear-frame)提交保存，最终文档SHA以Git历史及交付输出为准。

2026-10-12，`openspec-verify-change` 独立复核 `present-clear-frame`：24/24任务、12/12要求、34/34场景有对应证据，7项设计与代码模式核查通过；0 critical / warning / suggestion，无跳过检查。重新构建Debug/Release，各34/34 CTest；两种配置的正常/最小化关闭和Release替代颜色窗口验收均通过。新证据在 `build-present-clean/verify-logs/`，详细计数及代码定位见归档内的verification.md。该次复核只更新报告和交接记录，未同步或归档。

真实硬件仍为FIFO、族0、3图像/2帧槽；MAILBOX/分族的合成覆盖与基础WSI清理限制保持明示。替代色截图两次被聊天界面遮挡，失败证据保留；通过忽略目录内的临时脚本仅将验收自身窗口置顶后重验通过，仓库脚本与产品代码未改动。已核对先前ASan记录及静态分析构建参数，该轮未重跑。复核提交 [`114c73a9781ce5f68d1454c8f0bc136538b9c921`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/114c73a9781ce5f68d1454c8f0bc136538b9c921) 已推送main并核对远程SHA。

2026-10-12，沿用用户“同步主规格后归档”的选择，完成 `present-clear-frame` 归档（schema：spec-driven）。新增 `openspec/specs/frame-presentation/spec.md` 的8条要求；更新 `vulkan-context` 的3条要求与 `project-onboarding` 的1条要求。12条delta要求和34个场景逐项与主规格比对一致；新Purpose原文保留，既有主规格标题、Purpose及未涉及要求不变，无delta操作标题或TBD占位。

同步后的4份主规格严格校验全部通过，归档前change严格校验通过。8个change文件（含 `.openspec.yaml`、三份delta及verification）移动前后SHA256完全一致，归档路径为 `openspec/changes/archive/2026-10-12-present-clear-frame/`，active changes为空，无未完成材料或任务警告。核查快照在忽略目录 `build-present-clean/verify-logs/pre-archive-main-specs.json` 和 `archive-manifest.json`。归档内报告保留当时实施/复核状态文字，当前状态以本交接记录为准。

本次只同步规格、移动归档并更新STATUS/ROADMAP；产品代码未改动，不重复运行已通过的构建和窗口验收。归档和主规格更新纳入独立 `present-clear-frame` 文档提交，目标仍为已核实的HTTPS origin、main / origin/main；最终提交与远程同步SHA以Git历史及交付结果为准。既有其他归档、desktop-window及未跟踪的 `.agents/` 和两个 `.gitkeep` 保留。

## initialize-vulkan 历史交付

最近完成：`initialize-vulkan`，14/14 任务完成，实现与运行验收通过，已提交推送 `main`，已归档至 `openspec/changes/archive/2026-10-11-initialize-vulkan/`。2026-10-11 环境基线：SDK `C:\VulkanSDK\1.4.341.1` 的 Vulkan 头文件、import library、validation JSON/DLL 和 `vulkaninfoSDK.exe` 可用；loader 1.4.341，Khronos validation 1.4.341，AMD Radeon RX 7900 XT（设备 API 1.4.349）。未修改驱动、注册表或全局 layer 设置。

本次 `vulkaninfoSDK.exe --summary` 原始输出保存在 `build/vulkanBaseline.log`，包含 `VK_LAYER_GAMEPP uses API version 1.3 ... application ... 1.4` 的 loader WARNING。提案探测还曾出现旧 AMD ICD JSON 路径无法打开、EOS Overlay Win64/Win32 重复的 loader 消息；本次 summary 未复现，不能将这些环境诊断归为应用生命周期错误，也不将未复现视为系统已修复。

## initialize-vulkan 实施验证

已接入 `Vulkan::Vulkan`、空 RAII context、instance/messenger/surface/device 和两种 queue；Debug 初步构建及 10/10 CTest 通过（`build-vulkan`）。真实设备为 AMD Radeon RX 7900 XT，graphicsFamily=0、presentFamily=0，去重请求 1 个族，两个角色取得同一个非空队列。

故障目标复用应用入口，八种条件分别返回 1、输出具体阶段/原因、不输出初始化成功：

| 测试条件 | Vulkan 清理记录（之后均 window → GLFW） |
|---|---|
| 缺 validation layer / debug-utils extension | 没有 Vulkan 对象 |
| instance 成功后失败 | instance |
| messenger 成功后失败 | messenger → instance |
| surface 成功后失败 / 无合适设备 / 模拟 vkCreateDevice 返回 -3 | surface → messenger → instance |
| device 成功后失败 | device → surface → messenger → instance |

各记录逐项比较顺序及次数；诊断测试用真实 debug-utils 提交受控 WARNING，观察标识 `ZLRenderer-ControlledDiagnostic`、正文和退出 0。它只验证回调传递。分族及无队列/缺能力由公共选择函数的合成输入覆盖，未宣称本机走过真实分族设备路径。

Debug 的 Win32 窗口验收通过：标题/可见性、移动到 (100,100)、外框调整为 900×600、事件响应、发送标题栏对应的 SC_CLOSE 系统命令（未模拟鼠标点击）、5 秒内退出 0；日志严格为 device → surface → messenger → instance → window → GLFW。初步日志在 `build-vulkan/test-logs/Debug/` 与 `build-vulkan/Debug/window-logs/`。

应用 pNext 回调复现了旧 AMD ICD JSON ERROR 和 EOS Overlay 重复 WARNING，两者消息标识均为 `Loader Message`。完整 stdout/stderr 保留；测试只按 `tests/knownEnvironmentDiagnostics.txt` 中这两条具体消息归因，其他 WARNING/ERROR 仍失败。无应用 validation WARNING/ERROR。未修复、关闭或隐藏系统层；API 1.0 应用未出现 vulkaninfo 的 GamePP API 1.4 请求警告。

从全新 `build-vulkan-clean` 按 README 配置（重新下载 GLFW）、编译 Debug/Release 均成功，使用 `--parallel 2` 限制验证并行度。两个配置各 10/10 CTest 和完整窗口验收通过。命令分别为 `cmake -S . -B build-vulkan-clean -G "Visual Studio 17 2022" -A x64`、`cmake --build build-vulkan-clean --config <Debug/Release> --parallel 2`、`ctest --test-dir build-vulkan-clean -C <Debug/Release> --output-on-failure`、`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/verifyWindow.ps1 -executablePath build-vulkan-clean/<Debug/Release>/zlRenderer.exe`。原始日志保存在该目录的 `test-logs/<配置>/` 与 `<配置>/window-logs/`，两个配置均显示 validation 启用、族 0 和完整清理序列。

附加验证：

- `build-vulkan-asan` 使用 `/EHsc /Zi /fsanitize=address` 和链接 `/DEBUG`，Release 的 10/10 CTest 及同样窗口验收通过，无 ASan 报告。C++ 应用与测试代码插桩，GLFW C 源码及外部驱动/layers 未插桩。验证进程 PATH 临时加入 MSVC `14.38.33130/bin/Hostx64/x64` 以找到 ASan DLL；首次未加时 Windows 返回 `0xc0000135`，发生于程序进入 main 前。MSVC 不支持 UBSan，本次没有可用的 clang 编译器，未运行 UBSan。
- `build-vulkan-analysis` 使用 `/EHsc /analyze`，三个 C++ 目标静态分析通过，无警告。日志 `static-analysis.log`。首次临时覆盖 CXX_FLAGS 时漏 `/EHsc`，触发 C4530；修正验证参数后通过，普通配置一直使用 CMake 默认异常展开参数。
- 单次验证进程设置 `VK_LOADER_DEBUG=layer`，原始日志 `build-vulkan-clean/validation-layer-evidence/window.stderr.log` 明确显示 `Insert instance layer "VK_LAYER_KHRONOS_validation"` 和 `Inserted device layer "VK_LAYER_KHRONOS_validation"`，装载 SDK 中实际 DLL；窗口仍完整验收通过。此诊断变量仅属于该进程，未更改系统 layer 设置。
- 新规格 6 条要求及现有窗口/入门规格逐项核对：正常/失败路径、README 从头配置、强制 validation、公共队列选择与去重、所有权/清理顺序均有上述代码和记录支撑。没有 swapchain、MAILBOX 选择、GPU 提交或等待；既有 AGENTS 约定与 bootstrap 归档未改动。

`openspec validate initialize-vulkan --strict`、工作区与暂存区 `git diff --check` 均通过。产品 Release 二进制不包含测试环境变量、故障注入或受控消息标识。

Git 交付已核实：工程提交 [`ffa2f73065111b18d346934f11ee7772bc4639df`](https://github.com/BlackJoke76/ZLRenderer_v2/commit/ffa2f73065111b18d346934f11ee7772bc4639df) 已推送，`git ls-remote` 与 GitHub API 的 main SHA 均一致。分支 `main`，upstream `origin/main`，origin 为 `https://github.com/BlackJoke76/ZLRenderer_v2.git`。任务完成状态和本交接记录随单独的 `docs(initialize-vulkan)` 提交保存，最终文档 SHA 以 Git 历史为准。本地 `.agents/` 与两个预存 `.gitkeep` 未暂存；既有 bootstrap 归档不变。

2026-10-11，`openspec-verify-change` 已复核：14/14 任务、6/6 新要求、13 个场景均有实现与验证依据，6 项设计决策及代码一致性检查通过；0 critical / warning / suggestion。复跑 Debug/Release 编译，各 10/10 CTest 和真实窗口验收通过；新窗口原始日志在 `build-vulkan-clean/verify-logs/<配置>/`。分族按设计使用合成输入及代码核对；本机仍只实际验证族 0。完整报告现位于 `openspec/changes/archive/2026-10-11-initialize-vulkan/verification.md`。复核记录提交 `e28bfeb43366f0b541476aa8c655c818d3dd936c` 已推送至 main。

2026-10-11，沿用用户“同步主规格后归档”的选择，新增 `openspec/specs/vulkan-context/spec.md`，保留 delta 的 Purpose、6 条要求及 13 个场景原文，仅规范主规格标题和 Requirements 分区。同步前后逐项内容比较一致，`openspec validate --specs --strict` 三份主规格全部通过。归档前 change 的严格校验通过；移动后的 6 个文件（包括 metadata 和 verification）SHA256 均不变，`openspec list --json` 的 active changes 已为空，无未完成任务/材料警告。

归档和主规格更新纳入独立 `initialize-vulkan` 文档提交，目标为已核实的 `main` / `origin/main`；提交及远程 SHA 以 Git 历史和交付结果为准。此次归档时无 active change；已有窗口/入门主规格和 bootstrap 归档保持原样。之后由新的 present-clear-frame change 实施交换链与清屏。

## bootstrap-project 历史记录

已完成 change：`bootstrap-project`，14/14 任务完成，已归档至 `openspec/changes/archive/2026-10-11-bootstrap-project/`。
`desktop-window` 和 `project-onboarding` 已同步为主规格，各 4 条要求，Purpose 与场景原文保留，严格校验通过。

## 环境

- Windows x64；Visual Studio Community 2022 17.14.7。
- VS 自带 CMake 3.31.6-msvc6；Git 2.54.0.windows.1。
- 实际编译器 MSVC 19.38.33145，Windows SDK 10.0.26100.0。
- GLFW 3.4，官方 tag 已核实为 `a74efa0d5628b74adc0426af4c5710e287fa7c2c`。

## 验证记录

2026-10-11，在普通 PowerShell 使用 README 的 VS CMake PATH 配置：

- `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` 与 `cmake --build build --config Debug --parallel` 成功。
- 将目录替换为全新的 `build-clean` 后，两条命令仍成功；运行 `build-clean/Debug/zlRenderer.exe` 打开可见的 ZLRenderer 窗口。
- 用临时 Win32 验证脚本观测窗口可见性、标题、移动和调整到 900×600、事件响应；发送标题栏关闭按钮使用的 `SC_CLOSE` 系统命令，进程在 5 秒内退出，退出码 0。未模拟鼠标点击。
- 初始化故障注入（非法平台）与窗口创建故障注入（宽度为 0）均输出 GLFW 错误和应用错误，退出码 1；创建失败时观测到 GLFW 终止。注入仅在忽略的 build 目录复制源码中进行，临时源码已移除；产品源码未加入测试开关，最终 Debug 重建通过。
- 应用的 MSVC ASan Release 构建与同样窗口验收通过，无 ASan 报告；验证参数 `/EHsc /Zi /fsanitize=address`、链接 `/DEBUG`。GLFW 的 C 源码未被该 C++ 参数插桩。MSVC 不提供 UBSan，本次未运行 UBSan。
- `openspec validate bootstrap-project --strict` 通过。命名、资源释放顺序和文档已对照规格检查。
- verify 阶段复跑普通 Release 和 Debug 窗口验收、两种失败验证二进制均通过。无阻塞问题；1 项非阻塞警告：CMake 当前固定的 `a74efa0d5628b74adc0426af4c5710e287fa7c2c` 是 GLFW 3.4 的附注 tag 对象 SHA，实际 commit SHA 为 `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`。构建来源仍固定，但 README 称其为“提交”与设计要求的 commit SHA 有描述偏差，后续修订时统一。

上述临时验证脚本和产物在忽略的 build 目录内，不属于发布工程。

## 交接与限制

只实现无图形 API 的窗口；尚未实现 Vulkan、swapchain 或 MAILBOX 模式。
OpenSpec schema 读取在本机 Windows 沙箱内会因 realpath EPERM 失败，获准的沙箱外 CLI 调用正常。
GitHub 目标为 https://github.com/BlackJoke76/ZLRenderer_v2（PUBLIC），仓库已存在，复用并保留其 MIT LICENSE。
用户已授权接到远程 main 历史并改用 HTTPS；本地分支 main，origin 为该 HTTPS URL。原远程提交为 `20132ca888504f4e0f73b8dfe5369686340e6187`。
SSH 原因是 host key verification 失败；未更改用户 SSH 设置。
工程提交 `98a9a62f0b36bcf1cd12eaae0dae49274106351e` 已成功推送到 `origin/main`，远程 SHA 核对一致，upstream 为 `origin/main`。
随后用仅包含本 change 交付记录的文档提交同步任务完成状态；最终提交 SHA 以 Git 历史为准。
2026-10-11，用户确认同步主规格后归档；两份主规格与 delta 内容逐项核对一致，active changes 已为空。归档和主规格更新纳入独立 `bootstrap-project` 归档提交，目标仍为 `main` / `origin/main`；同步结果以 Git 历史和远程 SHA 核对为准。
预先存在的 `.agents/` 本地技能文件保留，未纳入本 change 提交。无其他未提交的工程文件。
