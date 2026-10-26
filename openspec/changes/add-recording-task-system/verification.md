# add-recording-task-system 实施验收

日期：2026-10-26。实现保持纯CPU边界，未修改现有renderGraph/rhi/presenter源文件、未接入Vulkan并行录制。

## 实现与生命周期核查

- `TaskSystem::submitBatch` 验证开放状态及全部回调，构造完整共享批次状态、预分配每任务异常槽，再一次发布批次；零worker按输入顺序在调用线程运行共同executeTask路径，正数创建固定jthread集合。
- 队列mutex只保护closing、批次队列和nextTaskIndex；批次mutex保护result及remainingCount。领取与执行分开，不嵌套两种锁。调用/释放回调在锁外；exchange清空原槽，局部回调及捕获释放后才公布完成。wait谓词等待全部任务，发布结果可见性，不自动重抛任务异常。
- 句柄、队列和运行任务共同拥有批次状态；没有指向执行器的借用引用。丢句柄不取消；保留句柄跨执行器析构仍有效；结果引用需要一个持有该状态的句柄存活。
- 关闭停止接受、唤醒并排空队列，再join全部worker；不能把队列已空等同所有运行任务完成。任务异常catch(...)保存，不退出worker。构造失败先closing+notify，再回收已创建线程后报告失败。回调必须最终返回/抛异常，禁止重入同执行器控制操作。
- 正常库无故障钩子/API。独立taskSystemFailureTests从同一生产cpp编译，测试宏仅增加接受前及创建worker前的控制点和线程计数；库二进制检查确认无四个测试钩子符号。

## 配置、工具与原始证据

编译器MSVC 19.38.33145，CMake为VS2022安装版本，Windows x64。原始日志均位于忽略目录，不作为源码提交：

| 验证 | 结果 | 证据 |
|---|---|---|
| CPU-only Debug | 26/26，9.94秒 | build-task-system/build-Debug.log、ctest-Debug.log、Testing/Temporary/LastTest.log |
| CPU-only Release | 26/26，9.86秒 | build-task-system/build-Release.log、ctest-Release.log |
| CPU-only ASan Release | 26/26，10.83秒，无ASan报告 | build-task-system/asan-configure.log、asan-build.log、asan-tests.log；独立build-task-system-asan |
| CPU-only /analyze Release | 库与两个测试目标构建无警告 | build-task-system/analysis-build.log；独立build-task-system-analysis |
| 默认renderer Debug | 104/104，50.63秒 | build-task-system/default-build-Debug.log、default-ctest-Debug.log |
| 默认renderer Release | 104/104，46.65秒 | build-task-system/default-build-Release.log、default-ctest-Release.log |
| 不生成测试 | BUILD_TESTING=OFF Release库构建通过，仅taskSystem及CMake基础目标 | build-task-system-library/生成项目与Release/taskSystem.lib |

CPU新目录首次配置使用 `-DZL_BUILD_RENDERER=OFF -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_SOURCE_DIR_GLFW=Z:/no-glfw`，Vulkan查找被禁止、图形下载被断开且GLFW来源无效；仍正常配置/运行。新目录无_deps，任务项目的编译/链接输入不含Vulkan/GLFW/RenderGraph。`build-task-system/isolation-checks.log`保存目标与正常库钩子符号检查结果。CMake提示这些图形控制变量未被使用，符合整个图形配置分支未进入；不是编译器警告。未假称物理卸载了本机SDK。

ASan仅给验证进程PATH追加MSVC ASan DLL目录；未修改系统环境。当前只找到MSVC工具链，PATH及常见LLVM/VS LLVM路径未发现可用Clang；未运行TSan/UBSan，不把ASan或重复案例报告成数据竞争检测通过。默认图形回归重跑既有初始化/呈现测试，没有重跑八个像素窗口流程，不宣称并行录制正确或性能改善。

## 场景逐项映射

