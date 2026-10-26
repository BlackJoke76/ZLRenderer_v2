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
