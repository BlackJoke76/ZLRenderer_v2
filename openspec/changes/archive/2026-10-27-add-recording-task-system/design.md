# Design

## Context

动机和范围见 proposal.md。当前检查结果：

- `src/` 没有线程池、任务批次或条件变量执行器，不能复用现有同职责实现。`VulkanPresenter::recordGraphFrame` 仍串行调用 static `recordRenderGroup`；本轮不接入或修改它。
- `PreparedGraphFrame`/`PreparedRenderGroup` 已提供本帧准备结果，但依然包含 Vulkan 借用句柄；CPU 执行器不能依赖这些类型，其输入只有 CPU 回调。
- `CMakeLists.txt` 无条件 FetchContent GLFW、`find_package(Vulkan REQUIRED)`，虽然已有纯 CPU 图测试，根配置仍需要图形依赖。因此只增加一个“不链接 Vulkan”的测试目标不够，需要可独立配置的路径。
- 复用现有 `configureCppTarget` 的 C++20、严格警告与 `src` include 规则。tests 采用参数选择案例的独立可执行文件和 CTest 注册方式，可以沿用。
- 现有五份主规格没有 CPU 任务执行能力。`prepare-render-group-recording` 17/17，仍未同步/归档；不在本 change 处理其生命周期或改写其 delta。ROADMAP 尚未列出该完成阶段，实施文档更新时只补正与当前任务阶段直接相关的状态。

## Goals / Non-Goals

**Goals:**
- 一个协调线程提交独立批次，外部线程等待批次，零/单/多 worker 统一结果语义；整个模块能在无图形环境中验收。
- 将任务完成、捕获释放、异常收集和线程关闭的顺序写成明确不变量，避免零 worker 另走一套错误协议。
- 提供稳定 worker 索引，后续调用方可据此管理自己的 worker 局部资源；本轮不预建录制资源接口。

**Non-Goals:**
- 不建任务/批次依赖图，不增全局 waitIdle、嵌套调度、caller-helping、取消、重试、优先级、work stealing 或跨执行器调度。
- 不改变 renderer 的串行执行、命令池归属、barrier、GPU submit、交换链或帧槽生命周期。不做线程数自动探测、性能阈值或基准框架。

## Decisions

### 1. 接口围绕一次提交的批次，而非任务图

建议在 `src/taskSystem.h/.cpp` 定义 `TaskSystem`、`TaskBatchHandle`、`TaskBatchResult`，最小调用链为：

`TaskSystem(workerCount) → submitBatch(tasks) → handle.wait() → shutdown()`。

Task 使用 `std::function<void(std::size_t workerIndex)>`，批次是移动进 submitBatch 的 task vector。仅接受有效回调；空 vector 是合法空批次。无需 public 可变 TaskGroup、seal/submit 状态组合，也不为未来 Vulkan 参数模板化执行器。复制型 std::function 已满足当前需求；move-only callable 支持不属于本轮。

`TaskBatchHandle` 持有共享批次状态，可复制/移动；`wait() const` 返回稳定的只读 `TaskBatchResult` 引用，引用有效期由至少一个该状态的句柄保证。默认或移出后无效句柄等待明确抛出调用错误。销毁句柄仅释放引用，不等待、不取消。执行器不可复制/移动，避免 worker 捕获的宿主地址改变。

`TaskBatchResult` 建议包含固定长度 `taskExceptions`（输入任务索引对应 exception_ptr，成功项为空）和 `failedTaskCount`，任务总数即槽数。结果在接受前预分配，完成后只读；调用方按索引遍历非空项即可得到全部失败，不在 worker 结束时再分配失败列表。`wait()` 不因任务失败自动 rethrow，汇总内的 exception_ptr 可由调用者检查与重新抛出。提交参数错误、资源创建失败与回调失败必须分开：前两者从对应 API 报错，后者只进入批次结果。

替代方案：为每个任务分配 future 增加对象且不直接表达全批完成；只保存第一个异常不足以满足失败汇总；为批次增加依赖边超出当前需求。

### 2. 固定 worker 与共享就绪队列

正 worker 数构造时一次创建固定 `std::jthread` 集合，索引来自创建顺序 `[0, workerCount)`，在该执行器存活期间不变。协调线程 wait 不执行任务，也不占用额外 worker 索引。

一个 mutex 和 condition_variable 保护执行器的就绪队列及 accepting/closing 状态。可将批次状态的共享引用放入 FIFO 队列，每个状态的 nextTaskIndex 在队列锁下领取；索引全部被领取后移除队列项。每次只领取一个任务，多个 worker 可以领取同批不同索引，跨批也可以同时存在运行任务。不承诺多线程的开始/完成次序或特定任务到特定 worker 的分配。

用批次队列代替逐回调队列，是为了在接受前准备好 task vector、异常槽和 remainingCount，然后用一次受锁保护的队列发布实现全有或全无。任何验证/分配/发布失败都必须发生在可执行接受点之前，不能已经运行半批任务再向 submitBatch 调用者报告未接受。worker 从队列中取得状态后，局部共享引用保护运行任务，外部句柄全丢弃也不会销毁在途状态。