| 规格场景 | 测试与证据 |
|---|---|
| Run empty and nonempty batches | task-basic-0/1/4：空、单、64任务，逐索引原子计数=1，结果槽数一致 |
| Reject a batch before acceptance | basic的含空回调整批拒绝；task-acceptance-failure-0/1/4在完整准备后、发布前控制失败，拒绝任务不执行，前后有效批次均正常 |
| Keep batch completion independent | task-batches-0/1/4分别8批×17任务；4worker时保持A阻塞而B独立完成，等待B不等待全执行器 |
| Execute inline with zero workers | task-basic-0：协调线程ID、输入序列、索引0、submit返回时已全批完成 |
| Execute on a single worker | task-basic-1：后台固定线程、索引0、无协调线程执行；多批重复可用 |
| Execute independent tasks concurrently | task-concurrent：4回调门控同时在途，4个线程ID与索引互不相同，后续128任务的映射仍一致 |
| Observe completed task outputs | task-waiting-1/4：较早任务抛异常而另一个仍门控，wait不提前返回；释放后输出42与捕获weak_ptr释放可见；0模式的同步输出由basic覆盖 |
| Wait repeatedly and from several external threads | waiting中两个外部线程持有复制句柄、握手后等待，同一只读结果身份一致；basic重复wait不执行任务 |
| Drop handles and keep accepted work | lifetime先占满正数worker，丢弃后续批句柄仍保留捕获，释放门控后任务全部完成；stress重复此路径；零worker按可达同步路径检查 |
| Retain a handle beyond executor lifetime | lifetime在析构后从保留句柄读取两任务结果及一个失败，不借用执行器 |
| Reject an invalid completion handle | basic对默认无效句柄和移出后句柄明确拒绝 |
| Collect several task failures | failures-0/1/4混合标准runtime_error与非标准int=37，原索引1/3正确、总失败2、各任务仍执行一次；4worker控制结束次序反转 |
| Continue accepting work after task failure | failures中新批次成功，旧结果重复读取仍2个失败；acceptance控制失败之后也可继续 |
| Close an idle executor repeatedly | shutdown-0/1/4从未工作空闲关闭、重复关闭；basic关闭后空/非空提交均拒绝 |
| Close with queued running and failing tasks | shutdown-1/4使用独立协调线程，在worker被占满、另一批排队时关闭；外部握手观察尚未返回，释放后全部任务终结、失败仍可读、全部线程join；0模式覆盖同步关闭契约 |
| Destroy without explicit close | lifetime所有模式退出执行器作用域，排空成功和抛异常任务；保留句柄及捕获释放正确 |
| Fail during worker startup | task-startup-failure在索引0/1/3创建前注入，真实已启动/退出计数分别0/1/3，构造报告失败后无遗留线程 |
| Configure and test without graphics dependencies | 新目录禁用图形查找/下载的26项CPU矩阵、生成目标/链接检查、无_deps及关闭测试的库构建 |
| Preserve the existing renderer path | 默认ON两配置104项=既有78+任务26，现有renderer源文件与调用链无diff，任务库未链接进应用 |

每个CPU测试配置中的task-stress-0/1/4各执行100轮waiting、batches、lifetime、failures、shutdown，LastTest.log保留每组“Completed 100 rounds”输出。0worker不伪造后台阻塞/并发覆盖；全部测试有CTest超时。门控清理器在executor/辅助线程join前释放条件，门控本体及被借用输出活得更久，断言失败不把测试卡在自身drain中。

控制注入只证明相应host接受/初始化回收分支；未声称OS线程创建或分配器真的自然失败。验证、预分配、单次队列发布的异常安全顺序已代码核查，队列发布之前没有任何该批工作可见。

## 交付范围

