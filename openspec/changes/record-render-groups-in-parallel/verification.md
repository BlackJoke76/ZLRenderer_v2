# record-render-groups-in-parallel 实施验收

本报告记录本次 apply 的实现与技术验收，不代替后续独立 openspec-verify-change。基线为 `0f09df95f591821970cceb8b59b6f35f505b9226`。仅本 change 的代码、测试、规划与文档纳入交付；前置 change 和主规格未同步/归档。

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