零 worker 不创建队列消费者或后台线程，submitBatch 在完成接受校验与状态准备后，按输入顺序直接调用同一个 `executeTask`，workerIndex=0；全部任务终结后返回已完成句柄。空批次无论模式都直接发布完成状态，不入队。

替代方案：每任务新建线程无法复用 worker；锁自由队列或 per-worker deque 尚无性能证据；单个大回调代表全批会使同批任务无法并发。

### 3. 调用者并发边界保持窄而明确

提交、shutdown 和宿主销毁由同一协调线程串行调用，不承诺多个 producer 与 shutdown 的竞争语义。有效句柄可由多个非回调线程同时等待；它们不需要访问执行器本体。

任务彼此独立，用户负责其共享对象同步以及借用对象生命周期。回调不得调用同一执行器的 submitBatch、批次 wait、shutdown 或销毁操作；这是一条调用前置条件，而不是本轮实现嵌套任务、线程内协助或依赖环检测的入口。不对违反前置条件的程序承诺调度进展，测试不以制造未支持重入来验收死锁恢复。

回调可以使用由测试/业务外部控制的有限等待，但执行器不负责终止永不返回的用户代码。零 worker 同步执行是接口行为，调用者不能等待 submitBatch 返回后才释放一个该批正在等待的外部条件。

替代方案：支持回调内阻塞 wait 会让单 worker 饥饿，需要嵌套调度或依赖分析；加入这些机制会模糊本轮可验收边界。

### 4. 执行和完成只用一条公共路径

批次状态拥有回调、异常槽、remainingCount、批次 mutex/cv。任务领取后移出对应回调，释放队列锁，再调用公共 `executeTask`。同一函数用于零 worker inline 路径和 workerLoop：

1. try 调用回调，catch(...) 保存 current_exception，不能让任务异常逃出 worker 入口。
2. 在不持有任何执行器/批次锁时释放局部回调及其捕获。
3. 在批次锁下把异常写入该任务的预分配槽，若有异常增加失败数，再递减 remainingCount。
4. 最后一个任务使完成谓词成立，解锁后 notify_all。wait 使用谓词等待 remainingCount==0，返回只读结果。

回调对象、用户回调和其捕获析构均不得在队列或批次锁内调用，以避免用户代码被隐式包含在内部临界区。mutex 解锁/等待重新获得锁建立结果可见性；不用一个无保护布尔值表示完成，不使用忙等。避免同时持有队列锁和批次锁；领取与完成是两个短且分离的临界区。

允许多批同时在途，但每批 own state/result 不混合。所有回调都释放后才完成计数，因此 wait 返回后调用者可安全释放供该批借用的外部录制输入。对象如果仍由用户的其他引用持有当然不会被执行器强制销毁；承诺只针对执行器对回调及捕获的持有。

### 5. 关闭排空，初始化失败回收

生命周期为 `accepting → draining → stopped`。shutdown 在队列锁下停止接受并通知 worker，worker 在队列有工作时继续领取；只有 closing 且队列已空才退出。其他线程仍在执行的最后任务由其 worker 自己完成，协调线程 join 全部 worker 后才能返回。即使可领取队列已经空，也不能据此跳过 join 或声称任务已完成。

shutdown 不重新抛出任务异常；已完成结果继续由保留句柄拥有，句柄不包含指向执行器 mutex/cv 的借用引用。重复 shutdown 无效操作，关闭后即使提交空批次也报错。析构 noexcept 走同一排空路径，不把 jthread 的 stop 请求解释为取消已接受任务。

线程创建中途失败时构造函数先设 closing、通知已创建 worker，再 join 回收并重抛初始化错误；不能只依靠成员自动析构，因为空队列里的 worker 可能仍在等不再有人发出的通知。构造失败时尚无公开执行器可提交任务。使用仅测试构建的“第 k 个线程创建前失败”检查点验证真实已启动线程的回收；它只证明 host 清理路径，不声称实际 OS 线程创建失败。测试钩子不进入公开 API或正常模块目标。

替代方案：关闭立即丢弃队列会使完成句柄永久等待或需要取消协议；detach 会破坏宿主和捕获生命周期。两者均不采用。

### 6. CPU-only 配置是独立性验收的一部分

新增 `ZL_BUILD_RENDERER` 选项，默认 ON。公共 `configureCppTarget` 和 `include(CTest)` 位于图形分支之外；taskSystem 独立库及 BUILD_TESTING 下的 taskSystemTests 放在公共部分，使用 `find_package(Threads REQUIRED)` 与 `Threads::Threads`。现有 GLFW 获取、Vulkan 查找、zlRenderer 和现有图形/工程测试生成放入 renderer 分支，尽量整体包裹以避免无关目标重构。