严格change校验与git diff --check通过。README包含CPU-only命令、调用与失败读取示例；ARCHITECTURE记录锁/句柄/关闭边界；ROADMAP区分组录制准备、CPU执行器和未来并行录制接入。此change保持活动，未同步主规格或归档，也未改变prepare-render-group-recording状态。提交与推送回执在交付后补充。

## Git回执

实施提交6994f0404dbfc249d4799e8968e734980fa58fd4已推送至origin/main，git ls-remote确认远端SHA一致。15/15任务已完成；最终勾选与交接回执以独立文档提交保存。未同步主规格、未归档，其他工作区未跟踪内容保留。

## 独立复核（2026-10-26）

复核基线：`75dc398391095460595e074803d0a51bdcaf8626`（实现提交`6994f0404dbfc249d4799e8968e734980fa58fd4`）。本轮按spec-driven的proposal、delta spec、design、tasks重新核查，不把apply的all_done当作结论。没有修改产品源码、测试源码或规划契约，没有同步主规格或归档。

### 三维评分与适用检查

| 维度 | 结论 |
|---|---|
| 完整性 | Task Completion：15/15；Spec Coverage：6/6 ADDED要求均有实现；不存在缺失/不可读跟踪文件 |
| 正确性 | Requirement Implementation Mapping：6/6；Scenario Coverage：19/19，结合代码、重新执行的CPU/真实线程/控制测试核验 |
| 一致性 | Design Adherence：7/7关键决策；Code Pattern Consistency通过，命名、文件组织、C++20/严格警告、公开边界及测试钩子隔离均与工程一致 |

六项适用检查全部执行，无Not verified或部分验证检查；无REMOVED/RENAMED要求，不需要相应迁移核对。CRITICAL=0、WARNING=0、SUGGESTION=0。All checks passed. Ready for archive；本轮不执行归档。

### 需求与实现的独立对应

| 要求 | 实现锚点 | 重新核查的场景及测试 |
|---|---|---|
| Accept independent owned task batches | src/taskSystem.cpp:47-73 | 空/非空、整批拒绝、独立批次共3场景：basic、batches、acceptance控制；验证/预分配在可执行发布前，队列只有一次push_back |
| Execute with explicit zero single and multiple workers | src/taskSystem.cpp:30-43、47-73、92-112 | 零/单/多共3场景：basic-0/1/4和concurrent；零worker索引0按输入顺序同步完成，固定4线程门控重叠及后续128任务索引稳定 |
| Wait for full completion and retain results | src/taskSystem.cpp:8-28、75-112；src/taskSystem.h:20-29 | 输出/捕获、重复及多等待者、丢句柄、跨析构、无效句柄共5场景：waiting、lifetime、basic；结果引用生命周期限制与文档一致 |
| Aggregate callback failures without abandoning the batch | src/taskSystem.cpp:75-90 | 多异常汇总与失败后继续共2场景：failures-0/1/4、waiting；runtime_error与int=37按输入索引1/3保留，所有回调都终结后才完成 |
| Drain accepted work and join workers during shutdown | src/taskSystem.cpp:30-45、114-123 | 空闲重复、有排队/运行/失败任务、隐式析构、启动失败共4场景：shutdown、lifetime、startup控制；closing唤醒后仍排空并join所有已创建线程 |
| Keep control operations external and validate without graphics | src/taskSystem.h:31-46；CMakeLists.txt:15-49 | 无图形配置与默认路径共2场景：全新CPU目录禁用图形查找/下载、链接与目标检查、仅库构建，以及默认两配置104项；单协调线程/禁止重入属于明确调用前置条件，不冒充支持并发submit与shutdown |

上表19个场景逐一与delta的WHEN/THEN对应；前文实施阶段19行场景表已按当前代码和本轮新结果复核，不依赖旧摘要字符串或勾选数量推断。

### 七项设计决策核对

1. 批次API与句柄：TaskSystem/TaskBatchHandle/TaskBatchResult、移动任务vector、只读结果引用、无效句柄错误均符合设计；没有增加任务依赖图或每任务future API。
2. 固定worker与共享批次队列：worker索引绑定创建顺序；队列保护领取索引；队列及运行任务各持有共享状态，外部句柄释放不影响已接受工作。
3. 调用者并发边界：header、README和ARCHITECTURE一致声明单协调线程提交/关闭与回调禁止重入，等待者只触及批次状态。
4. 一条公共执行/完成路径：0worker与后台worker都调用executeTask。exchange清空任务原槽，回调及其捕获释放后才在completionMutex内写异常/计数；完成谓词和结果由同一锁保护，两种锁不嵌套。
5. 排空关闭/初始化失败：队列空不能替代join；构造部分成功后失败先closing+notify再join。控制测试计数核对了0/1/3个真实已启动线程的退出，不以注入替代OS自然失败证据。
6. CPU-only配置：公共任务目标与Threads::Threads不依赖图形分支；默认ON仍提供既有目标；仅库构建可用；正常库不含测试钩子，任务模块未链接进zlRenderer。
7. 确定性测试矩阵：0/1/4显式配置、外部门控/握手、任务身份断言、有限future等待、每模式100轮与CTest TIMEOUT均存在。检查了门控释放器、借用输出和join对象的生命周期顺序；0worker测试只核验可达同步行为，不伪造后台并发。

### 本轮实际重新执行的验证

| 验证 | 结果 | 新证据路径 |
|---|---|---|
| 新CPU-only Debug | 26/26，10.12秒，构建无编译警告 | build-task-system-verify/build-Debug.log、ctest-Debug.log |
| 新CPU-only Release | 26/26，10.03秒，构建无编译警告 | build-task-system-verify/build-Release.log、ctest-Release.log |
| 新ASan CPU-only Release | 26/26，10.66秒，无ASan报告 | build-task-system-verify/asan-build.log、asan-tests.log；build-task-system-verify-asan |
| 新/analyze Release | 库及两个测试目标构建通过，无警告 | build-task-system-verify/analysis-build.log；build-task-system-verify-analysis |
| 新BUILD_TESTING=OFF仅库构建 | Release库成功，仅taskSystem与CMake基础目标 | build-task-system-verify/library-configure.log、library-build.log；build-task-system-verify-library |
| 默认renderer Debug | 104/104，58.21秒 | build-task-system-verify/default-build-Debug.log、default-ctest-Debug.log |
| 默认renderer Release | 104/104，54.09秒 | build-task-system-verify/default-build-Release.log、default-ctest-Release.log |

新CPU目录初始不存在，在禁用Vulkan查找、FetchContent断开及无效GLFW来源的控制条件下从源码重新配置。生成的任务项目无Vulkan/GLFW/RenderGraph编译/链接输入，没有_deps目录；新Release/taskSystem.lib的四个测试钩子符号均不存在。检查结果保存为build-task-system-verify/isolation-checks.log。CMake提示图形控制变量未使用符合分支未进入，不属于编译器警告；未声称SDK被物理卸载。

本轮各CPU测试配置重新执行task-stress-0/1/4的每组100轮；原始stdout在各Testing/Temporary/LastTest.log，已核对三组Completed 100 rounds输出及真实失败控制日志。默认构建仅增量编译，新CPU/ASan/分析/仅库目录均独立。旧实施日志保留，不与本轮数据混用。

### 限制与最终结论

TSan/UBSan仍未运行，当前MSVC及此前核实的可用工具范围不变；ASan及100轮重复不证明不存在数据竞争。上述六项契约/实现检查均有可用证据，工具限制明确保留。没有重跑八个像素窗口流程（设计明确不要求），未接入或声称验证Vulkan并行录制、GPU完成或性能收益；测试故障注入仅证明相应host控制路径。严格OpenSpec校验与git diff --check通过。未发现需整改的问题，可按后续用户请求进入同步/归档流程。