renderer=OFF 时只需 C++/CMake 与平台线程支持，不执行 FetchContent GLFW 或查找 Vulkan，不生成需要图形依赖的目标。新增任务目标自身不包含 renderGraph/vulkan/GLFW 头或链接项。renderer=ON 时保留现有目标、严格警告、固定 GLFW 来源和默认配置行为；本轮不把 taskSystem 自动链接并调用到 presenter/Rhi。

实施时在 README 提供可直接运行的命令：

```powershell
cmake -S . -B build-task-system -G "Visual Studio 17 2022" -A x64 -DZL_BUILD_RENDERER=OFF -DBUILD_TESTING=ON
cmake --build build-task-system --config Debug --parallel
ctest --test-dir build-task-system -C Debug --output-on-failure
```

Release 同目录切换配置，并验证 BUILD_TESTING=OFF 时任务库仍可构建。CPU-only 新目录用 `CMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE`、`FETCHCONTENT_FULLY_DISCONNECTED=ON` 和无效 GLFW 源目录作为控制输入，确认没有图形查找/下载及缓存泄漏；也要检查生成目标和真实链接输入，不能只看“测试启动时没有窗口”。默认全图形配置运行现有回归；CPU-only 成功不等于该默认回归成功。

### 7. 零/单/多 worker 的确定性测试矩阵

`tests/taskSystemTests.cpp` 按案例参数与 CTest 注册，常规 worker 数 0、1、至少2（例如4），不由 hardware_concurrency 决定。所有测试有 CTest TIMEOUT，有限次数重复关键竞态，禁止用固定 sleep 后“应该完成”作正确性证据。

| 验证面 | 主要检查 |
|---|---|
| 基础矩阵 | 空批、单任务、多任务、多个连续/在途批次；原子执行次数恰好一次，结果按任务索引关联 |
| 模式 | 0在线程ID/输入顺序上等于协调线程；1在不同固定线程且索引0；多worker记录线程ID到索引稳定映射 |
| 真实并发 | 用promise/cv的外部门控使两个worker同时进入回调并保持在途，证明不同线程实际重叠，不靠日志先后推断 |
| 等待/生命周期 | 门控使某任务未完成时wait不返回；输出可见、重复/多等待者、句柄丢弃、句柄跨执行器析构、捕获释放 |
| 异常 | 多个标准与非标准异常、相反结束顺序、全任务继续完成、失败数及槽位、后续批次成功 |
| 关闭 | 空闲/重复、运行中及排队任务、带失败关闭、隐式析构、关闭后空/非空提交拒绝 |
| 初始化/接受错误 | 空回调整批拒绝；准备/发布失败无部分接受的异常安全核查；第k线程创建前控制失败回收真实已启动线程 |

对等待/关闭中的“尚未返回”，先用明确的已进入回调/已开始等待握手建立状态，再用短的有界 future wait_for 检查，释放门控后等待成功。任何测试断言失败前先释放门控，避免测试自身异常路径在析构 drain 时永久卡住。多worker的线程内稳定索引和回调总计数都要检查，但不要求所有任务轮流均匀分配。

Debug/Release 独立 CPU 配置跑全部新测试；关键等待/失败/关闭案例重复至少100轮，记录轮数。按可用工具做ASan与静态分析；TSan/UBSan不可用就明确记录，不以ASan或重复测试声称数据竞争检测通过。默认 renderer 路径构建及现有CTest回归与CPU验收分开记录。没有渲染执行逻辑变化，本轮不要求重新做八个像素窗口流程或宣称并行录制性能收益。

## Risks / Trade-offs

- [多worker完成顺序不确定] → 结果与异常槽按输入索引固定，测试用门控和任务身份，不验证偶然顺序。
- [关闭等待用户回调永不返回] → 文档限定任务最终终结、禁止回调重入同执行器控制接口；不通过detach或伪取消隐藏问题。
- [最后句柄释放导致批次状态过早销毁] → 队列与运行任务分别持有共享状态；wait/result不借用执行器生命周期。
- [异常收集时再分配失败导致worker退出] → 接受前预分配每任务异常槽，完成路径只存exception_ptr和更新计数，统一catch(...)。
- [构造第k线程失败后join卡住] → 显式closing+notify再回收；测试钩子验证部分成功创建场景。
- [CPU-only默认选项隔离不完整] → 新目录禁用Vulkan查找和GLFW下载控制验证，检查链接与目标；默认ON回归防止破坏既有构建。
- [TSan不可用而并发测试都通过] → 报告实际工具覆盖，不将调度压力测试当作数据竞争证明。

## Migration Plan

1. apply 后新增纯CPU模块和测试，默认不接入渲染器；按任务同步落地基本、异常和生命周期测试及文档。
2. 加入可选renderer构建隔离，完成无图形新目录验收、默认路径回归和报告。验收通过再更新STATUS并按项目规则提交推送；此proposal不执行实施、提交或归档。
3. 回退时只撤销本change的任务模块、相关测试与构建/文档增量，恢复原默认图形构建，不触碰既有组录制成果。后续实际并行录制须另立change，处理每worker命令池和资源生命周期，不由本执行器代办。
